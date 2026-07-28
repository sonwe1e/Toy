#include "dvs/ui/RenderAckRelay.h"

#include "dvs/application/Ports.h"

#include <QMetaObject>
#include <QQuickItem>
#include <QThread>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace dvs::ui {
namespace {

using namespace std::chrono_literals;

class UpdateTarget;

struct RelayState final {
    RelayState(std::shared_ptr<platform::PresentationAckMailbox> mailbox,
               std::weak_ptr<application::IApplicationEventSink> eventSink)
        : acknowledgementMailbox(std::move(mailbox)), events(std::move(eventSink)) {
        if (!acknowledgementMailbox) {
            throw std::invalid_argument{"The presentation acknowledgement mailbox is required."};
        }
    }

    std::shared_ptr<platform::PresentationAckMailbox> acknowledgementMailbox;
    std::weak_ptr<application::IApplicationEventSink> events;
    std::weak_ptr<UpdateTarget> updateTarget;

    std::atomic<bool> closing{false};
    std::atomic<std::uint64_t> wakeSequence{0U};

    mutable std::mutex exitMutex;
    std::condition_variable exitCondition;
    bool exited = false;

    std::atomic<std::uint64_t> frameNotifications{0U};
    std::atomic<std::uint64_t> ackNotifications{0U};
    std::atomic<std::uint64_t> acknowledgementsPopped{0U};
    std::atomic<std::uint64_t> criticalPostsAccepted{0U};
    std::atomic<std::uint64_t> updateRequests{0U};
    std::atomic<std::uint64_t> queuedUpdates{0U};
    std::atomic<std::uint64_t> itemUpdates{0U};
    mutable std::mutex threadMutex;
    std::thread::id workerThread;
    std::thread::id lastCriticalPostThread;
};

class UpdateTarget final : public QObject {
public:
    explicit UpdateTarget(std::shared_ptr<RelayState> state) : state_(std::move(state)) {}

    void attach(QQuickItem* const item) noexcept {
        static_cast<void>(QObject::disconnect(itemDestroyedConnection_));
        item_ = item;
        if (item_ != nullptr) {
            itemDestroyedConnection_ = QObject::connect(
                item_,
                &QObject::destroyed,
                this,
                [this] {
                    attached_.store(false, std::memory_order_release);
                    item_ = nullptr;
                },
                Qt::DirectConnection);
        }
        attached_.store(item != nullptr, std::memory_order_release);
    }

    void detach() noexcept {
        // Publish the tombstone before clearing the GUI-thread-only guarded pointer. A queued
        // callback observes this before consulting the pointer, so it cannot touch a detached item.
        attached_.store(false, std::memory_order_release);
        static_cast<void>(QObject::disconnect(itemDestroyedConnection_));
        itemDestroyedConnection_ = {};
        item_ = nullptr;
    }

    void requestUpdate() noexcept {
        if (!attached_.load(std::memory_order_acquire) ||
            updateQueued_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        bool queued = false;
        try {
            queued =
                QMetaObject::invokeMethod(this, [this] { deliverUpdate(); }, Qt::QueuedConnection);
        } catch (...) {
            queued = false;
        }
        if (queued) {
            state_->queuedUpdates.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        updateQueued_.store(false, std::memory_order_release);
    }

private:
    void deliverUpdate() noexcept {
        updateQueued_.store(false, std::memory_order_release);
        if (!attached_.load(std::memory_order_acquire)) {
            return;
        }
        QQuickItem* const item = item_;
        if (item == nullptr) {
            attached_.store(false, std::memory_order_release);
            return;
        }
        state_->itemUpdates.fetch_add(1U, std::memory_order_relaxed);
        item->update();
    }

    std::shared_ptr<RelayState> state_;
    QQuickItem* item_ = nullptr;
    QMetaObject::Connection itemDestroyedConnection_;
    std::atomic<bool> attached_{false};
    std::atomic<bool> updateQueued_{false};
};

struct UpdateTargetDeleter final {
    void operator()(UpdateTarget* const target) const noexcept {
        if (target == nullptr) {
            return;
        }
        if (target->thread() == QThread::currentThread()) {
            delete target;
            return;
        }
        target->deleteLater();
    }
};

void requestItemUpdate(const std::shared_ptr<RelayState>& state) noexcept {
    state->updateRequests.fetch_add(1U, std::memory_order_relaxed);
    if (const std::shared_ptr<UpdateTarget> target = state->updateTarget.lock()) {
        target->requestUpdate();
    }
}

void wakeWorker(const std::shared_ptr<RelayState>& state) noexcept {
    state->wakeSequence.fetch_add(1U, std::memory_order_release);
    state->wakeSequence.notify_one();
}

void runRelay(const std::shared_ptr<RelayState>& state) noexcept {
    {
        const std::lock_guard lock{state->threadMutex};
        state->workerThread = std::this_thread::get_id();
    }

    std::uint64_t observedWake = state->wakeSequence.load(std::memory_order_acquire);
    for (;;) {
        while (std::optional<application::FrameSetPresented> acknowledgement =
                   state->acknowledgementMailbox->tryPop()) {
            state->acknowledgementsPopped.fetch_add(1U, std::memory_order_relaxed);

            // The pop made capacity available. Queue a render retry before a potentially
            // backpressured critical post so a Full acknowledgement can make progress promptly.
            requestItemUpdate(state);

            if (const std::shared_ptr<application::IApplicationEventSink> events =
                    state->events.lock()) {
                {
                    const std::lock_guard lock{state->threadMutex};
                    state->lastCriticalPostThread = std::this_thread::get_id();
                }
                if (events->postCritical(application::ApplicationEvent{*acknowledgement}) ==
                    application::EventPostResult::Accepted) {
                    state->criticalPostsAccepted.fetch_add(1U, std::memory_order_relaxed);
                }
            }
        }

        if (state->closing.load(std::memory_order_acquire) &&
            state->acknowledgementMailbox->isDrained()) {
            break;
        }

        // atomic::wait avoids both periodic polling and the lost-wakeup window that an atomic
        // predicate plus an independently notified condition variable would create.
        state->wakeSequence.wait(observedWake, std::memory_order_acquire);
        observedWake = state->wakeSequence.load(std::memory_order_acquire);
    }

    {
        const std::lock_guard lock{state->exitMutex};
        state->exited = true;
    }
    state->exitCondition.notify_all();
}

} // namespace

class RenderAckRelay::Impl final {
public:
    Impl(std::shared_ptr<platform::PresentationAckMailbox> acknowledgementMailbox,
         std::weak_ptr<application::IApplicationEventSink> events)
        : state_(
              std::make_shared<RelayState>(std::move(acknowledgementMailbox), std::move(events))),
          updateTarget_(
              std::shared_ptr<UpdateTarget>{new UpdateTarget{state_}, UpdateTargetDeleter{}}) {
        state_->updateTarget = updateTarget_;
        worker_ = std::thread{[state = state_] { runRelay(state); }};
    }

    ~Impl() {
        detach();
        if (worker_.joinable()) {
            const bool closeAlreadyRequested = state_->closing.load(std::memory_order_acquire);
            bool alreadyExited = false;
            {
                const std::lock_guard lock{state_->exitMutex};
                alreadyExited = state_->exited;
            }

            if (!closeAlreadyRequested && shutdown(2s)) {
                updateTarget_.reset();
                return;
            }
            if (alreadyExited) {
                worker_.join();
            } else {
                // A critical event sink may be backpressured beyond the composition bound. The
                // worker captures only shared state and weak GUI/event targets, so late completion
                // is safe. A prior timed-out shutdown is not waited a second time.
                worker_.detach();
            }
        }
        updateTarget_.reset();
    }

    void attach(QQuickItem* const item) noexcept {
        updateTarget_->attach(item);
    }

    void detach() noexcept {
        if (updateTarget_) {
            updateTarget_->detach();
        }
    }

    [[nodiscard]] platform::PresentationAckPushResult
    tryPublishAcknowledgement(const application::FrameSetPresented& acknowledgement) noexcept {
        const platform::PresentationAckPushResult result =
            state_->acknowledgementMailbox->tryPush(acknowledgement);
        if (result == platform::PresentationAckPushResult::Accepted) {
            notifyAckPublished();
        }
        return result;
    }

    void notifyFramePublished() noexcept {
        state_->frameNotifications.fetch_add(1U, std::memory_order_relaxed);
        if (!state_->closing.load(std::memory_order_acquire)) {
            requestItemUpdate(state_);
        }
    }

    void notifyAckPublished() noexcept {
        state_->ackNotifications.fetch_add(1U, std::memory_order_relaxed);
        wakeWorker(state_);
    }

    [[nodiscard]] bool shutdown(const std::chrono::milliseconds timeout) noexcept {
        if (!state_->closing.exchange(true, std::memory_order_acq_rel)) {
            state_->acknowledgementMailbox->close();
        }
        wakeWorker(state_);

        bool exited = false;
        {
            std::unique_lock lock{state_->exitMutex};
            exited =
                state_->exitCondition.wait_for(lock, timeout, [this] { return state_->exited; });
        }
        if (exited && worker_.joinable()) {
            worker_.join();
        }
        return exited;
    }

    [[nodiscard]] bool isClosed() const noexcept {
        return state_->closing.load(std::memory_order_acquire);
    }

    [[nodiscard]] RenderAckRelayStatistics statistics() const noexcept {
        RenderAckRelayStatistics result{
            .frameNotifications = state_->frameNotifications.load(std::memory_order_relaxed),
            .ackNotifications = state_->ackNotifications.load(std::memory_order_relaxed),
            .acknowledgementsPopped =
                state_->acknowledgementsPopped.load(std::memory_order_relaxed),
            .criticalPostsAccepted = state_->criticalPostsAccepted.load(std::memory_order_relaxed),
            .updateRequests = state_->updateRequests.load(std::memory_order_relaxed),
            .queuedUpdates = state_->queuedUpdates.load(std::memory_order_relaxed),
            .itemUpdates = state_->itemUpdates.load(std::memory_order_relaxed),
        };
        const std::lock_guard lock{state_->threadMutex};
        result.workerThread = state_->workerThread;
        result.lastCriticalPostThread = state_->lastCriticalPostThread;
        return result;
    }

private:
    std::shared_ptr<RelayState> state_;
    std::shared_ptr<UpdateTarget> updateTarget_;
    std::thread worker_;
};

RenderAckRelay::RenderAckRelay(
    std::shared_ptr<platform::PresentationAckMailbox> acknowledgementMailbox,
    std::weak_ptr<application::IApplicationEventSink> events,
    QObject* const parent)
    : QObject(parent),
      impl_(std::make_unique<Impl>(std::move(acknowledgementMailbox), std::move(events))) {}

RenderAckRelay::~RenderAckRelay() = default;

void RenderAckRelay::attach(QQuickItem* const item) noexcept {
    impl_->attach(item);
}

void RenderAckRelay::detach() noexcept {
    impl_->detach();
}

platform::PresentationAckPushResult RenderAckRelay::tryPublishAcknowledgement(
    const application::FrameSetPresented& acknowledgement) noexcept {
    return impl_->tryPublishAcknowledgement(acknowledgement);
}

void RenderAckRelay::notifyFramePublished() noexcept {
    impl_->notifyFramePublished();
}

void RenderAckRelay::notifyAckPublished() noexcept {
    impl_->notifyAckPublished();
}

bool RenderAckRelay::shutdown(const std::chrono::milliseconds timeout) noexcept {
    return impl_->shutdown(timeout);
}

bool RenderAckRelay::isClosed() const noexcept {
    return impl_->isClosed();
}

RenderAckRelayStatistics RenderAckRelay::statistics() const noexcept {
    return impl_->statistics();
}

} // namespace dvs::ui
