#include "ReviewRuntime.h"

#include "dvs/application/Commands.h"
#include "dvs/application/PlaybackCoordinator.h"
#include "dvs/media/MediaProbe.h"
#include "dvs/media/MultiSourceFrameProvider.h"
#include "dvs/persistence/SettingsRepository.h"
#include "dvs/platform/D3d11RenderChannel.h"
#include "dvs/platform/FrameBudget.h"
#include "dvs/platform/FrameMailbox.h"
#include "dvs/platform/GpuTransferActor.h"
#include "dvs/platform/GraphicsDeviceBroker.h"
#include "dvs/platform/PresentationAckMailbox.h"
#include "dvs/platform/RenderActivitySink.h"
#include "dvs/platform/SteadyDeadlineScheduler.h"
#include "dvs/ui/ComparisonSurface.h"
#include "dvs/ui/RenderAckRelay.h"
#include "dvs/ui/ReviewController.h"
#include "dvs/ui/ReviewPreferencesController.h"

#include <QMetaObject>
#include <QObject>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace dvs::app {
namespace {

using namespace std::chrono_literals;

constexpr std::size_t kFrameBudgetBytes = 256U * 1024U * 1024U;
constexpr auto kGraphicsPollInterval = 2ms;
constexpr auto kAdapterShutdownTimeout = 2s;
constexpr auto kTotalShutdownTimeout = 7s;
constexpr auto kShutdownReturnMargin = 50ms;

class RenderActivityBridge final : public platform::IRenderActivitySink {
public:
    void bind(std::shared_ptr<platform::IRenderActivitySink> sink) noexcept {
        sink_.store(std::move(sink), std::memory_order_release);
    }

    void unbind() noexcept {
        sink_.store({}, std::memory_order_release);
    }

    void notifyFramePublished() noexcept override {
        if (const std::shared_ptr<platform::IRenderActivitySink> sink =
                sink_.load(std::memory_order_acquire)) {
            sink->notifyFramePublished();
        }
    }

    void notifyAckPublished() noexcept override {
        if (const std::shared_ptr<platform::IRenderActivitySink> sink =
                sink_.load(std::memory_order_acquire)) {
            sink->notifyAckPublished();
        }
    }

private:
    std::atomic<std::shared_ptr<platform::IRenderActivitySink>> sink_;
};

class GraphicsNotificationPump final {
public:
    GraphicsNotificationPump(std::shared_ptr<platform::GraphicsDeviceBroker> deviceBroker,
                             std::shared_ptr<platform::FrameMailbox> frameMailbox,
                             std::weak_ptr<application::IApplicationEventSink> events)
        : deviceBroker_(std::move(deviceBroker)), frameMailbox_(std::move(frameMailbox)),
          events_(std::move(events)) {
        if (!deviceBroker_ || !frameMailbox_) {
            throw std::invalid_argument{"Graphics notification pump dependencies are required."};
        }
        worker_ = std::thread{[this] { run(); }};
    }

    ~GraphicsNotificationPump() {
        stop();
    }

    GraphicsNotificationPump(const GraphicsNotificationPump&) = delete;
    GraphicsNotificationPump& operator=(const GraphicsNotificationPump&) = delete;

    void requestStop() noexcept {
        stopping_.store(true, std::memory_order_release);
        wake_.notify_all();
    }

    void stop() noexcept {
        requestStop();
        if (!worker_.joinable()) {
            return;
        }
        if (worker_.get_id() == std::this_thread::get_id()) {
            worker_.detach();
            return;
        }
        worker_.join();
    }

private:
    [[nodiscard]] bool drainOne() noexcept {
        std::optional<platform::GraphicsDeviceNotification> notification =
            deviceBroker_->tryConsumeNotification();
        if (!notification.has_value()) {
            return false;
        }

        try {
            std::visit(
                [this](auto&& event) {
                    static_cast<void>(
                        frameMailbox_->advanceDeviceGeneration(event.context.deviceGeneration));
                    if (const std::shared_ptr<application::IApplicationEventSink> events =
                            events_.lock()) {
                        static_cast<void>(events->postCritical(application::ApplicationEvent{
                            std::forward<decltype(event)>(event),
                        }));
                    }
                },
                std::move(*notification));
        } catch (...) {
            stopping_.store(true, std::memory_order_release);
            wake_.notify_all();
        }
        return true;
    }

    void run() noexcept {
        while (!stopping_.load(std::memory_order_acquire)) {
            while (!stopping_.load(std::memory_order_acquire) && drainOne()) {
            }

            std::unique_lock lock{waitMutex_};
            static_cast<void>(wake_.wait_for(lock, kGraphicsPollInterval, [this] {
                return stopping_.load(std::memory_order_acquire);
            }));
        }
    }

    std::shared_ptr<platform::GraphicsDeviceBroker> deviceBroker_;
    std::shared_ptr<platform::FrameMailbox> frameMailbox_;
    std::weak_ptr<application::IApplicationEventSink> events_;
    std::atomic<bool> stopping_{false};
    std::mutex waitMutex_;
    std::condition_variable wake_;
    std::thread worker_;
};

[[nodiscard]] std::chrono::milliseconds
boundedRemainingTime(const std::chrono::steady_clock::time_point deadline,
                     const std::chrono::milliseconds maximum) noexcept {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return 0ms;
    }
    return std::min(maximum, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
}

// This state owns every non-GUI runtime object once shutdown starts. The control thread captures
// the state, never ReviewRuntime::Impl, so a seven-second caller timeout cannot leave a dangling
// `this` or make the GUI thread run an adapter's unbounded destructor join.
class ShutdownWork final {
public:
    void run() noexcept {
        graphicsPump.reset();

        if (coordinator) {
            coordinator->shutdown();
        }
        coordinator.reset();
        coordinatorEventSink.reset();

        // These adapters currently join their workers in their destructors. They are deliberately
        // released on the detached control thread while this state continues to own every
        // dependency that a late decoder operation can access.
        deadlineScheduler.reset();
        frameProvider.reset();
        mediaProbe.reset();
        settingsRepository.reset();
        clock.reset();
        renderChannel.reset();

        if (frameMailbox) {
            frameMailbox->shutdown();
        }
        if (acknowledgementMailbox) {
            acknowledgementMailbox->close();
        }

        const bool actorStopped =
            transferActor &&
            transferActor->shutdown(boundedRemainingTime(deadline, kAdapterShutdownTimeout));
        if (actorStopped && deviceBroker) {
            deviceBroker->shutdown();
        }

        transferActor.reset();
        activityBridge.reset();
        frameMailbox.reset();
        acknowledgementMailbox.reset();
        deviceBroker.reset();
        frameBudget.reset();

        {
            const std::scoped_lock lock(completionMutex);
            result = relayStopped && actorStopped;
            completedAt = std::chrono::steady_clock::now();
            completed = true;
        }
        completion.notify_all();
    }

    [[nodiscard]] bool waitUntil(const std::chrono::steady_clock::time_point until,
                                 bool& completedResult) noexcept {
        std::unique_lock lock(completionMutex);
        if (!completion.wait_until(lock, until, [this] { return completed; })) {
            return false;
        }
        completedResult = result;
        return completedAt <= until;
    }

    std::chrono::steady_clock::time_point deadline;
    bool relayStopped = false;

    std::unique_ptr<GraphicsNotificationPump> graphicsPump;
    std::shared_ptr<application::PlaybackCoordinator> coordinator;
    std::shared_ptr<application::IApplicationEventSink> coordinatorEventSink;
    std::shared_ptr<platform::SteadyDeadlineScheduler> deadlineScheduler;
    std::shared_ptr<media::MultiSourceFrameProvider> frameProvider;
    std::shared_ptr<media::MediaProbe> mediaProbe;
    std::shared_ptr<application::ISettingsRepository> settingsRepository;
    std::shared_ptr<platform::SystemSteadyClock> clock;
    std::shared_ptr<platform::D3d11RenderChannel> renderChannel;
    std::shared_ptr<platform::GpuTransferActor> transferActor;
    std::shared_ptr<RenderActivityBridge> activityBridge;
    std::shared_ptr<platform::FrameMailbox> frameMailbox;
    std::shared_ptr<platform::PresentationAckMailbox> acknowledgementMailbox;
    std::shared_ptr<platform::GraphicsDeviceBroker> deviceBroker;
    std::shared_ptr<platform::FrameBudget> frameBudget;

private:
    std::mutex completionMutex;
    std::condition_variable completion;
    std::chrono::steady_clock::time_point completedAt;
    bool completed = false;
    bool result = false;
};

} // namespace

class ReviewRuntime::Impl final {
public:
    Impl()
        : shutdownWork_(std::make_shared<ShutdownWork>()),
          abandonedWork_(std::make_unique<std::shared_ptr<ShutdownWork>>()) {
        frameBudget_ = std::make_shared<platform::FrameBudget>(kFrameBudgetBytes);
        deviceBroker_ = std::make_shared<platform::GraphicsDeviceBroker>();
        frameMailbox_ =
            std::make_shared<platform::FrameMailbox>(deviceBroker_->currentGeneration());
        acknowledgementMailbox_ = std::make_shared<platform::PresentationAckMailbox>();
        activityBridge_ = std::make_shared<RenderActivityBridge>();
        transferActor_ = std::make_shared<platform::GpuTransferActor>(
            frameBudget_, deviceBroker_, frameMailbox_, activityBridge_);
        renderChannel_ = std::make_shared<platform::D3d11RenderChannel>(transferActor_);
        mediaProbe_ = std::make_shared<media::MediaProbe>();
        settingsRepository_ = std::make_shared<persistence::SettingsRepository>();
        frameProvider_ = std::make_shared<media::MultiSourceFrameProvider>(*frameBudget_);
        deadlineScheduler_ = std::make_shared<platform::SteadyDeadlineScheduler>();
        clock_ = std::make_shared<platform::SystemSteadyClock>();
        coordinator_ =
            application::PlaybackCoordinator::create(domain::SessionId{1U},
                                                     application::PlaybackCoordinator::Dependencies{
                                                         .mediaProbe = mediaProbe_,
                                                         .directFrameProvider = frameProvider_,
                                                         .deadlineScheduler = deadlineScheduler_,
                                                         .clock = clock_,
                                                         .renderChannel = renderChannel_,
                                                     });
        if (!coordinator_) {
            throw std::runtime_error{"The playback coordinator could not be created."};
        }
        coordinatorEventSink_ = coordinator_->eventSink();

        acknowledgementRelay_ = std::make_shared<ui::RenderAckRelay>(
            acknowledgementMailbox_,
            std::weak_ptr<application::IApplicationEventSink>{coordinatorEventSink_});
        activityBridge_->bind(acknowledgementRelay_);

        const std::weak_ptr<application::PlaybackCoordinator> weakCoordinator = coordinator_;
        controller_ = std::make_unique<ui::ReviewController>(ui::ReviewController::Dependencies{
            .submit =
                [weakCoordinator](application::PlaybackCommand command) {
                    if (const std::shared_ptr<application::PlaybackCoordinator> coordinator =
                            weakCoordinator.lock()) {
                        return coordinator->submit(std::move(command));
                    }
                    return application::PortSubmitResult::Closed;
                },
            .snapshot = [weakCoordinator]() -> std::shared_ptr<const application::SessionSnapshot> {
                if (const std::shared_ptr<application::PlaybackCoordinator> coordinator =
                        weakCoordinator.lock()) {
                    return coordinator->snapshot();
                }
                return {};
            },
            .takeCompletedCommands =
                [weakCoordinator] {
                    if (const std::shared_ptr<application::PlaybackCoordinator> coordinator =
                            weakCoordinator.lock()) {
                        return coordinator->takeCompletedCommands();
                    }
                    return std::vector<application::CommandTerminal>{};
                },
        });
        preferences_ = std::make_unique<ui::ReviewPreferencesController>(settingsRepository_);
        graphicsPump_ = std::make_unique<GraphicsNotificationPump>(
            deviceBroker_,
            frameMailbox_,
            std::weak_ptr<application::IApplicationEventSink>{coordinatorEventSink_});
    }

    ~Impl() {
        prepareForSceneGraphRelease();
        static_cast<void>(shutdownAfterSceneGraphRelease());
    }

    [[nodiscard]] ui::ReviewController* controller() noexcept {
        return controller_.get();
    }

    [[nodiscard]] ui::ReviewPreferencesController* preferences() noexcept {
        return preferences_.get();
    }

    [[nodiscard]] bool attachSurface(ui::ComparisonSurface& surface) noexcept {
        if (prepared_ || shutdownCompleted_ || !acknowledgementRelay_ || !deviceBroker_ ||
            !frameMailbox_ || !acknowledgementMailbox_) {
            return false;
        }
        if (surface_ != nullptr) {
            return surface_ == &surface && surface.hasRendererServices();
        }

        acknowledgementRelay_->attach(&surface);
        try {
            if (!surface.attachRendererServices(
                    deviceBroker_, frameMailbox_, acknowledgementMailbox_, acknowledgementRelay_)) {
                acknowledgementRelay_->detach();
                return false;
            }
        } catch (...) {
            acknowledgementRelay_->detach();
            return false;
        }
        surface_ = &surface;
        surfaceDestroyedConnection_ =
            QObject::connect(&surface, &QObject::destroyed, [this] { surface_ = nullptr; });
        return true;
    }

    void prepareForSceneGraphRelease() noexcept {
        if (prepared_) {
            return;
        }
        prepared_ = true;

        if (controller_) {
            controller_->stop();
        }
        if (preferences_) {
            preferences_->stop();
        }
        if (coordinator_) {
            coordinator_->closeRealtimeIngress();
            coordinator_->closeCriticalIngress();
        }
        if (graphicsPump_) {
            graphicsPump_->requestStop();
        }
        if (activityBridge_) {
            activityBridge_->unbind();
        }
        if (acknowledgementRelay_) {
            acknowledgementRelay_->detach();
        }
        if (surface_ != nullptr) {
            surface_->detachRendererServices();
        }
        static_cast<void>(QObject::disconnect(surfaceDestroyedConnection_));
        surfaceDestroyedConnection_ = {};
        surface_ = nullptr;
    }

    [[nodiscard]] bool shutdownAfterSceneGraphRelease() noexcept {
        if (shutdownCompleted_) {
            return shutdownResult_;
        }
        if (!prepared_) {
            return false;
        }

        const std::chrono::steady_clock::time_point absoluteShutdownDeadline =
            std::chrono::steady_clock::now() + kTotalShutdownTimeout;
        // condition_variable waits are not hard real-time scheduling primitives. Keep a small
        // margin so timeout handling itself still returns inside the public seven-second bound.
        const std::chrono::steady_clock::time_point shutdownDeadline =
            absoluteShutdownDeadline - kShutdownReturnMargin;
        const bool relayStopped =
            acknowledgementRelay_ && acknowledgementRelay_->shutdown(boundedRemainingTime(
                                         shutdownDeadline, kAdapterShutdownTimeout));

        std::shared_ptr<ShutdownWork> work = std::move(shutdownWork_);

        work->deadline = shutdownDeadline;
        work->relayStopped = relayStopped;
        work->graphicsPump = std::move(graphicsPump_);
        work->coordinator = std::move(coordinator_);
        work->coordinatorEventSink = std::move(coordinatorEventSink_);
        work->deadlineScheduler = std::move(deadlineScheduler_);
        work->frameProvider = std::move(frameProvider_);
        work->mediaProbe = std::move(mediaProbe_);
        work->settingsRepository = std::move(settingsRepository_);
        work->clock = std::move(clock_);
        work->renderChannel = std::move(renderChannel_);
        work->transferActor = std::move(transferActor_);
        work->activityBridge = std::move(activityBridge_);
        work->frameMailbox = std::move(frameMailbox_);
        work->acknowledgementMailbox = std::move(acknowledgementMailbox_);
        work->deviceBroker = std::move(deviceBroker_);
        work->frameBudget = std::move(frameBudget_);

        // Reserve the failure keepalive before thread creation. If the OS rejects the new thread,
        // abandoning this holder intentionally keeps all live worker objects intact instead of
        // running their unbounded destructors on the GUI thread.
        std::unique_ptr<std::shared_ptr<ShutdownWork>> abandonedWork = std::move(abandonedWork_);

        std::thread controlThread;
        try {
            controlThread = std::thread{[work]() noexcept { work->run(); }};
        } catch (...) {
            *abandonedWork = std::move(work);
            static_cast<void>(abandonedWork.release());
            shutdownResult_ = false;
            shutdownCompleted_ = true;
            return false;
        }
        controlThread.detach();

        bool controlResult = false;
        const bool completedInTime = work->waitUntil(shutdownDeadline, controlResult);
        shutdownResult_ = completedInTime && controlResult;
        shutdownCompleted_ = true;
        return shutdownResult_;
    }

private:
    // Allocate the shutdown state and its emergency leak holder before any worker is created. The
    // actual shutdown path consequently performs no allocation before transferring ownership away
    // from the GUI thread.
    std::shared_ptr<ShutdownWork> shutdownWork_;
    std::unique_ptr<std::shared_ptr<ShutdownWork>> abandonedWork_;
    std::shared_ptr<platform::FrameBudget> frameBudget_;
    std::shared_ptr<platform::GraphicsDeviceBroker> deviceBroker_;
    std::shared_ptr<platform::FrameMailbox> frameMailbox_;
    std::shared_ptr<platform::PresentationAckMailbox> acknowledgementMailbox_;
    std::shared_ptr<RenderActivityBridge> activityBridge_;
    std::shared_ptr<platform::GpuTransferActor> transferActor_;
    std::shared_ptr<platform::D3d11RenderChannel> renderChannel_;
    std::shared_ptr<media::MediaProbe> mediaProbe_;
    std::shared_ptr<application::ISettingsRepository> settingsRepository_;
    std::shared_ptr<media::MultiSourceFrameProvider> frameProvider_;
    std::shared_ptr<platform::SteadyDeadlineScheduler> deadlineScheduler_;
    std::shared_ptr<platform::SystemSteadyClock> clock_;
    std::shared_ptr<application::PlaybackCoordinator> coordinator_;
    std::shared_ptr<application::IApplicationEventSink> coordinatorEventSink_;
    std::shared_ptr<ui::RenderAckRelay> acknowledgementRelay_;
    std::unique_ptr<GraphicsNotificationPump> graphicsPump_;
    std::unique_ptr<ui::ReviewController> controller_;
    std::unique_ptr<ui::ReviewPreferencesController> preferences_;
    ui::ComparisonSurface* surface_ = nullptr;
    QMetaObject::Connection surfaceDestroyedConnection_;
    bool prepared_ = false;
    bool shutdownCompleted_ = false;
    bool shutdownResult_ = false;
};

std::unique_ptr<ReviewRuntime> ReviewRuntime::create() {
    try {
        return std::unique_ptr<ReviewRuntime>{new ReviewRuntime{}};
    } catch (...) {
        return {};
    }
}

ReviewRuntime::ReviewRuntime() : impl_(std::make_unique<Impl>()) {}

ReviewRuntime::~ReviewRuntime() = default;

ui::ReviewController* ReviewRuntime::controller() noexcept {
    return impl_ ? impl_->controller() : nullptr;
}

ui::ReviewPreferencesController* ReviewRuntime::preferences() noexcept {
    return impl_ ? impl_->preferences() : nullptr;
}

bool ReviewRuntime::attachSurface(ui::ComparisonSurface& surface) noexcept {
    return impl_ && impl_->attachSurface(surface);
}

void ReviewRuntime::prepareForSceneGraphRelease() noexcept {
    if (impl_) {
        impl_->prepareForSceneGraphRelease();
    }
}

bool ReviewRuntime::shutdownAfterSceneGraphRelease() noexcept {
    return impl_ && impl_->shutdownAfterSceneGraphRelease();
}

} // namespace dvs::app
