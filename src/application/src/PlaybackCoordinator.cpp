#include "dvs/application/PlaybackCoordinator.h"

#include "dvs/domain/ComparisonValidator.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace dvs::application {
namespace {

using namespace std::chrono_literals;

constexpr auto kExactFrameDeadline = 5s;

enum class PendingPhase {
    kOpeningProvider,
    kOpeningFirstFrame,
    kSeekingFrame,
    kClosingProvider,
};

struct CommandIdentity final {
    std::uint64_t sessionId = 0;
    std::uint64_t sessionEpoch = 0;
    std::uint64_t commandId = 0;

    [[nodiscard]] constexpr bool operator==(const CommandIdentity&) const noexcept = default;
};

struct CommandIdentityHash final {
    [[nodiscard]] std::size_t operator()(const CommandIdentity& identity) const noexcept {
        const auto mix = [](const std::size_t seed, const std::uint64_t value) noexcept {
            return seed ^ (std::hash<std::uint64_t>{}(value) + 0x9e3779b97f4a7c15ULL +
                           (seed << 6U) + (seed >> 2U));
        };
        return mix(mix(std::hash<std::uint64_t>{}(identity.sessionId), identity.sessionEpoch),
                   identity.commandId);
    }
};

[[nodiscard]] domain::MediaError probeCoordinatorError(const domain::MediaErrorCode code,
                                                       std::optional<domain::SourceId> source,
                                                       std::string technicalDetail,
                                                       const bool recoverable = true) {
    return domain::makeMediaError(code,
                                  domain::MediaOperation::kMediaProbe,
                                  source,
                                  recoverable,
                                  std::move(technicalDetail));
}

[[nodiscard]] domain::MediaError coordinatorError(const domain::MediaErrorCode code,
                                                  std::string technicalDetail,
                                                  const bool recoverable = true) {
    return domain::makeMediaError(code,
                                  domain::MediaOperation::kMediaDecode,
                                  std::nullopt,
                                  recoverable,
                                  std::move(technicalDetail));
}

[[nodiscard]] domain::MediaError presentationError(std::string technicalDetail) {
    return domain::makeMediaError(domain::MediaErrorCode::kFramePresentationTimedOut,
                                  domain::MediaOperation::kFramePresentation,
                                  std::nullopt,
                                  true,
                                  std::move(technicalDetail));
}

[[nodiscard]] bool matchesContext(const EventContext& context,
                                  const PlaybackRequestContext& expected) noexcept {
    const auto* const actual = std::get_if<PlaybackRequestContext>(&context);
    return actual != nullptr && *actual == expected;
}

[[nodiscard]] bool matchesContext(const EventContext& context,
                                  const FrameRequestContext& expected) noexcept {
    const auto* const actual = std::get_if<FrameRequestContext>(&context);
    return actual != nullptr && *actual == expected;
}

// Checked signed 64-bit addition. Returns the sum, or nullopt when a + b would over/underflow
// the int64 range, so callers never commit signed-overflow UB into a MediaTime or a tick delta.
[[nodiscard]] std::optional<std::int64_t> checkedAddInt64(const std::int64_t a,
                                                          const std::int64_t b) noexcept {
    if (b > 0 && a > INT64_MAX - b) {
        return std::nullopt;
    }
    if (b < 0 && a < INT64_MIN - b) {
        return std::nullopt;
    }
    return a + b;
}

// Checked signed 64-bit subtraction: returns a - b, or nullopt when the result would
// over/underflow the int64 range (e.g. near the steady-clock epoch/rest bounds).
[[nodiscard]] std::optional<std::int64_t> checkedSubInt64(const std::int64_t a,
                                                          const std::int64_t b) noexcept {
    if (b > 0) {
        if (a < INT64_MIN + b) {
            return std::nullopt;
        }
    } else if (b < 0) {
        if (a > INT64_MAX + b) {
            return std::nullopt;
        }
    }
    return a - b;
}

// Applies a microsecond duration to a steady_clock time_point without UB: the conversion of the
// duration to clock ticks is verified exact, and the addition is checked against the time_point's
// representable bounds. Returns nullopt when the result cannot be represented, so callers fall
// through to the existing arithmetic-overflow path instead of producing an invalid deadline.
[[nodiscard]] std::optional<std::chrono::steady_clock::time_point>
addDurationSafe(std::chrono::steady_clock::time_point tp,
                std::chrono::microseconds delta) noexcept {
    const auto ticks = std::chrono::duration_cast<std::chrono::steady_clock::duration>(delta);
    if (std::chrono::duration_cast<std::chrono::microseconds>(ticks) != delta) {
        return std::nullopt;
    }
    const std::chrono::steady_clock::duration epoch = tp.time_since_epoch();
    const std::int64_t count = ticks.count();
    if (count > 0 && epoch > std::chrono::steady_clock::duration::max() - ticks) {
        return std::nullopt;
    }
    if (count < 0 && epoch < std::chrono::steady_clock::duration::min() - ticks) {
        return std::nullopt;
    }
    return tp + ticks;
}

class CoordinatorEventTarget {
public:
    virtual ~CoordinatorEventTarget() = default;

    [[nodiscard]] virtual EventPostResult postCritical(ApplicationEvent event) noexcept = 0;
    [[nodiscard]] virtual EventPostResult postRealtime(ApplicationEvent event) noexcept = 0;
    virtual void closeRealtimeIngress() noexcept = 0;
    virtual void closeCriticalIngress() noexcept = 0;
};

// Async adapters may retain this gate, but the gate never owns its coordinator target. detach()
// first prevents new calls and then waits for calls that already crossed the gate, making target
// destruction safe without allowing an adapter worker to own the coordinator itself.
class CoordinatorEventSinkGate final : public IApplicationEventSink {
public:
    explicit CoordinatorEventSinkGate(CoordinatorEventTarget& target) noexcept : target_(&target) {}

    [[nodiscard]] EventPostResult postCritical(ApplicationEvent event) noexcept override {
        CoordinatorEventTarget* const target = enter();
        if (target == nullptr) {
            return EventPostResult::Closed;
        }
        const EventPostResult result = target->postCritical(std::move(event));
        leave();
        return result;
    }

    [[nodiscard]] EventPostResult postRealtime(ApplicationEvent event) noexcept override {
        CoordinatorEventTarget* const target = enter();
        if (target == nullptr) {
            return EventPostResult::Closed;
        }
        const EventPostResult result = target->postRealtime(std::move(event));
        leave();
        return result;
    }

    void closeRealtimeIngress() noexcept override {
        CoordinatorEventTarget* const target = enter();
        if (target == nullptr) {
            return;
        }
        target->closeRealtimeIngress();
        leave();
    }

    void closeCriticalIngress() noexcept override {
        CoordinatorEventTarget* const target = enter();
        if (target == nullptr) {
            return;
        }
        target->closeCriticalIngress();
        leave();
    }

    void detach() noexcept {
        std::unique_lock lock(mutex_);
        target_ = nullptr;
        idle_.wait(lock, [this] { return activeCalls_ == 0U; });
    }

private:
    [[nodiscard]] CoordinatorEventTarget* enter() noexcept {
        std::scoped_lock lock(mutex_);
        if (target_ == nullptr) {
            return nullptr;
        }
        ++activeCalls_;
        return target_;
    }

    void leave() noexcept {
        std::scoped_lock lock(mutex_);
        --activeCalls_;
        if (activeCalls_ == 0U) {
            idle_.notify_all();
        }
    }

    std::mutex mutex_;
    std::condition_variable idle_;
    CoordinatorEventTarget* target_ = nullptr;
    std::size_t activeCalls_ = 0U;
};

} // namespace

class PlaybackCoordinator::Impl final : public CoordinatorEventTarget {
public:
    Impl(const domain::SessionId sessionId, Dependencies dependencies)
        : dependencies_(std::move(dependencies)),
          eventSink_(std::make_shared<CoordinatorEventSinkGate>(*this)) {
        state_.sessionId = sessionId;
        publishSnapshot();
        worker_ = std::thread([this] { run(); });
    }

    ~Impl() {
        shutdown();
    }

    [[nodiscard]] std::shared_ptr<IApplicationEventSink> eventSink() const noexcept {
        return eventSink_;
    }

    void shutdown() noexcept {
        shutdownImpl();
    }

    [[nodiscard]] PortSubmitResult submit(PlaybackCommand command) {
        {
            std::scoped_lock lock(ingressMutex_);
            if (shuttingDown_) {
                return PortSubmitResult::Closed;
            }
            if (commands_.size() >= kCommandIngressCapacity) {
                return PortSubmitResult::Busy;
            }
            commands_.push_back(std::move(command));
        }
        condition_.notify_one();
        return PortSubmitResult::Accepted;
    }

    [[nodiscard]] std::shared_ptr<const SessionSnapshot> snapshot() const {
        std::scoped_lock lock(publicationMutex_);
        return publishedSnapshot_;
    }

    [[nodiscard]] std::vector<CommandTerminal> takeCompletedCommands() {
        std::scoped_lock lock(publicationMutex_);
        std::vector<CommandTerminal> terminals = std::move(completedCommands_);
        completedCommands_.clear();
        return terminals;
    }

    [[nodiscard]] EventPostResult postCritical(ApplicationEvent event) noexcept override {
        {
            std::unique_lock lock(ingressMutex_);
            // A critical event is a terminal or a complete exact frame set. It may wait for bounded
            // queue space, but it must never be discarded merely because the UI is momentarily
            // behind; callers only see Closed once coordinator teardown starts.
            condition_.wait(lock, [this] {
                return shuttingDown_ || criticalIngressClosed_ ||
                       criticalEvents_.size() < kCriticalEventCapacity;
            });
            if (shuttingDown_ || criticalIngressClosed_) {
                return EventPostResult::Closed;
            }
            criticalEvents_.push_back(std::move(event));
        }
        condition_.notify_one();
        return EventPostResult::Accepted;
    }

    [[nodiscard]] EventPostResult postRealtime(ApplicationEvent event) noexcept override {
        {
            std::scoped_lock lock(ingressMutex_);
            if (shuttingDown_ || realtimeIngressClosed_) {
                return EventPostResult::Closed;
            }
            realtimeEvent_ = std::move(event);
        }
        condition_.notify_one();
        return EventPostResult::Accepted;
    }

    void closeRealtimeIngress() noexcept override {
        std::scoped_lock lock(ingressMutex_);
        realtimeIngressClosed_ = true;
        realtimeEvent_.reset();
    }

    void closeCriticalIngress() noexcept override {
        {
            std::scoped_lock lock(ingressMutex_);
            criticalIngressClosed_ = true;
        }
        condition_.notify_all();
    }

private:
    struct PendingCommand final {
        PendingPhase phase;
        CommandContext command;
        PlaybackRequestContext providerContext;
        std::optional<FrameRequestContext> frameContext;
        std::optional<FrameSet> set;
        std::optional<domain::FrameId> expectedFrame;
        std::optional<std::uint64_t> presentationTimerId;
        bool framePublished = false;
        bool providerSucceeded = false;
        bool framePresented = false;
    };

    struct PendingProbeSlot final {
        RequestContext context;
        domain::SourceId sourceId = 0;
        std::filesystem::path sourcePath;
        domain::ComparisonRole role = domain::ComparisonRole::kPrediction;
        std::string displayName;
        std::optional<domain::MediaDescriptor> descriptor;
        // VFR probes publish the shared canonical-source timeline; CFR probes leave this nullopt.
        // Captured on first arrival so a stale duplicate ProbeCompleted cannot overwrite a good
        // value.
        std::optional<std::shared_ptr<const domain::FrameTimeline>> timeline;
        bool succeeded = false;
    };

    struct PendingProbe final {
        CommandContext command;
        std::vector<PendingProbeSlot> slots;
        bool preservesReadySession = false;
    };

    struct PendingPlaybackFrame final {
        FrameRequestContext context;
        domain::FrameId expectedFrame;
        std::optional<FrameSet> set;
        std::optional<std::uint64_t> presentationTimerId;
        bool framePublished = false;
        bool providerSucceeded = false;
        bool framePresented = false;
    };

    struct PlaybackRun final {
        PlaybackRequestContext providerContext;
        PlaybackRequestContext cadenceContext;
        domain::FrameId firstTarget;
        domain::FrameId nextMinimum;
        domain::FrameId anchorFrame;
        std::chrono::steady_clock::time_point wallAnchor;
        std::optional<std::uint64_t> cadenceTimerId;
        std::optional<domain::FrameId> cadenceTarget;
        std::optional<PendingPlaybackFrame> frame;
        bool restartFromEnd = false;
        bool pauseRequested = false;
    };

    using WorkItem = std::variant<PlaybackCommand, ApplicationEvent>;

    [[nodiscard]] static domain::SessionEpoch increment(const domain::SessionEpoch value) noexcept {
        return domain::SessionEpoch{value.value() + 1U};
    }

    [[nodiscard]] static domain::PlaybackGeneration
    increment(const domain::PlaybackGeneration value) noexcept {
        return domain::PlaybackGeneration{value.value() + 1U};
    }

    [[nodiscard]] bool acceptsCommand(const CommandContext& context) const noexcept {
        return context.sessionId == state_.sessionId && context.sessionEpoch == state_.sessionEpoch;
    }

    [[nodiscard]] PlaybackRequestContext makePlaybackContext() {
        return PlaybackRequestContext{
            .request =
                RequestContext{
                    .sessionId = state_.sessionId,
                    .sessionEpoch = state_.sessionEpoch,
                    .requestId = domain::RequestId{nextRequestId_++},
                },
            .playbackGeneration = state_.playbackGeneration,
        };
    }

    [[nodiscard]] RequestContext makeRequestContext() {
        return RequestContext{
            .sessionId = state_.sessionId,
            .sessionEpoch = state_.sessionEpoch,
            .requestId = domain::RequestId{nextRequestId_++},
        };
    }

    [[nodiscard]] PlaybackRequestContext currentPlaybackScope() const noexcept {
        return PlaybackRequestContext{
            .request =
                RequestContext{
                    .sessionId = state_.sessionId,
                    .sessionEpoch = state_.sessionEpoch,
                    .requestId = domain::RequestId{0},
                },
            .playbackGeneration = state_.playbackGeneration,
        };
    }

    void publishSnapshot() {
        auto snapshot = std::make_shared<const SessionSnapshot>(state_);
        std::scoped_lock lock(publicationMutex_);
        publishedSnapshot_ = std::move(snapshot);
    }

    void completeCommand(const CommandContext& context,
                         const CommandOutcome outcome,
                         std::optional<domain::MediaError> error = std::nullopt) {
        std::scoped_lock lock(publicationMutex_);
        completedCommands_.push_back(CommandTerminal{
            .context = context,
            .outcome = outcome,
            .error = std::move(error),
        });
    }

    void publishError(const domain::MediaError& error) {
        state_.lastError = error;
        publishSnapshot();
    }

    [[nodiscard]] bool claimCommand(const CommandContext& context) {
        return seenCommands_
            .insert(CommandIdentity{
                .sessionId = context.sessionId.value(),
                .sessionEpoch = context.sessionEpoch.value(),
                .commandId = context.commandId.value(),
            })
            .second;
    }

    void rejectCommand(const CommandContext& context,
                       const CommandOutcome outcome,
                       domain::MediaError error) {
        publishError(error);
        completeCommand(context, outcome, std::move(error));
    }

    void resetToEmpty() {
        state_.sessionState = domain::SessionState::kEmpty;
        state_.playbackState = domain::PlaybackState::kPaused;
        state_.displayedFrame.reset();
        state_.requestedFrame.reset();
        state_.canonicalFrameCount = 0U;
        state_.lastError.reset();
        sources_.reset();
        compatibilityReport_.reset();
        canonicalTimeline_.reset();
    }

    void failPending(domain::MediaError error, const CommandOutcome outcome) {
        if (!pending_.has_value()) {
            return;
        }
        const PendingCommand failed = std::move(*pending_);
        pending_.reset();

        if (failed.presentationTimerId.has_value()) {
            static_cast<void>(dependencies_.deadlineScheduler->cancel(*failed.presentationTimerId));
        }
        dependencies_.directFrameProvider->cancel(failed.providerContext);
        const bool failedExactFrame = failed.phase == PendingPhase::kOpeningFirstFrame ||
                                      failed.phase == PendingPhase::kSeekingFrame;
        if (failedExactFrame && failed.frameContext.has_value()) {
            dependencies_.renderChannel->clear(failed.providerContext);
            state_.playbackGeneration = increment(state_.playbackGeneration);
        }

        if (failed.phase == PendingPhase::kOpeningProvider ||
            failed.phase == PendingPhase::kOpeningFirstFrame ||
            failed.phase == PendingPhase::kClosingProvider) {
            sources_.reset();
            compatibilityReport_.reset();
            canonicalTimeline_.reset();
            state_.sessionState = domain::SessionState::kError;
            state_.playbackState = domain::PlaybackState::kPaused;
            state_.displayedFrame.reset();
            state_.requestedFrame.reset();
            state_.canonicalFrameCount = 0U;
        } else {
            state_.sessionState = domain::SessionState::kReady;
            state_.playbackState = domain::PlaybackState::kPaused;
            state_.requestedFrame.reset();
        }
        state_.lastError = error;
        publishSnapshot();
        completeCommand(failed.command, outcome, std::move(error));
    }

    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point>
    playbackDue(const domain::FrameId target) const {
        if (!playbackRun_.has_value() || !canonicalTimeline_.has_value() ||
            target < playbackRun_->firstTarget) {
            return std::nullopt;
        }
        // due(n) = wallAnchor + timelineTime(n) - timelineTime(anchorFrame). The two timeline
        // lookups are independent and checked, the per-frame offset is computed with checked exact
        // microsecond arithmetic, and the addition to the wall anchor is checked against the
        // clock's representable range, so any unrepresentable due falls through to the existing
        // arithmetic-overflow path rather than committing signed-overflow/time-point-overflow UB.
        const auto startTarget = domain::canonicalFrameStartTime(*canonicalTimeline_, target);
        const auto startAnchor =
            domain::canonicalFrameStartTime(*canonicalTimeline_, playbackRun_->anchorFrame);
        if (!startTarget || !startAnchor) {
            return std::nullopt;
        }
        const auto deltaMicroseconds =
            checkedSubInt64(startTarget.value().microseconds(), startAnchor.value().microseconds());
        if (!deltaMicroseconds.has_value()) {
            return std::nullopt;
        }
        return addDurationSafe(playbackRun_->wallAnchor,
                               std::chrono::microseconds{*deltaMicroseconds});
    }

    [[nodiscard]] domain::FrameId
    playbackTargetAt(const std::chrono::steady_clock::time_point now) const {
        if (!playbackRun_.has_value() || !canonicalTimeline_.has_value()) {
            return domain::FrameId{0};
        }
        const PlaybackRun& run = *playbackRun_;
        // Catch-up: the media time that should be showing now is the anchored frame's time plus
        // the wall-clock elapsed since anchoring. canonicalFrameAtOrBefore maps it back to a
        // display-order frame, then we clamp so the cadence never regresses and never passes the
        // final frame. Long gaps land directly on the true next PTS instead of accumulating.
        const auto startAnchor =
            domain::canonicalFrameStartTime(*canonicalTimeline_, run.anchorFrame);
        if (!startAnchor) {
            return run.nextMinimum;
        }
        // elapsedMicroseconds can be negative only on a non-monotonic clock read; anchor on zero
        // so the computed media time never walks backward past the anchor frame.
        const std::int64_t elapsedMicroseconds = std::max<std::int64_t>(
            0, std::chrono::duration_cast<std::chrono::microseconds>(now - run.wallAnchor).count());
        const std::uint64_t frameCount = state_.canonicalFrameCount;
        if (frameCount == 0) {
            return run.nextMinimum;
        }
        const std::int64_t maximum = static_cast<std::int64_t>(frameCount - 1U);
        domain::FrameId frame = run.nextMinimum;
        // The target media time is built from checked addition. Overflow means the wall clock has
        // advanced far past the media end, so fall back to the final frame rather than accumulate
        // through undefined arithmetic.
        const auto targetMedia =
            checkedAddInt64(startAnchor.value().microseconds(), elapsedMicroseconds);
        if (targetMedia.has_value()) {
            const auto atOrBefore = domain::canonicalFrameAtOrBefore(
                *canonicalTimeline_, domain::MediaTime{*targetMedia});
            if (atOrBefore) {
                frame = atOrBefore.value();
            }
        } else {
            frame = domain::FrameId{maximum};
        }
        return domain::FrameId{std::min(maximum, std::max(run.nextMinimum.value(), frame.value()))};
    }

    void stopPlayback(std::optional<domain::MediaError> error = std::nullopt,
                      const bool publish = true) {
        if (!playbackRun_.has_value()) {
            return;
        }
        PlaybackRun stopped = std::move(*playbackRun_);
        playbackRun_.reset();
        if (stopped.cadenceTimerId.has_value()) {
            static_cast<void>(dependencies_.deadlineScheduler->cancel(*stopped.cadenceTimerId));
        }
        if (stopped.frame.has_value() && stopped.frame->presentationTimerId.has_value()) {
            static_cast<void>(
                dependencies_.deadlineScheduler->cancel(*stopped.frame->presentationTimerId));
        }
        dependencies_.directFrameProvider->cancel(stopped.providerContext);
        if (stopped.frame.has_value() && stopped.frame->framePublished) {
            dependencies_.renderChannel->clear(stopped.providerContext);
        }
        state_.playbackGeneration = increment(state_.playbackGeneration);
        state_.playbackState = domain::PlaybackState::kPaused;
        state_.requestedFrame.reset();
        if (error.has_value()) {
            state_.lastError = std::move(error);
        }
        if (publish) {
            publishSnapshot();
        }
    }

    [[nodiscard]] bool submitPlaybackFrame(const domain::FrameId target) {
        if (!playbackRun_.has_value() || playbackRun_->frame.has_value()) {
            return false;
        }
        const PlaybackRequestContext playback = makePlaybackContext();
        const FrameRequestContext frameContext{
            .playback = playback,
            .deviceGeneration = state_.deviceGeneration,
        };
        playbackRun_->frame = PendingPlaybackFrame{
            .context = frameContext,
            .expectedFrame = target,
        };

        const std::uint64_t timerId = nextTimerId_++;
        const DeadlineRequest deadline{
            .context = playback,
            .timerId = timerId,
            .due = dependencies_.clock->now() + kExactFrameDeadline,
        };
        const PortSubmitResult deadlineResult =
            dependencies_.deadlineScheduler->schedule(deadline, eventSink_);
        if (deadlineResult != PortSubmitResult::Accepted) {
            stopPlayback(presentationError(
                "The playback frame presentation deadline could not be scheduled."));
            return false;
        }
        playbackRun_->frame->presentationTimerId = timerId;

        const FrameRequest request{
            .context = frameContext,
            .frameId = target,
            .priority = FrameRequestPriority::Sequential,
        };
        const PortSubmitResult providerResult =
            dependencies_.directFrameProvider->submit(request, eventSink_);
        if (providerResult != PortSubmitResult::Accepted) {
            stopPlayback(coordinatorError(
                domain::MediaErrorCode::kMediaDecodeFailed,
                "The direct frame provider did not accept a sequential playback request."));
            return false;
        }
        state_.playbackState = domain::PlaybackState::kBuffering;
        state_.requestedFrame = target;
        publishSnapshot();
        return true;
    }

    [[nodiscard]] bool schedulePlaybackTarget(const domain::FrameId target) {
        if (!playbackRun_.has_value() || playbackRun_->frame.has_value() ||
            playbackRun_->cadenceTimerId.has_value()) {
            return false;
        }
        const auto due = playbackDue(target);
        if (!due.has_value()) {
            stopPlayback(coordinatorError(domain::MediaErrorCode::kArithmeticOverflow,
                                          "The playback frame boundary could not be represented."));
            return false;
        }
        if (*due <= dependencies_.clock->now()) {
            const domain::FrameId dueTarget = playbackRun_->restartFromEnd
                                                  ? playbackRun_->firstTarget
                                                  : playbackTargetAt(dependencies_.clock->now());
            return submitPlaybackFrame(dueTarget);
        }

        const std::uint64_t timerId = nextTimerId_++;
        playbackRun_->cadenceTimerId = timerId;
        playbackRun_->cadenceTarget = target;
        const DeadlineRequest deadline{
            .context = playbackRun_->cadenceContext,
            .timerId = timerId,
            .due = *due,
        };
        const PortSubmitResult result =
            dependencies_.deadlineScheduler->schedule(deadline, eventSink_);
        if (result != PortSubmitResult::Accepted) {
            stopPlayback(
                presentationError("The playback cadence deadline could not be scheduled."));
            return false;
        }
        state_.playbackState = domain::PlaybackState::kPlaying;
        state_.requestedFrame.reset();
        publishSnapshot();
        return true;
    }

    void beginPlay(const PlayCommand& command) {
        if (!sources_.has_value() || state_.sessionState != domain::SessionState::kReady ||
            !state_.displayedFrame.has_value()) {
            rejectCommand(command.context,
                          CommandOutcome::Failed,
                          coordinatorError(domain::MediaErrorCode::kInvalidArgument,
                                           "Playback requires a ready comparison set.",
                                           false));
            return;
        }
        if (!state_.graphicsReady) {
            rejectCommand(
                command.context,
                CommandOutcome::Failed,
                domain::makeMediaError(domain::MediaErrorCode::kGraphicsUnavailable,
                                       domain::MediaOperation::kGraphicsInitialization,
                                       std::nullopt,
                                       true,
                                       "Playback requires an available graphics device."));
            return;
        }
        if (state_.canonicalFrameCount <= 1U) {
            rejectCommand(command.context,
                          CommandOutcome::Failed,
                          coordinatorError(domain::MediaErrorCode::kInvalidArgument,
                                           "A one-frame comparison set cannot play continuously.",
                                           false));
            return;
        }

        const std::int64_t maximum = static_cast<std::int64_t>(state_.canonicalFrameCount - 1U);
        const bool restartFromEnd = state_.displayedFrame->value() == maximum;
        const domain::FrameId firstTarget{restartFromEnd ? 0 : state_.displayedFrame->value() + 1};
        state_.playbackGeneration = increment(state_.playbackGeneration);
        const PlaybackRequestContext providerContext = currentPlaybackScope();
        const PlaybackRequestContext cadenceContext = makePlaybackContext();
        playbackRun_ = PlaybackRun{
            .providerContext = providerContext,
            .cadenceContext = cadenceContext,
            .firstTarget = firstTarget,
            .nextMinimum = firstTarget,
            .anchorFrame = *state_.displayedFrame,
            .wallAnchor = dependencies_.clock->now(),
            .restartFromEnd = restartFromEnd,
        };
        state_.lastError.reset();
        if (!schedulePlaybackTarget(firstTarget)) {
            completeCommand(command.context, CommandOutcome::Failed, state_.lastError);
            return;
        }
        completeCommand(command.context, CommandOutcome::Succeeded);
    }

    void beginPause(const PauseCommand& command) {
        if (!playbackRun_.has_value()) {
            completeCommand(command.context, CommandOutcome::Succeeded);
            return;
        }
        if (!playbackRun_->frame.has_value() || !playbackRun_->frame->framePublished) {
            stopPlayback();
            completeCommand(command.context, CommandOutcome::Succeeded);
            return;
        }

        playbackRun_->pauseRequested = true;
        state_.playbackState = domain::PlaybackState::kPaused;
        publishSnapshot();
        completeCommand(command.context, CommandOutcome::Succeeded);
    }

    void submitFirstOrSeekFrame(const domain::FrameId frameId, const PendingPhase phase) {
        if (!pending_.has_value()) {
            return;
        }
        PendingCommand& pending = *pending_;
        pending.phase = phase;
        pending.set.reset();
        pending.expectedFrame = frameId;
        pending.presentationTimerId.reset();
        pending.framePublished = false;
        pending.providerSucceeded = false;
        pending.framePresented = false;
        const PlaybackRequestContext playback = makePlaybackContext();
        const FrameRequestContext frameContext{
            .playback = playback,
            .deviceGeneration = state_.deviceGeneration,
        };
        pending.providerContext = playback;
        pending.frameContext = frameContext;
        state_.playbackState = domain::PlaybackState::kSeeking;
        state_.requestedFrame = frameId;
        publishSnapshot();

        const FrameRequest request{
            .context = frameContext,
            .frameId = frameId,
            .priority = FrameRequestPriority::Exact,
        };
        const std::uint64_t timerId = nextTimerId_++;
        const DeadlineRequest deadline{
            .context = playback,
            .timerId = timerId,
            .due = dependencies_.clock->now() + kExactFrameDeadline,
        };
        const PortSubmitResult deadlineResult =
            dependencies_.deadlineScheduler->schedule(deadline, eventSink_);
        if (deadlineResult != PortSubmitResult::Accepted) {
            failPending(presentationError("The exact frame deadline could not be scheduled."),
                        deadlineResult == PortSubmitResult::Busy ? CommandOutcome::Busy
                                                                 : CommandOutcome::Closed);
            return;
        }
        pending.presentationTimerId = timerId;
        if (dependencies_.directFrameProvider->submit(request, eventSink_) !=
            PortSubmitResult::Accepted) {
            failPending(coordinatorError(
                            domain::MediaErrorCode::kMediaDecodeFailed,
                            "The direct frame provider did not accept an exact frame request."),
                        CommandOutcome::Busy);
        }
    }

    // Shared post-validation open path. Stores the validated set and compatibility report, builds
    // the provider request from the canonical timeline, and submits the open to the frame
    // provider. Both the direct-descriptor and probed-paths entry points funnel through here.
    void beginOpenValidated(CommandContext commandContext,
                            domain::ValidatedComparisonSet set,
                            domain::CompatibilityReport report,
                            domain::CanonicalTimeline timeline) {
        if (sources_.has_value() || state_.displayedFrame.has_value()) {
            const PlaybackRequestContext previousScope = currentPlaybackScope();
            dependencies_.directFrameProvider->cancel(previousScope);
        }
        state_.sessionEpoch = increment(state_.sessionEpoch);
        state_.playbackGeneration = increment(state_.playbackGeneration);
        sources_ = std::move(set);
        compatibilityReport_ = std::move(report);
        canonicalTimeline_ = std::move(timeline);
        state_.sessionState = domain::SessionState::kLoading;
        state_.playbackState = domain::PlaybackState::kSeeking;
        state_.displayedFrame.reset();
        state_.requestedFrame = domain::FrameId{0};
        state_.canonicalFrameCount = static_cast<std::uint64_t>(sources_->canonicalFrameCount());
        state_.lastError.reset();

        const PlaybackRequestContext context = makePlaybackContext();
        pending_ = PendingCommand{
            .phase = PendingPhase::kOpeningProvider,
            .command = commandContext,
            .providerContext = context,
            .frameContext = std::nullopt,
            .set = std::nullopt,
        };
        publishSnapshot();

        const FrameProviderOpenRequest request{
            .context = context,
            .sources = std::vector<domain::ComparisonSource>(
                sources_->sources().begin(), sources_->sources().end()),
            .timeline = *canonicalTimeline_,
        };
        if (dependencies_.directFrameProvider->submit(request, eventSink_) !=
            PortSubmitResult::Accepted) {
            failPending(
                coordinatorError(domain::MediaErrorCode::kMediaDecodeFailed,
                                 "The direct frame provider did not accept the comparison set."),
                CommandOutcome::Busy);
        }
    }

    // Validates a set of pre-probed descriptors and opens the provider. Used by the direct open
    // path (CLI diagnostics and tests) where descriptors arrive already probed.
    void beginOpenDirect(const OpenDirectComparisonCommand& command) {
        auto validation = domain::ComparisonValidator::validate(command.sources);
        if (!validation) {
            if (state_.sessionState == domain::SessionState::kReady && sources_.has_value()) {
                rejectCommand(command.context, CommandOutcome::Failed, validation.error());
                return;
            }
            resetToEmpty();
            state_.sessionState = domain::SessionState::kInvalid;
            state_.playbackState = domain::PlaybackState::kPaused;
            state_.displayedFrame.reset();
            state_.requestedFrame.reset();
            state_.canonicalFrameCount = 0U;
            state_.lastError = validation.error();
            publishSnapshot();
            completeCommand(command.context, CommandOutcome::Failed, validation.error());
            return;
        }

        // The canonical source's descriptor determines the timeline. A direct open of a VFR
        // canonical source arrives without a probed runtime timeline, so it fails clearly here
        // without disturbing an existing ready session.
        const auto& set = validation.value().set;
        const bool canonicalIsVfr = set.canonicalDescriptor().timingConfidence ==
                                    domain::TimingConfidence::kVariableFrameRate;
        if (canonicalIsVfr) {
            rejectCommand(command.context,
                          CommandOutcome::Failed,
                          coordinatorError(domain::MediaErrorCode::kFrameTimelineInvalid,
                                           "A VFR canonical source requires a probed runtime "
                                           "timeline.",
                                           false));
            return;
        }

        beginOpenValidated(command.context,
                           std::move(validation.value().set),
                           std::move(validation.value().report),
                           domain::CanonicalTimeline{*set.canonicalRate()});
    }

    // Cancels every in-flight probe of the pending open and completes that command as canceled,
    // so a freshly submitted open can start its own probe set. Late completions from the canceled
    // probes no longer match any pending slot and are ignored.
    void supersedePendingProbes() {
        if (!pendingProbe_.has_value()) {
            return;
        }
        const PendingProbe pending = std::move(*pendingProbe_);
        pendingProbe_.reset();
        for (const auto& slot : pending.slots) {
            dependencies_.mediaProbe->cancel(slot.context);
        }
        completeCommand(pending.command, CommandOutcome::Canceled);
    }

    void failProbe(domain::MediaError error,
                   const CommandOutcome outcome,
                   const domain::SessionState initialFailureState) {
        if (!pendingProbe_.has_value()) {
            return;
        }
        const PendingProbe pending = std::move(*pendingProbe_);
        pendingProbe_.reset();
        for (const auto& slot : pending.slots) {
            dependencies_.mediaProbe->cancel(slot.context);
        }

        if (pending.preservesReadySession) {
            state_.lastError = error;
        } else {
            state_.sessionState = initialFailureState;
            state_.playbackState = domain::PlaybackState::kPaused;
            state_.displayedFrame.reset();
            state_.requestedFrame.reset();
            state_.canonicalFrameCount = 0U;
            state_.lastError = error;
        }
        publishSnapshot();
        completeCommand(pending.command, outcome, std::move(error));
    }

    [[nodiscard]] PendingProbeSlot* probeSlot(const RequestContext& context) noexcept {
        if (!pendingProbe_.has_value()) {
            return nullptr;
        }
        for (auto& slot : pendingProbe_->slots) {
            if (slot.context == context) {
                return &slot;
            }
        }
        return nullptr;
    }

    void finishProbeIfComplete() {
        if (!pendingProbe_.has_value()) {
            return;
        }
        for (const auto& slot : pendingProbe_->slots) {
            if (!slot.succeeded || !slot.descriptor.has_value()) {
                return;
            }
        }

        PendingProbe completed = std::move(*pendingProbe_);
        pendingProbe_.reset();

        // Build the ComparisonSource vector from the completed probe slots, preserving the
        // submission-order source ids, roles, and display names from the original command.
        std::vector<domain::ComparisonSource> comparisonSources;
        comparisonSources.reserve(completed.slots.size());
        for (auto& slot : completed.slots) {
            comparisonSources.push_back(domain::ComparisonSource{
                .id = slot.sourceId,
                .role = slot.role,
                .descriptor = std::move(*slot.descriptor),
                .displayName = std::move(slot.displayName),
            });
        }

        auto validation = domain::ComparisonValidator::validate(comparisonSources);
        if (!validation) {
            if (state_.sessionState == domain::SessionState::kReady && sources_.has_value()) {
                state_.lastError = validation.error();
                publishSnapshot();
                completeCommand(completed.command, CommandOutcome::Failed, validation.error());
            } else {
                resetToEmpty();
                state_.sessionState = domain::SessionState::kInvalid;
                state_.playbackState = domain::PlaybackState::kPaused;
                state_.displayedFrame.reset();
                state_.requestedFrame.reset();
                state_.canonicalFrameCount = 0U;
                state_.lastError = validation.error();
                publishSnapshot();
                completeCommand(completed.command, CommandOutcome::Failed, validation.error());
            }
            return;
        }

        // Build the canonical timeline from the canonical source's descriptor and probe slot.
        // CFR canonical sources carry their rational rate; VFR canonical sources carry the probed
        // shared timeline published alongside the descriptor.
        const auto& set = validation.value().set;
        const domain::SourceId canonicalId = set.canonicalSourceId();
        const bool canonicalIsVfr = set.canonicalDescriptor().timingConfidence ==
                                    domain::TimingConfidence::kVariableFrameRate;

        std::optional<domain::CanonicalTimeline> activeTimeline;
        if (canonicalIsVfr) {
            std::shared_ptr<const domain::FrameTimeline> canonicalTimelinePtr;
            for (const auto& slot : completed.slots) {
                if (slot.sourceId == canonicalId && slot.timeline.has_value() && *slot.timeline) {
                    canonicalTimelinePtr = *slot.timeline;
                    break;
                }
            }
            if (!canonicalTimelinePtr) {
                const domain::MediaError timelineError = probeCoordinatorError(
                    domain::MediaErrorCode::kFrameTimelineInvalid,
                    canonicalId,
                    "The VFR canonical source probe did not publish a runtime timeline.",
                    false);
                if (state_.sessionState == domain::SessionState::kReady && sources_.has_value()) {
                    state_.lastError = timelineError;
                    publishSnapshot();
                    completeCommand(completed.command, CommandOutcome::Failed, timelineError);
                } else {
                    resetToEmpty();
                    state_.sessionState = domain::SessionState::kInvalid;
                    state_.playbackState = domain::PlaybackState::kPaused;
                    state_.displayedFrame.reset();
                    state_.requestedFrame.reset();
                    state_.canonicalFrameCount = 0U;
                    state_.lastError = timelineError;
                    publishSnapshot();
                    completeCommand(completed.command, CommandOutcome::Failed, timelineError);
                }
                return;
            }
            activeTimeline = domain::CanonicalTimeline{std::move(canonicalTimelinePtr)};
        } else {
            if (!set.canonicalRate().has_value()) {
                const domain::MediaError rateError = probeCoordinatorError(
                    domain::MediaErrorCode::kInvalidCfrTiming,
                    canonicalId,
                    "The CFR canonical source is missing a rational rate.",
                    false);
                if (state_.sessionState == domain::SessionState::kReady && sources_.has_value()) {
                    state_.lastError = rateError;
                    publishSnapshot();
                    completeCommand(completed.command, CommandOutcome::Failed, rateError);
                } else {
                    resetToEmpty();
                    state_.sessionState = domain::SessionState::kInvalid;
                    state_.playbackState = domain::PlaybackState::kPaused;
                    state_.displayedFrame.reset();
                    state_.requestedFrame.reset();
                    state_.canonicalFrameCount = 0U;
                    state_.lastError = rateError;
                    publishSnapshot();
                    completeCommand(completed.command, CommandOutcome::Failed, rateError);
                }
                return;
            }
            activeTimeline = domain::CanonicalTimeline{*set.canonicalRate()};
        }

        beginOpenValidated(completed.command,
                           std::move(validation.value().set),
                           std::move(validation.value().report),
                           std::move(*activeTimeline));
    }

    void beginOpenPaths(const OpenComparisonCommand& command) {
        if (command.sources.empty()) {
            rejectCommand(command.context,
                          CommandOutcome::Failed,
                          probeCoordinatorError(domain::MediaErrorCode::kInvalidArgument,
                                                std::nullopt,
                                                "At least two source paths are required.",
                                                false));
            return;
        }
        for (const auto& source : command.sources) {
            if (source.path.empty()) {
                rejectCommand(command.context,
                              CommandOutcome::Failed,
                              probeCoordinatorError(domain::MediaErrorCode::kInvalidArgument,
                                                    std::nullopt,
                                                    "All source paths are required.",
                                                    false));
                return;
            }
        }

        const bool preservesReadySession = state_.sessionState == domain::SessionState::kReady &&
                                           sources_.has_value() &&
                                           state_.displayedFrame.has_value();

        // Build one probe slot per submitted source. Source ids are assigned in submission order
        // (0, 1, 2). Each slot gets its own request context for cancellation identity.
        std::vector<PendingProbeSlot> slots;
        slots.reserve(command.sources.size());
        for (std::size_t index = 0; index < command.sources.size(); ++index) {
            slots.push_back(PendingProbeSlot{
                .context = makeRequestContext(),
                .sourceId = static_cast<domain::SourceId>(index),
                .sourcePath = command.sources[index].path,
                .role = command.sources[index].role,
                .displayName = command.sources[index].displayName,
            });
        }

        PendingProbe pending{
            .command = command.context,
            .slots = std::move(slots),
            .preservesReadySession = preservesReadySession,
        };

        // Same-path dedup: identical paths among the submitted sources share one probe whose
        // descriptor fills every slot with that path. Only the first slot for each unique path
        // has a probe submitted; shared slots are filled when the primary's probe completes.
        std::vector<std::size_t> primaryIndices;
        for (std::size_t index = 0; index < pending.slots.size(); ++index) {
            bool isDuplicate = false;
            for (const std::size_t primary : primaryIndices) {
                if (pending.slots[primary].sourcePath == pending.slots[index].sourcePath) {
                    isDuplicate = true;
                    break;
                }
            }
            if (!isDuplicate) {
                primaryIndices.push_back(index);
            }
        }

        pendingProbe_ = std::move(pending);

        if (!preservesReadySession) {
            resetToEmpty();
            state_.sessionState = domain::SessionState::kLoading;
            publishSnapshot();
        } else {
            state_.lastError.reset();
            publishSnapshot();
        }

        for (const std::size_t primaryIndex : primaryIndices) {
            PendingProbeSlot& primary = pendingProbe_->slots[primaryIndex];
            const MediaProbeRequest request{
                .context = primary.context,
                .sourceId = primary.sourceId,
                .sourcePath = primary.sourcePath,
            };
            const PortSubmitResult accepted =
                dependencies_.mediaProbe->submit(request, eventSink_);
            if (accepted != PortSubmitResult::Accepted) {
                failProbe(probeCoordinatorError(domain::MediaErrorCode::kMediaProbeFailed,
                                                primary.sourceId,
                                                "Source probing was not accepted."),
                          accepted == PortSubmitResult::Busy ? CommandOutcome::Busy
                                                             : CommandOutcome::Closed,
                          domain::SessionState::kError);
                return;
            }
        }
    }

    void beginSeek(const CommandContext& command, const domain::FrameId frameId) {
        if (!sources_.has_value() || state_.sessionState != domain::SessionState::kReady) {
            rejectCommand(command,
                          CommandOutcome::Failed,
                          coordinatorError(domain::MediaErrorCode::kInvalidArgument,
                                           "A frame seek requires a ready comparison set.",
                                           false));
            return;
        }
        if (!frameId.isValid() ||
            static_cast<std::uint64_t>(frameId.value()) >= state_.canonicalFrameCount) {
            rejectCommand(command,
                          CommandOutcome::Failed,
                          coordinatorError(domain::MediaErrorCode::kInvalidFrameId,
                                           "The requested frame is outside the canonical timeline.",
                                           false));
            return;
        }

        dependencies_.directFrameProvider->cancel(currentPlaybackScope());
        state_.playbackGeneration = increment(state_.playbackGeneration);
        pending_ = PendingCommand{
            .phase = PendingPhase::kSeekingFrame,
            .command = command,
            .providerContext = currentPlaybackScope(),
            .frameContext = std::nullopt,
            .set = std::nullopt,
        };
        submitFirstOrSeekFrame(frameId, PendingPhase::kSeekingFrame);
    }

    void beginStep(const StepFramesCommand& command) {
        if (command.delta == 0) {
            rejectCommand(command.context,
                          CommandOutcome::Failed,
                          coordinatorError(domain::MediaErrorCode::kInvalidArgument,
                                           "A frame step delta must not be zero.",
                                           false));
            return;
        }
        if (!sources_.has_value() || state_.canonicalFrameCount == 0U) {
            beginSeek(command.context, domain::FrameId{0});
            return;
        }

        const std::int64_t maximum = static_cast<std::int64_t>(state_.canonicalFrameCount - 1U);
        const std::int64_t current = state_.displayedFrame.value_or(domain::FrameId{0}).value();
        const std::int64_t target =
            command.delta > 0
                ? (command.delta > maximum - current ? maximum : current + command.delta)
                : (command.delta < -current ? 0 : current + command.delta);
        beginSeek(command.context, domain::FrameId{target});
    }

    void beginClose(const CloseSessionCommand& command) {
        if (!sources_.has_value()) {
            resetToEmpty();
            publishSnapshot();
            completeCommand(command.context, CommandOutcome::Succeeded);
            return;
        }

        const PlaybackRequestContext providerContext = currentPlaybackScope();
        dependencies_.renderChannel->clear(providerContext);
        state_.sessionEpoch = increment(state_.sessionEpoch);
        state_.playbackGeneration = increment(state_.playbackGeneration);
        state_.sessionState = domain::SessionState::kLoading;
        state_.playbackState = domain::PlaybackState::kPaused;
        state_.requestedFrame.reset();
        state_.displayedFrame.reset();
        pending_ = PendingCommand{
            .phase = PendingPhase::kClosingProvider,
            .command = command.context,
            .providerContext = providerContext,
            .frameContext = std::nullopt,
            .set = std::nullopt,
        };
        publishSnapshot();

        const FrameProviderCloseRequest request{.context = providerContext};
        if (dependencies_.directFrameProvider->submit(request, eventSink_) !=
            PortSubmitResult::Accepted) {
            failPending(
                coordinatorError(domain::MediaErrorCode::kMediaDecodeFailed,
                                 "The direct frame provider did not accept the close request."),
                CommandOutcome::Busy);
        }
    }

    void handleCommand(PlaybackCommand command) {
        const CommandContext& context = commandContext(command);
        if (!claimCommand(context)) {
            return;
        }
        if (!acceptsCommand(context)) {
            completeCommand(context,
                            CommandOutcome::Canceled,
                            coordinatorError(domain::MediaErrorCode::kInvalidArgument,
                                             "The command belongs to an obsolete session epoch.",
                                             true));
            return;
        }
        if (std::holds_alternative<PauseCommand>(command)) {
            if (pending_.has_value() || pendingProbe_.has_value()) {
                completeCommand(context, CommandOutcome::Busy);
            } else {
                beginPause(std::get<PauseCommand>(command));
            }
            return;
        }
        const bool isOpenCommand = std::holds_alternative<OpenComparisonCommand>(command) ||
                                   std::holds_alternative<OpenDirectComparisonCommand>(command);
        if (isOpenCommand && !pending_.has_value() && !playbackRun_.has_value() &&
            pendingProbe_.has_value()) {
            // A new open supersedes in-flight probes instead of bouncing off a Busy gate.
            supersedePendingProbes();
        }
        if (pending_.has_value() || pendingProbe_.has_value() || playbackRun_.has_value()) {
            completeCommand(context, CommandOutcome::Busy);
            return;
        }

        std::visit(
            [this](const auto& value) {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, OpenDirectComparisonCommand>) {
                    beginOpenDirect(value);
                } else if constexpr (std::is_same_v<Value, OpenComparisonCommand>) {
                    beginOpenPaths(value);
                } else if constexpr (std::is_same_v<Value, SeekFrameCommand>) {
                    beginSeek(value.context, value.frameId);
                } else if constexpr (std::is_same_v<Value, StepFramesCommand>) {
                    beginStep(value);
                } else if constexpr (std::is_same_v<Value, FirstFrameCommand>) {
                    beginSeek(value.context, domain::FrameId{0});
                } else if constexpr (std::is_same_v<Value, LastFrameCommand>) {
                    if (state_.canonicalFrameCount == 0U) {
                        rejectCommand(
                            value.context,
                            CommandOutcome::Failed,
                            coordinatorError(domain::MediaErrorCode::kInvalidFrameId,
                                             "The comparison set has no canonical final frame.",
                                             false));
                    } else {
                        beginSeek(value.context,
                                  domain::FrameId{
                                      static_cast<std::int64_t>(state_.canonicalFrameCount - 1U)});
                    }
                } else if constexpr (std::is_same_v<Value, PlayCommand>) {
                    beginPlay(value);
                } else if constexpr (std::is_same_v<Value, PauseCommand>) {
                    // Pause is handled before the general active-operation admission gate.
                } else if constexpr (std::is_same_v<Value, CloseSessionCommand>) {
                    beginClose(value);
                }
            },
            std::move(command));
    }

    [[nodiscard]] bool matchesPending(const EventContext& context) const noexcept {
        if (!pending_.has_value()) {
            return false;
        }
        if (pending_->phase == PendingPhase::kOpeningProvider ||
            pending_->phase == PendingPhase::kClosingProvider) {
            return matchesContext(context, pending_->providerContext);
        }
        return pending_->frameContext.has_value() &&
               matchesContext(context, *pending_->frameContext);
    }

    void succeedPending() {
        if (!pending_.has_value()) {
            return;
        }
        const PendingPhase phase = pending_->phase;
        if (phase == PendingPhase::kOpeningProvider) {
            submitFirstOrSeekFrame(domain::FrameId{0}, PendingPhase::kOpeningFirstFrame);
            return;
        }
        if (phase == PendingPhase::kOpeningFirstFrame || phase == PendingPhase::kSeekingFrame) {
            pending_->providerSucceeded = true;
            commitPresentedFrameIfComplete();
            return;
        }

        const CommandContext command = pending_->command;
        pending_.reset();
        resetToEmpty();
        publishSnapshot();
        completeCommand(command, CommandOutcome::Succeeded);
    }

    void commitPresentedFrameIfComplete() {
        if (!pending_.has_value() || !pending_->set.has_value() || !pending_->framePublished ||
            !pending_->providerSucceeded || !pending_->framePresented ||
            !pending_->presentationTimerId.has_value()) {
            return;
        }

        static_cast<void>(dependencies_.deadlineScheduler->cancel(*pending_->presentationTimerId));
        const CommandContext command = pending_->command;
        const domain::FrameId displayedFrame = pending_->set->canonicalFrameId();
        pending_.reset();
        state_.sessionState = domain::SessionState::kReady;
        state_.playbackState = domain::PlaybackState::kPaused;
        state_.displayedFrame = displayedFrame;
        state_.requestedFrame.reset();
        state_.lastError.reset();
        publishSnapshot();
        completeCommand(command, CommandOutcome::Succeeded);
    }

    [[nodiscard]] bool matchesPlaybackFrame(const EventContext& context) const noexcept {
        return playbackRun_.has_value() && playbackRun_->frame.has_value() &&
               matchesContext(context, playbackRun_->frame->context);
    }

    void commitPlaybackFrameIfComplete() {
        if (!playbackRun_.has_value() || !playbackRun_->frame.has_value()) {
            return;
        }
        PendingPlaybackFrame& frame = *playbackRun_->frame;
        if (!frame.set.has_value() || !frame.framePublished || !frame.providerSucceeded ||
            !frame.framePresented || !frame.presentationTimerId.has_value()) {
            return;
        }

        static_cast<void>(dependencies_.deadlineScheduler->cancel(*frame.presentationTimerId));
        const domain::FrameId displayedFrame = frame.expectedFrame;
        const bool pauseRequested = playbackRun_->pauseRequested;
        const bool reachedEnd =
            static_cast<std::uint64_t>(displayedFrame.value()) + 1U >= state_.canonicalFrameCount;
        playbackRun_->frame.reset();
        state_.displayedFrame = displayedFrame;
        state_.requestedFrame.reset();
        state_.lastError.reset();

        if (pauseRequested || reachedEnd) {
            playbackRun_.reset();
            state_.playbackState = domain::PlaybackState::kPaused;
            publishSnapshot();
            return;
        }

        if (playbackRun_->restartFromEnd) {
            // frame 0 of a restart-from-end just committed: re-anchor on the absolute canonical
            // timeline at frame 0 and continue from frame 1.
            playbackRun_->restartFromEnd = false;
            playbackRun_->anchorFrame = domain::FrameId{0};
            playbackRun_->firstTarget = domain::FrameId{1};
            playbackRun_->nextMinimum = domain::FrameId{1};
            playbackRun_->wallAnchor = dependencies_.clock->now();
        } else {
            playbackRun_->nextMinimum = domain::FrameId{displayedFrame.value() + 1};
        }
        static_cast<void>(schedulePlaybackTarget(playbackTargetAt(dependencies_.clock->now())));
    }

    [[nodiscard]] bool handlePlaybackTerminal(const RequestTerminal& terminal) {
        const EventContext& terminalContext = std::visit(
            [](const auto& value) -> const EventContext& { return value.context; }, terminal);
        if (!matchesPlaybackFrame(terminalContext)) {
            return false;
        }
        if (std::holds_alternative<RequestSucceeded>(terminal)) {
            playbackRun_->frame->providerSucceeded = true;
            commitPlaybackFrameIfComplete();
            return true;
        }
        if (const auto* const failed = std::get_if<RequestFailed>(&terminal)) {
            stopPlayback(failed->error);
            return true;
        }
        stopPlayback(coordinatorError(domain::MediaErrorCode::kMediaDecodeFailed,
                                      "The active sequential playback request was canceled."));
        return true;
    }

    void handleTerminal(const RequestTerminal& terminal) {
        const EventContext& terminalContext = std::visit(
            [](const auto& value) -> const EventContext& { return value.context; }, terminal);
        if (const auto* const requestContext = std::get_if<RequestContext>(&terminalContext)) {
            if (PendingProbeSlot* const slot = probeSlot(*requestContext); slot != nullptr) {
                if (std::holds_alternative<RequestSucceeded>(terminal)) {
                    slot->succeeded = true;
                    // With same-path dedup, also mark shared slots (same path) as succeeded so
                    // that finishProbeIfComplete sees every slot ready.
                    for (auto& other : pendingProbe_->slots) {
                        if (&other != slot && other.sourcePath == slot->sourcePath) {
                            other.succeeded = true;
                        }
                    }
                    finishProbeIfComplete();
                    return;
                }
                if (const auto* const failed = std::get_if<RequestFailed>(&terminal)) {
                    failProbe(failed->error, CommandOutcome::Failed, domain::SessionState::kError);
                    return;
                }
                const auto& canceled = std::get<RequestCanceled>(terminal);
                failProbe(probeCoordinatorError(domain::MediaErrorCode::kMediaProbeFailed,
                                                slot->sourceId,
                                                "Media probing was canceled."),
                          canceled.reason == CancellationReason::Shutdown
                              ? CommandOutcome::Closed
                              : CommandOutcome::Canceled,
                          domain::SessionState::kError);
                return;
            }
        }

        if (handlePlaybackTerminal(terminal)) {
            return;
        }

        const auto matches = std::visit(
            [this](const auto& value) { return matchesPending(value.context); }, terminal);
        if (!matches) {
            return;
        }

        if (std::holds_alternative<RequestSucceeded>(terminal)) {
            succeedPending();
            return;
        }
        if (const auto* const failed = std::get_if<RequestFailed>(&terminal)) {
            failPending(failed->error, CommandOutcome::Failed);
            return;
        }
        failPending(coordinatorError(domain::MediaErrorCode::kMediaDecodeFailed,
                                     "The direct frame provider canceled the active operation."),
                    CommandOutcome::Canceled);
    }

    void handleProbeCompleted(ProbeCompleted completed) {
        PendingProbeSlot* const slot = probeSlot(completed.context);
        if (slot == nullptr) {
            return;
        }
        if (completed.sourceId != slot->sourceId) {
            failProbe(probeCoordinatorError(domain::MediaErrorCode::kMediaProbeFailed,
                                            slot->sourceId,
                                            "A probe published a descriptor for the wrong source."),
                      CommandOutcome::Failed,
                      domain::SessionState::kError);
            return;
        }
        // The first ProbeCompleted for a slot owns both its descriptor and its timeline. A
        // duplicate ProbeCompleted for the same source that arrives before its separate terminal
        // is ignored wholesale, so it can neither overwrite the descriptor nor fill a
        // previously-null CFR timeline. Stale events are rejected by probeSlot() above.
        if (slot->descriptor.has_value()) {
            return;
        }
        slot->descriptor = std::move(completed.descriptor);
        slot->timeline = std::move(completed.timeline);
        if (const auto* const descriptor = slot->descriptor ? &*slot->descriptor : nullptr;
            descriptor != nullptr) {
            const bool isVfr =
                descriptor->timingConfidence == domain::TimingConfidence::kVariableFrameRate;
            if (isVfr) {
                // A VFR source must publish a runtime timeline whose shared_ptr is non-null before
                // it can be dereferenced; an engaged optional may still hold a null pointer.
                if (!slot->timeline.has_value() || !*slot->timeline) {
                    failProbe(
                        probeCoordinatorError(domain::MediaErrorCode::kMediaProbeFailed,
                                              slot->sourceId,
                                              "A VFR source must publish a runtime timeline."),
                        CommandOutcome::Failed,
                        domain::SessionState::kError);
                    return;
                }
                if ((*slot->timeline)->frameCount() != descriptor->frameCount.value) {
                    failProbe(probeCoordinatorError(
                                  domain::MediaErrorCode::kMediaProbeFailed,
                                  slot->sourceId,
                                  "The VFR timeline length does not match the source frame count."),
                              CommandOutcome::Failed,
                              domain::SessionState::kError);
                    return;
                }
            } else if (slot->timeline.has_value()) {
                failProbe(
                    probeCoordinatorError(domain::MediaErrorCode::kMediaProbeFailed,
                                          slot->sourceId,
                                          "A CFR source must not publish a runtime timeline."),
                    CommandOutcome::Failed,
                    domain::SessionState::kError);
                return;
            }
        }

        // Same-path dedup: copy the validated descriptor and timeline to every other slot that
        // shares this probe's source path, so finishProbeIfComplete sees all slots filled after
        // a single probe's payload and terminal.
        const std::filesystem::path primaryPath = slot->sourcePath;
        for (auto& other : pendingProbe_->slots) {
            if (&other != slot && other.sourcePath == primaryPath &&
                !other.descriptor.has_value()) {
                other.descriptor = slot->descriptor;
                other.timeline = slot->timeline;
            }
        }

        finishProbeIfComplete();
    }

    void handleFrameSet(const FrameSetReady& ready) {
        if (playbackRun_.has_value() && playbackRun_->frame.has_value() &&
            ready.context == playbackRun_->frame->context) {
            PendingPlaybackFrame& frame = *playbackRun_->frame;
            if (ready.set.canonicalFrameId() != frame.expectedFrame) {
                stopPlayback(coordinatorError(
                    domain::MediaErrorCode::kMediaDecodeFailed,
                    "The provider published a mismatched sequential playback frame set."));
                return;
            }
            if (frame.framePublished) {
                return;
            }
            if (dependencies_.renderChannel->publish(ready.context, ready.set) ==
                RenderPublishResult::Closed) {
                stopPlayback(
                    coordinatorError(domain::MediaErrorCode::kMediaDecodeFailed,
                                     "The render channel closed during sequential playback."));
                return;
            }
            frame.set = ready.set;
            frame.framePublished = true;
            commitPlaybackFrameIfComplete();
            return;
        }
        if (!pending_.has_value() || !pending_->frameContext.has_value() ||
            ready.context != *pending_->frameContext) {
            return;
        }
        if (!pending_->expectedFrame.has_value() ||
            ready.set.canonicalFrameId() != *pending_->expectedFrame) {
            failPending(
                coordinatorError(domain::MediaErrorCode::kMediaDecodeFailed,
                                 "The provider published a different exact frame than requested."),
                CommandOutcome::Failed);
            return;
        }
        if (pending_->framePublished) {
            return;
        }
        if (dependencies_.renderChannel->publish(ready.context, ready.set) ==
            RenderPublishResult::Closed) {
            failPending(
                coordinatorError(
                    domain::MediaErrorCode::kMediaDecodeFailed,
                    "The render channel closed before it accepted the complete frame set."),
                CommandOutcome::Canceled);
            return;
        }
        pending_->set = ready.set;
        pending_->framePublished = true;
        commitPresentedFrameIfComplete();
    }

    void handleFramePresented(const FrameSetPresented& presented) {
        if (playbackRun_.has_value() && playbackRun_->frame.has_value() &&
            playbackRun_->frame->framePublished &&
            presented.context == playbackRun_->frame->context &&
            presented.frameId == playbackRun_->frame->expectedFrame) {
            playbackRun_->frame->framePresented = true;
            commitPlaybackFrameIfComplete();
            return;
        }
        if (!pending_.has_value() || !pending_->frameContext.has_value() ||
            !pending_->expectedFrame.has_value() || !pending_->framePublished ||
            presented.context != *pending_->frameContext ||
            presented.frameId != *pending_->expectedFrame) {
            return;
        }
        pending_->framePresented = true;
        commitPresentedFrameIfComplete();
    }

    void handleDeadline(const DeadlineElapsed& elapsed) {
        if (playbackRun_.has_value() && playbackRun_->cadenceTimerId.has_value() &&
            playbackRun_->cadenceTarget.has_value() &&
            elapsed.context == playbackRun_->cadenceContext &&
            elapsed.timerId == *playbackRun_->cadenceTimerId) {
            const domain::FrameId scheduledTarget = *playbackRun_->cadenceTarget;
            playbackRun_->cadenceTimerId.reset();
            playbackRun_->cadenceTarget.reset();
            const domain::FrameId dueTarget =
                playbackRun_->restartFromEnd
                    ? scheduledTarget
                    : std::max(scheduledTarget, playbackTargetAt(dependencies_.clock->now()));
            static_cast<void>(schedulePlaybackTarget(dueTarget));
            return;
        }
        if (playbackRun_.has_value() && playbackRun_->frame.has_value() &&
            playbackRun_->frame->presentationTimerId.has_value() &&
            elapsed.context == playbackRun_->frame->context.playback &&
            elapsed.timerId == *playbackRun_->frame->presentationTimerId) {
            stopPlayback(
                presentationError("The playback frame was not presented within five seconds."));
            return;
        }
        if (!pending_.has_value() || !pending_->frameContext.has_value() ||
            !pending_->presentationTimerId.has_value() ||
            elapsed.context != pending_->frameContext->playback ||
            elapsed.timerId != *pending_->presentationTimerId) {
            return;
        }
        failPending(presentationError("The exact frame was not presented within five seconds."),
                    CommandOutcome::Failed);
    }

    void handleGraphicsReady(const GraphicsDeviceReady& ready) {
        if (ready.context.deviceGeneration < state_.deviceGeneration) {
            return;
        }
        state_.deviceGeneration = ready.context.deviceGeneration;
        state_.graphicsReady = true;
        if (state_.lastError.has_value() &&
            (state_.lastError->code == domain::MediaErrorCode::kGraphicsUnavailable ||
             state_.lastError->code == domain::MediaErrorCode::kGraphicsDeviceLost)) {
            state_.lastError.reset();
        }
        publishSnapshot();
    }

    void handleGraphicsFailure(const GraphicsEventContext& context,
                               const domain::MediaError& error) {
        if (context.deviceGeneration < state_.deviceGeneration) {
            return;
        }
        if (pendingProbe_.has_value()) {
            failProbe(error, CommandOutcome::Failed, domain::SessionState::kError);
        }
        if (pending_.has_value()) {
            failPending(error, CommandOutcome::Failed);
        }
        if (playbackRun_.has_value()) {
            stopPlayback(error, false);
        }
        state_.deviceGeneration = context.deviceGeneration;
        state_.graphicsReady = false;
        state_.playbackState = domain::PlaybackState::kPaused;
        state_.lastError = error;
        publishSnapshot();
    }

    void handleEvent(ApplicationEvent event) {
        std::visit(
            [this](const auto& value) {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, RequestTerminal>) {
                    handleTerminal(value);
                } else if constexpr (std::is_same_v<Value, ProbeCompleted>) {
                    handleProbeCompleted(value);
                } else if constexpr (std::is_same_v<Value, FrameSetReady>) {
                    handleFrameSet(value);
                } else if constexpr (std::is_same_v<Value, FrameSetPresented>) {
                    handleFramePresented(value);
                } else if constexpr (std::is_same_v<Value, DeadlineElapsed>) {
                    handleDeadline(value);
                } else if constexpr (std::is_same_v<Value, GraphicsDeviceReady>) {
                    handleGraphicsReady(value);
                } else if constexpr (std::is_same_v<Value, GraphicsDeviceUnavailable> ||
                                     std::is_same_v<Value, GraphicsDeviceLost>) {
                    handleGraphicsFailure(value.context, value.error);
                }
            },
            std::move(event));
    }

    [[nodiscard]] bool hasWorkLocked() const noexcept {
        return !criticalEvents_.empty() || !commands_.empty() || realtimeEvent_.has_value();
    }

    [[nodiscard]] WorkItem takeWorkLocked() {
        if (!criticalEvents_.empty()) {
            ApplicationEvent event = std::move(criticalEvents_.front());
            criticalEvents_.pop_front();
            return WorkItem{std::in_place_index<1>, std::move(event)};
        }
        if (!commands_.empty()) {
            PlaybackCommand command = std::move(commands_.front());
            commands_.pop_front();
            return WorkItem{std::in_place_index<0>, std::move(command)};
        }
        ApplicationEvent event = std::move(*realtimeEvent_);
        realtimeEvent_.reset();
        return WorkItem{std::in_place_index<1>, std::move(event)};
    }

    void run() noexcept {
        for (;;) {
            std::optional<WorkItem> work;
            {
                std::unique_lock lock(ingressMutex_);
                condition_.wait(lock, [this] { return shuttingDown_ || hasWorkLocked(); });
                if (!hasWorkLocked()) {
                    if (shuttingDown_) {
                        return;
                    }
                    continue;
                }
                work.emplace(takeWorkLocked());
            }
            condition_.notify_all();
            if (std::holds_alternative<PlaybackCommand>(*work)) {
                handleCommand(std::move(std::get<PlaybackCommand>(*work)));
            } else {
                handleEvent(std::move(std::get<ApplicationEvent>(*work)));
            }
        }
    }

    void shutdownImpl() noexcept {
        {
            std::scoped_lock lock(ingressMutex_);
            if (shuttingDown_) {
                return;
            }
            shuttingDown_ = true;
            criticalIngressClosed_ = true;
            realtimeIngressClosed_ = true;
            realtimeEvent_.reset();
        }
        condition_.notify_all();
        eventSink_->detach();
        if (worker_.joinable()) {
            worker_.join();
        }
        if (pendingProbe_.has_value()) {
            for (const auto& slot : pendingProbe_->slots) {
                dependencies_.mediaProbe->cancel(slot.context);
            }
        }
        if (pending_.has_value()) {
            if (pending_->presentationTimerId.has_value()) {
                static_cast<void>(
                    dependencies_.deadlineScheduler->cancel(*pending_->presentationTimerId));
            }
            dependencies_.directFrameProvider->cancel(pending_->providerContext);
            if ((pending_->phase == PendingPhase::kOpeningFirstFrame ||
                 pending_->phase == PendingPhase::kSeekingFrame) &&
                pending_->frameContext.has_value()) {
                dependencies_.renderChannel->clear(pending_->providerContext);
            }
        }
        if (playbackRun_.has_value()) {
            stopPlayback(std::nullopt, false);
        }
    }

    Dependencies dependencies_;
    std::shared_ptr<CoordinatorEventSinkGate> eventSink_;
    SessionSnapshot state_;
    std::optional<domain::ValidatedComparisonSet> sources_;
    std::optional<domain::CompatibilityReport> compatibilityReport_;
    std::optional<domain::CanonicalTimeline> canonicalTimeline_;
    std::optional<PendingCommand> pending_;
    std::optional<PendingProbe> pendingProbe_;
    std::optional<PlaybackRun> playbackRun_;
    std::uint64_t nextRequestId_ = 1U;
    std::uint64_t nextTimerId_ = 1U;
    std::unordered_set<CommandIdentity, CommandIdentityHash> seenCommands_;

    mutable std::mutex publicationMutex_;
    std::shared_ptr<const SessionSnapshot> publishedSnapshot_;
    std::vector<CommandTerminal> completedCommands_;

    std::mutex ingressMutex_;
    std::condition_variable condition_;
    std::deque<PlaybackCommand> commands_;
    std::deque<ApplicationEvent> criticalEvents_;
    std::optional<ApplicationEvent> realtimeEvent_;
    bool criticalIngressClosed_ = false;
    bool realtimeIngressClosed_ = false;
    bool shuttingDown_ = false;
    std::thread worker_;
};

std::shared_ptr<PlaybackCoordinator> PlaybackCoordinator::create(const domain::SessionId sessionId,
                                                                 Dependencies dependencies) {
    if (!dependencies.mediaProbe || !dependencies.directFrameProvider ||
        !dependencies.deadlineScheduler || !dependencies.clock || !dependencies.renderChannel) {
        return {};
    }

    return std::shared_ptr<PlaybackCoordinator>(
        new PlaybackCoordinator(sessionId, std::move(dependencies)));
}

PlaybackCoordinator::PlaybackCoordinator(const domain::SessionId sessionId,
                                         Dependencies dependencies)
    : impl_(std::make_unique<Impl>(sessionId, std::move(dependencies))) {}

PlaybackCoordinator::~PlaybackCoordinator() = default;

PortSubmitResult PlaybackCoordinator::submit(PlaybackCommand command) {
    return impl_->submit(std::move(command));
}

std::shared_ptr<const SessionSnapshot> PlaybackCoordinator::snapshot() const {
    return impl_->snapshot();
}

std::vector<CommandTerminal> PlaybackCoordinator::takeCompletedCommands() {
    return impl_->takeCompletedCommands();
}

std::shared_ptr<IApplicationEventSink> PlaybackCoordinator::eventSink() const noexcept {
    return impl_->eventSink();
}

void PlaybackCoordinator::shutdown() noexcept {
    impl_->shutdown();
}

EventPostResult PlaybackCoordinator::postCritical(ApplicationEvent event) noexcept {
    return impl_->postCritical(std::move(event));
}

EventPostResult PlaybackCoordinator::postRealtime(ApplicationEvent event) noexcept {
    return impl_->postRealtime(std::move(event));
}

void PlaybackCoordinator::closeRealtimeIngress() noexcept {
    impl_->closeRealtimeIngress();
}

void PlaybackCoordinator::closeCriticalIngress() noexcept {
    impl_->closeCriticalIngress();
}

} // namespace dvs::application
