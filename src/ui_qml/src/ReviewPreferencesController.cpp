#include "dvs/ui/ReviewPreferencesController.h"

#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace dvs::ui {
namespace {

constexpr int kEventDrainIntervalMilliseconds = 16;
constexpr int kSaveDebounceMilliseconds = 150;
constexpr auto kShutdownSaveFlushTimeout = std::chrono::milliseconds{500};

constexpr std::string_view kLargeStepKey = "review.large-step-frames";
constexpr std::string_view kViewModeKey = "review.view-mode";
constexpr std::string_view kDifferenceMetricKey = "review.difference-metric";
constexpr std::string_view kDifferenceGainKey = "review.difference-gain";
constexpr std::string_view kDifferenceEdgeKey = "review.difference-edge";
constexpr std::string_view kDifferenceFilterKey = "review.difference-filter";

class SettingsEventQueue final : public application::IApplicationEventSink {
public:
    [[nodiscard]] application::EventPostResult
    postCritical(application::ApplicationEvent event) noexcept override {
        std::scoped_lock lock{mutex_};
        if (closed_) {
            return application::EventPostResult::Closed;
        }
        try {
            events_.push_back(std::move(event));
        } catch (...) {
            return application::EventPostResult::Closed;
        }
        return application::EventPostResult::Accepted;
    }

    [[nodiscard]] application::EventPostResult
    postRealtime(application::ApplicationEvent event) noexcept override {
        return postCritical(std::move(event));
    }

    void closeRealtimeIngress() noexcept override {}

    void closeCriticalIngress() noexcept override {
        std::scoped_lock lock{mutex_};
        closed_ = true;
    }

    [[nodiscard]] std::vector<application::ApplicationEvent> takeAll() noexcept {
        std::scoped_lock lock{mutex_};
        std::vector<application::ApplicationEvent> result;
        try {
            result.reserve(events_.size());
            while (!events_.empty()) {
                result.push_back(std::move(events_.front()));
                events_.pop_front();
            }
        } catch (...) {
            events_.clear();
        }
        return result;
    }

private:
    std::mutex mutex_;
    std::deque<application::ApplicationEvent> events_;
    bool closed_ = false;
};

[[nodiscard]] const application::RequestContext*
terminalContext(const application::RequestTerminal& terminal) noexcept {
    return std::visit(
        [](const auto& outcome) -> const application::RequestContext* {
            return std::get_if<application::RequestContext>(&outcome.context);
        },
        terminal);
}

template <typename Enum>
[[nodiscard]] std::optional<Enum>
parseEnum(const std::map<std::string, std::string, std::less<>>& values,
          const std::string_view key,
          const std::initializer_list<std::pair<std::string_view, Enum>> options) {
    const auto iterator = values.find(key);
    if (iterator == values.end()) {
        return std::nullopt;
    }
    for (const auto& [text, value] : options) {
        if (iterator->second == text) {
            return value;
        }
    }
    return std::nullopt;
}

} // namespace

class ReviewPreferencesController::Impl final {
public:
    Impl(ReviewPreferencesController& owner,
         std::shared_ptr<application::ISettingsRepository> repository)
        : owner_(owner), repository_(std::move(repository)),
          events_(std::make_shared<SettingsEventQueue>()) {
        if (!repository_) {
            throw std::invalid_argument{"Review preferences require a settings repository."};
        }
        QObject::connect(&eventTimer_, &QTimer::timeout, &owner_, [this] { drainEvents(); });
        eventTimer_.setInterval(kEventDrainIntervalMilliseconds);
        QObject::connect(&saveTimer_, &QTimer::timeout, &owner_, [this] { saveNow(); });
        saveTimer_.setSingleShot(true);
        saveTimer_.setInterval(kSaveDebounceMilliseconds);
        eventTimer_.start();
        beginLoad();
    }

    ~Impl() {
        stop();
    }

    [[nodiscard]] int largeStepFrames() const noexcept {
        return largeStepFrames_;
    }

    [[nodiscard]] ViewMode viewMode() const noexcept {
        return viewMode_;
    }

    [[nodiscard]] DifferenceMetric differenceMetric() const noexcept {
        return differenceMetric_;
    }

    [[nodiscard]] DifferenceGain differenceGain() const noexcept {
        return differenceGain_;
    }

    [[nodiscard]] DifferenceEdge differenceEdge() const noexcept {
        return differenceEdge_;
    }

    [[nodiscard]] DifferenceFilter differenceFilter() const noexcept {
        return differenceFilter_;
    }

    void setLargeStepFrames(const int value) {
        if ((value != 5 && value != 10) || value == largeStepFrames_) {
            return;
        }
        largeStepFrames_ = value;
        changed();
    }

    void setViewMode(const ViewMode value) {
        setEnum(viewMode_, value, ViewMode::SideBySide, ViewMode::Difference);
    }

    void setDifferenceMetric(const DifferenceMetric value) {
        if (value < DifferenceMetric::RgbAbsolute || value > DifferenceMetric::Heatmap ||
            value == differenceMetric_) {
            return;
        }
        differenceMetric_ = value;
        changed();
    }

    void setDifferenceGain(const DifferenceGain value) {
        if (value < DifferenceGain::Gain1x || value > DifferenceGain::Gain16x ||
            value == differenceGain_) {
            return;
        }
        differenceGain_ = value;
        changed();
    }

    void setDifferenceEdge(const DifferenceEdge value) {
        setEnum(differenceEdge_, value, DifferenceEdge::Edge0And1, DifferenceEdge::Edge1And2);
    }

    void setDifferenceFilter(const DifferenceFilter value) {
        if (value < DifferenceFilter::Nearest || value > DifferenceFilter::Bicubic ||
            value == differenceFilter_) {
            return;
        }
        differenceFilter_ = value;
        changed();
    }

    void stop() noexcept {
        if (stopped_ || stopping_) {
            return;
        }
        stopping_ = true;
        eventTimer_.stop();
        saveTimer_.stop();
        try {
            flushPendingChangesBeforeStop();
        } catch (...) {
            // Shutdown remains bounded even if snapshot construction or an adapter submission
            // unexpectedly throws. Outstanding requests are canceled below.
        }
        stopped_ = true;
        if (repository_ && pendingLoad_.has_value()) {
            repository_->cancel(*pendingLoad_);
        }
        if (repository_ && pendingSave_.has_value()) {
            repository_->cancel(*pendingSave_);
        }
        pendingLoad_.reset();
        pendingSave_.reset();
        events_->closeRealtimeIngress();
        events_->closeCriticalIngress();
        repository_.reset();
        stopping_ = false;
    }

private:
    template <typename Enum>
    void setEnum(Enum& target, const Enum value, const Enum minimum, const Enum maximum) {
        if (value < minimum || value > maximum || value == target) {
            return;
        }
        target = value;
        changed();
    }

    void changed() {
        localChanges_ = true;
        dirty_ = true;
        Q_EMIT owner_.preferencesChanged();
        scheduleSave();
    }

    [[nodiscard]] application::RequestContext nextContext() noexcept {
        const std::uint64_t requestId = nextRequestId_;
        if (nextRequestId_ != (std::numeric_limits<std::uint64_t>::max)()) {
            ++nextRequestId_;
        }
        return application::RequestContext{
            .sessionId = domain::SessionId{1U},
            .sessionEpoch = domain::SessionEpoch{1U},
            .requestId = domain::RequestId{requestId},
        };
    }

    void beginLoad() {
        const application::RequestContext context = nextContext();
        if (repository_->submit(application::SettingsLoadRequest{.context = context}, events_) ==
            application::PortSubmitResult::Accepted) {
            pendingLoad_ = context;
            return;
        }
        loadFinished_ = true;
    }

    void scheduleSave() {
        if (!stopped_ && !stopping_ && loadFinished_ && !pendingSave_.has_value()) {
            saveTimer_.start();
        }
    }

    void saveNow() {
        if (stopped_ || !dirty_ || !loadFinished_ || pendingSave_.has_value()) {
            return;
        }
        writeKnownValues(settings_.values);
        const application::RequestContext context = nextContext();
        const application::SettingsSaveRequest request{
            .context = context,
            .settings = settings_,
        };
        const application::PortSubmitResult result = repository_->submit(request, events_);
        if (result == application::PortSubmitResult::Accepted) {
            pendingSave_ = context;
            pendingSaveSnapshot_ = request.settings;
            dirty_ = false;
        } else if (result == application::PortSubmitResult::Busy && !stopping_) {
            saveTimer_.start();
        }
    }

    void flushPendingChangesBeforeStop() {
        if (!repository_ || (!dirty_ && !pendingSave_.has_value())) {
            return;
        }

        // The Qt event loop has normally stopped by this point. Drain repository completions
        // directly so an already-delivered load can preserve unknown keys and the latest
        // debounced values can reach the atomic save boundary before cancellation.
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + kShutdownSaveFlushTimeout;
        do {
            drainEvents();
            if (loadFinished_ && dirty_ && !pendingSave_.has_value()) {
                saveNow();
            }
            drainEvents();
            if (!dirty_ && !pendingSave_.has_value()) {
                return;
            }
            QThread::msleep(1U);
        } while (std::chrono::steady_clock::now() < deadline);
    }

    void drainEvents() noexcept {
        if (stopped_) {
            return;
        }
        try {
            for (application::ApplicationEvent& event : events_->takeAll()) {
                if (auto* loaded = std::get_if<application::SettingsLoaded>(&event)) {
                    if (pendingLoad_.has_value() && loaded->context == *pendingLoad_) {
                        settings_ = std::move(loaded->settings);
                        if (!localChanges_) {
                            applyKnownValues(settings_.values);
                        }
                    }
                    continue;
                }
                const auto* terminal = std::get_if<application::RequestTerminal>(&event);
                if (terminal == nullptr) {
                    continue;
                }
                const application::RequestContext* context = terminalContext(*terminal);
                if (context == nullptr) {
                    continue;
                }
                if (pendingLoad_.has_value() && *context == *pendingLoad_) {
                    pendingLoad_.reset();
                    loadFinished_ = true;
                    scheduleSave();
                    continue;
                }
                if (pendingSave_.has_value() && *context == *pendingSave_) {
                    const bool succeeded =
                        std::holds_alternative<application::RequestSucceeded>(*terminal);
                    pendingSave_.reset();
                    if (succeeded) {
                        settings_ = pendingSaveSnapshot_;
                    } else {
                        dirty_ = true;
                    }
                    scheduleSave();
                }
            }
        } catch (...) {
            stop();
        }
    }

    void applyKnownValues(const std::map<std::string, std::string, std::less<>>& values) {
        int nextLargeStep = 10;
        if (const auto iterator = values.find(kLargeStepKey);
            iterator != values.end() && iterator->second == "5") {
            nextLargeStep = 5;
        }
        const ViewMode nextViewMode =
            parseEnum<ViewMode>(values,
                                kViewModeKey,
                                {{"side-by-side", ViewMode::SideBySide},
                                 {"three-up", ViewMode::ThreeUp},
                                 {"reference-focus", ViewMode::ReferenceFocus},
                                 {"difference", ViewMode::Difference}})
                .value_or(ViewMode::SideBySide);
        const DifferenceMetric nextMetric =
            parseEnum<DifferenceMetric>(values,
                                        kDifferenceMetricKey,
                                        {{"rgb-absolute", DifferenceMetric::RgbAbsolute},
                                         {"luma", DifferenceMetric::Luma},
                                         {"chroma", DifferenceMetric::Chroma},
                                         {"heatmap", DifferenceMetric::Heatmap}})
                .value_or(DifferenceMetric::RgbAbsolute);
        const DifferenceGain nextGain =
            parseEnum<DifferenceGain>(values,
                                      kDifferenceGainKey,
                                      {{"1x", DifferenceGain::Gain1x},
                                       {"2x", DifferenceGain::Gain2x},
                                       {"4x", DifferenceGain::Gain4x},
                                       {"8x", DifferenceGain::Gain8x},
                                       {"16x", DifferenceGain::Gain16x}})
                .value_or(DifferenceGain::Gain1x);
        const DifferenceEdge nextReference =
            parseEnum<DifferenceEdge>(values,
                                      kDifferenceEdgeKey,
                                      {{"0-1", DifferenceEdge::Edge0And1},
                                       {"0-2", DifferenceEdge::Edge0And2},
                                       {"1-2", DifferenceEdge::Edge1And2}})
                .value_or(DifferenceEdge::Edge0And1);
        const DifferenceFilter nextFilter =
            parseEnum<DifferenceFilter>(values,
                                        kDifferenceFilterKey,
                                        {{"nearest", DifferenceFilter::Nearest},
                                         {"bilinear", DifferenceFilter::Bilinear},
                                         {"bicubic", DifferenceFilter::Bicubic}})
                .value_or(DifferenceFilter::Bilinear);

        const bool changed = largeStepFrames_ != nextLargeStep || viewMode_ != nextViewMode ||
                             differenceMetric_ != nextMetric || differenceGain_ != nextGain ||
                             differenceEdge_ != nextReference || differenceFilter_ != nextFilter;
        largeStepFrames_ = nextLargeStep;
        viewMode_ = nextViewMode;
        differenceMetric_ = nextMetric;
        differenceGain_ = nextGain;
        differenceEdge_ = nextReference;
        differenceFilter_ = nextFilter;
        if (changed) {
            Q_EMIT owner_.preferencesChanged();
        }
    }

    void writeKnownValues(std::map<std::string, std::string, std::less<>>& values) const {
        values.insert_or_assign(std::string{kLargeStepKey}, largeStepFrames_ == 5 ? "5" : "10");
        const char* viewModeName = "side-by-side";
        switch (viewMode_) {
        case ViewMode::ThreeUp:
            viewModeName = "three-up";
            break;
        case ViewMode::ReferenceFocus:
            viewModeName = "reference-focus";
            break;
        case ViewMode::Difference:
            viewModeName = "difference";
            break;
        case ViewMode::SideBySide:
            break;
        }
        values.insert_or_assign(std::string{kViewModeKey}, std::string{viewModeName});
        static constexpr std::string_view metrics[] = {"rgb-absolute", "luma", "chroma", "heatmap"};
        static constexpr std::string_view gains[] = {"1x", "2x", "4x", "8x", "16x"};
        static constexpr std::string_view filters[] = {"nearest", "bilinear", "bicubic"};
        values.insert_or_assign(std::string{kDifferenceMetricKey},
                                std::string{metrics[static_cast<std::size_t>(differenceMetric_)]});
        values.insert_or_assign(std::string{kDifferenceGainKey},
                                std::string{gains[static_cast<std::size_t>(differenceGain_)]});
        const char* edgeName = "0-1";
        switch (differenceEdge_) {
        case DifferenceEdge::Edge0And2:
            edgeName = "0-2";
            break;
        case DifferenceEdge::Edge1And2:
            edgeName = "1-2";
            break;
        case DifferenceEdge::Edge0And1:
            break;
        }
        values.insert_or_assign(std::string{kDifferenceEdgeKey}, std::string{edgeName});
        values.insert_or_assign(std::string{kDifferenceFilterKey},
                                std::string{filters[static_cast<std::size_t>(differenceFilter_)]});
    }

    ReviewPreferencesController& owner_;
    std::shared_ptr<application::ISettingsRepository> repository_;
    std::shared_ptr<SettingsEventQueue> events_;
    QTimer eventTimer_;
    QTimer saveTimer_;
    application::SettingsSnapshot settings_;
    application::SettingsSnapshot pendingSaveSnapshot_;
    std::optional<application::RequestContext> pendingLoad_;
    std::optional<application::RequestContext> pendingSave_;
    std::uint64_t nextRequestId_ = 1U;
    int largeStepFrames_ = 10;
    ViewMode viewMode_ = ViewMode::SideBySide;
    DifferenceMetric differenceMetric_ = DifferenceMetric::RgbAbsolute;
    DifferenceGain differenceGain_ = DifferenceGain::Gain1x;
    DifferenceEdge differenceEdge_ = DifferenceEdge::Edge0And1;
    DifferenceFilter differenceFilter_ = DifferenceFilter::Bilinear;
    bool loadFinished_ = false;
    bool localChanges_ = false;
    bool dirty_ = false;
    bool stopping_ = false;
    bool stopped_ = false;
};

ReviewPreferencesController::ReviewPreferencesController(
    std::shared_ptr<application::ISettingsRepository> repository, QObject* const parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, std::move(repository))) {}

ReviewPreferencesController::~ReviewPreferencesController() = default;

int ReviewPreferencesController::largeStepFrames() const noexcept {
    return impl_->largeStepFrames();
}

ReviewPreferencesController::ViewMode ReviewPreferencesController::viewMode() const noexcept {
    return impl_->viewMode();
}

ReviewPreferencesController::DifferenceMetric
ReviewPreferencesController::differenceMetric() const noexcept {
    return impl_->differenceMetric();
}

ReviewPreferencesController::DifferenceGain
ReviewPreferencesController::differenceGain() const noexcept {
    return impl_->differenceGain();
}

ReviewPreferencesController::DifferenceEdge
ReviewPreferencesController::differenceEdge() const noexcept {
    return impl_->differenceEdge();
}

ReviewPreferencesController::DifferenceFilter
ReviewPreferencesController::differenceFilter() const noexcept {
    return impl_->differenceFilter();
}

void ReviewPreferencesController::setLargeStepFrames(const int value) {
    impl_->setLargeStepFrames(value);
}

void ReviewPreferencesController::setViewMode(const ViewMode value) {
    impl_->setViewMode(value);
}

void ReviewPreferencesController::setDifferenceMetric(const DifferenceMetric value) {
    impl_->setDifferenceMetric(value);
}

void ReviewPreferencesController::setDifferenceGain(const DifferenceGain value) {
    impl_->setDifferenceGain(value);
}

void ReviewPreferencesController::setDifferenceEdge(const DifferenceEdge value) {
    impl_->setDifferenceEdge(value);
}

void ReviewPreferencesController::setDifferenceFilter(const DifferenceFilter value) {
    impl_->setDifferenceFilter(value);
}

void ReviewPreferencesController::stop() noexcept {
    if (thread() != QThread::currentThread()) {
        static_cast<void>(QMetaObject::invokeMethod(this, "stop", Qt::QueuedConnection));
        return;
    }
    impl_->stop();
}

} // namespace dvs::ui
