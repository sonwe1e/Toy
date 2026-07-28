#include "dvs/media/MultiSourceFrameProvider.h"

#include "dvs/domain/FrameTimeline.h"
#include "dvs/platform/FrameBudget.h"

#include "SoftwareDecoder.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace dvs::media {
namespace {

inline constexpr std::size_t kExactRequestSlots = 1U;
inline constexpr std::size_t kSequentialRequestSlots = 2U;
inline constexpr std::size_t kPrefetchRequestSlots = 8U;
inline constexpr std::size_t kSetTableCapacity = 16U;

enum class ProviderOperationKind {
    kOpen,
    kFrame,
    kClose,
};

enum class ProviderOperationLifecycle {
    kPending,
    kCanceled,
    kTerminalClaimed,
};

struct ProviderOperation final {
    ProviderOperationKind kind;
    std::variant<application::FrameProviderOpenRequest,
                 application::FrameRequest,
                 application::FrameProviderCloseRequest>
        request;
    std::weak_ptr<application::IApplicationEventSink> events;
    std::atomic<ProviderOperationLifecycle> lifecycle = ProviderOperationLifecycle::kPending;
    std::atomic<application::CancellationReason> cancellationReason =
        application::CancellationReason::Superseded;
    std::atomic<bool> cancellationRequested = false;

    ProviderOperation(const ProviderOperationKind kindValue,
                      std::variant<application::FrameProviderOpenRequest,
                                   application::FrameRequest,
                                   application::FrameProviderCloseRequest> requestValue,
                      std::weak_ptr<application::IApplicationEventSink> eventsValue)
        : kind(kindValue), request(std::move(requestValue)), events(std::move(eventsValue)) {}

    [[nodiscard]] bool requestCancellation(const application::CancellationReason reason) noexcept {
        cancellationReason.store(reason, std::memory_order_release);
        ProviderOperationLifecycle expected = ProviderOperationLifecycle::kPending;
        if (!lifecycle.compare_exchange_strong(expected,
                                               ProviderOperationLifecycle::kCanceled,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire)) {
            return false;
        }
        cancellationRequested.store(true, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool isCanceled() const noexcept {
        return lifecycle.load(std::memory_order_acquire) == ProviderOperationLifecycle::kCanceled;
    }

    [[nodiscard]] bool claimTerminal() noexcept {
        ProviderOperationLifecycle expected = ProviderOperationLifecycle::kPending;
        return lifecycle.compare_exchange_strong(expected,
                                                 ProviderOperationLifecycle::kTerminalClaimed,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire);
    }

    [[nodiscard]] bool claimCanceledTerminal() noexcept {
        ProviderOperationLifecycle expected = ProviderOperationLifecycle::kCanceled;
        return lifecycle.compare_exchange_strong(expected,
                                                 ProviderOperationLifecycle::kTerminalClaimed,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire);
    }
};

[[nodiscard]] domain::MediaError providerError(const domain::MediaErrorCode code,
                                               std::optional<domain::SourceId> sourceId,
                                               std::string technicalDetail,
                                               const bool recoverable = false) {
    return domain::makeMediaError(code,
                                  domain::MediaOperation::kMediaDecode,
                                  sourceId,
                                  recoverable,
                                  std::move(technicalDetail));
}

[[nodiscard]] const application::PlaybackRequestContext&
playbackContext(const ProviderOperation& operation) {
    switch (operation.kind) {
    case ProviderOperationKind::kOpen:
        return std::get<application::FrameProviderOpenRequest>(operation.request).context;
    case ProviderOperationKind::kFrame:
        return std::get<application::FrameRequest>(operation.request).context.playback;
    case ProviderOperationKind::kClose:
        return std::get<application::FrameProviderCloseRequest>(operation.request).context;
    }
    std::terminate();
}

[[nodiscard]] bool sameSessionEpoch(const application::PlaybackRequestContext& left,
                                    const application::PlaybackRequestContext& right) noexcept {
    return left.request.sessionId == right.request.sessionId &&
           left.request.sessionEpoch == right.request.sessionEpoch;
}

[[nodiscard]] bool samePlaybackScope(const application::PlaybackRequestContext& left,
                                     const application::PlaybackRequestContext& right) noexcept {
    return sameSessionEpoch(left, right) && left.playbackGeneration == right.playbackGeneration;
}

[[nodiscard]] application::EventContext eventContext(const ProviderOperation& operation) {
    if (operation.kind == ProviderOperationKind::kFrame) {
        return application::EventContext{
            std::get<application::FrameRequest>(operation.request).context};
    }
    return application::EventContext{playbackContext(operation)};
}

[[nodiscard]] domain::RequestId requestId(const ProviderOperation& operation) noexcept {
    return playbackContext(operation).request.requestId;
}

void postCritical(const std::weak_ptr<application::IApplicationEventSink>& events,
                  application::ApplicationEvent event) noexcept {
    if (const std::shared_ptr<application::IApplicationEventSink> sink = events.lock()) {
        static_cast<void>(sink->postCritical(std::move(event)));
    }
}

void postCanceled(const std::shared_ptr<ProviderOperation>& operation) noexcept {
    if (!operation->claimCanceledTerminal()) {
        return;
    }
    postCritical(operation->events,
                 application::ApplicationEvent{application::RequestCanceled{
                     .context = eventContext(*operation),
                     .reason = operation->cancellationReason.load(std::memory_order_acquire),
                 }});
}

void postFailed(const std::shared_ptr<ProviderOperation>& operation,
                domain::MediaError error) noexcept {
    if (!operation->claimTerminal()) {
        postCanceled(operation);
        return;
    }
    error.requestId = requestId(*operation);
    postCritical(operation->events,
                 application::ApplicationEvent{application::RequestFailed{
                     .context = eventContext(*operation),
                     .error = std::move(error),
                 }});
}

void postSucceeded(const std::shared_ptr<ProviderOperation>& operation) noexcept {
    if (!operation->claimTerminal()) {
        postCanceled(operation);
        return;
    }
    postCritical(operation->events,
                 application::ApplicationEvent{application::RequestSucceeded{
                     .context = eventContext(*operation),
                 }});
}

void postFrameSetSucceeded(const std::shared_ptr<ProviderOperation>& operation,
                           application::FrameSet set) noexcept {
    if (!operation->claimTerminal()) {
        postCanceled(operation);
        return;
    }
    const application::FrameRequestContext context =
        std::get<application::FrameRequest>(operation->request).context;
    postCritical(operation->events,
                 application::ApplicationEvent{application::FrameSetReady{
                     .context = context,
                     .set = std::move(set),
                 }});
    postCritical(operation->events,
                 application::ApplicationEvent{application::RequestSucceeded{
                     .context = application::EventContext{context},
                 }});
}

[[nodiscard]] std::uint64_t frameDistance(const domain::FrameId left,
                                          const domain::FrameId right) noexcept {
    if (!left.isValid() || !right.isValid()) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left.value() >= right.value() ? static_cast<std::uint64_t>(left.value() - right.value())
                                         : static_cast<std::uint64_t>(right.value() - left.value());
}

} // namespace

class MultiSourceFrameProvider::Impl final {
public:
    explicit Impl(platform::FrameBudget& frameBudget, const std::size_t requestCapacity)
        : frameBudget_(frameBudget), queueCapacity_(std::max(kSetTableCapacity, requestCapacity)),
          worker_([this] { run(); }) {}

    ~Impl() {
        shutdown();
    }

    [[nodiscard]] application::PortSubmitResult
    submit(const application::FrameProviderOpenRequest& request,
           const std::shared_ptr<application::IApplicationEventSink>& events) {
        if (!events) {
            return application::PortSubmitResult::Closed;
        }
        auto operation = std::make_shared<ProviderOperation>(
            ProviderOperationKind::kOpen,
            std::variant<application::FrameProviderOpenRequest,
                         application::FrameRequest,
                         application::FrameProviderCloseRequest>{request},
            std::weak_ptr<application::IApplicationEventSink>{events});
        std::vector<std::shared_ptr<ProviderOperation>> displaced;
        bool interrupt = false;
        {
            std::scoped_lock lock(mutex_);
            if (closed_) {
                return application::PortSubmitResult::Closed;
            }
            cancelQueuedLocked(application::CancellationReason::Superseded, &displaced);
            interrupt = cancelActiveLocked(application::CancellationReason::Superseded);
            controlQueue_.push_back(std::move(operation));
        }
        if (interrupt) {
            interruptRequested_.store(true, std::memory_order_release);
        }
        postCanceledAll(displaced);
        condition_.notify_one();
        return application::PortSubmitResult::Accepted;
    }

    [[nodiscard]] application::PortSubmitResult
    submit(const application::FrameRequest& request,
           const std::shared_ptr<application::IApplicationEventSink>& events) {
        if (!events) {
            return application::PortSubmitResult::Closed;
        }
        auto operation = std::make_shared<ProviderOperation>(
            ProviderOperationKind::kFrame,
            std::variant<application::FrameProviderOpenRequest,
                         application::FrameRequest,
                         application::FrameProviderCloseRequest>{request},
            std::weak_ptr<application::IApplicationEventSink>{events});
        std::vector<std::shared_ptr<ProviderOperation>> displaced;
        bool interrupt = false;
        bool busy = false;
        bool staleGeneration = false;
        {
            std::scoped_lock lock(mutex_);
            if (closed_) {
                return application::PortSubmitResult::Closed;
            }

            const application::PlaybackRequestContext& playback = request.context.playback;
            if (!latestFrameContext_.has_value() ||
                !sameSessionEpoch(*latestFrameContext_, playback)) {
                latestFrameContext_ = playback;
            } else if (playback.playbackGeneration.value() <
                       latestFrameContext_->playbackGeneration.value()) {
                staleGeneration = true;
                if (operation->requestCancellation(application::CancellationReason::Superseded)) {
                    displaced.push_back(operation);
                }
            } else if (playback.playbackGeneration != latestFrameContext_->playbackGeneration) {
                latestFrameContext_ = playback;
                cancelOutdatedFrameQueuesLocked(playback, &displaced);
                interrupt = cancelActiveOutdatedFrameLocked(playback);
            }

            if (staleGeneration) {
                // The operation has its one canceled terminal posted after the lock is released.
            } else {
                switch (request.priority) {
                case application::FrameRequestPriority::Exact:
                    latestExactFrame_ = request.frameId;
                    while (exactQueue_.size() >= kExactRequestSlots) {
                        cancelFrontLocked(
                            exactQueue_, application::CancellationReason::Superseded, &displaced);
                    }
                    interrupt = cancelActiveExactLocked() || interrupt;
                    exactQueue_.push_back(std::move(operation));
                    break;
                case application::FrameRequestPriority::Sequential:
                    while (sequentialQueue_.size() >= kSequentialRequestSlots) {
                        cancelFrontLocked(sequentialQueue_,
                                          application::CancellationReason::Superseded,
                                          &displaced);
                    }
                    sequentialQueue_.push_back(std::move(operation));
                    break;
                case application::FrameRequestPriority::Prefetch:
                    if (prefetchQueue_.size() >= kPrefetchRequestSlots) {
                        cancelFarthestPrefetchLocked(request.frameId, &displaced);
                    }
                    prefetchQueue_.push_back(std::move(operation));
                    break;
                }
            }

            if (!staleGeneration && queuedCountLocked() > queueCapacity_) {
                auto& queue = queueForPriority(request.priority);
                const std::shared_ptr<ProviderOperation> rejected = std::move(queue.back());
                queue.pop_back();
                if (rejected->requestCancellation(application::CancellationReason::Superseded)) {
                    displaced.push_back(rejected);
                }
                busy = true;
            }
        }
        if (staleGeneration) {
            postCanceledAll(displaced);
            return application::PortSubmitResult::Accepted;
        }
        if (busy) {
            if (interrupt) {
                interruptRequested_.store(true, std::memory_order_release);
            }
            postCanceledAll(displaced);
            return application::PortSubmitResult::Busy;
        }
        if (interrupt) {
            interruptRequested_.store(true, std::memory_order_release);
        }
        postCanceledAll(displaced);
        condition_.notify_one();
        return application::PortSubmitResult::Accepted;
    }

    [[nodiscard]] application::PortSubmitResult
    submit(const application::FrameProviderCloseRequest& request,
           const std::shared_ptr<application::IApplicationEventSink>& events) {
        if (!events) {
            return application::PortSubmitResult::Closed;
        }
        auto operation = std::make_shared<ProviderOperation>(
            ProviderOperationKind::kClose,
            std::variant<application::FrameProviderOpenRequest,
                         application::FrameRequest,
                         application::FrameProviderCloseRequest>{request},
            std::weak_ptr<application::IApplicationEventSink>{events});
        std::vector<std::shared_ptr<ProviderOperation>> displaced;
        bool interrupt = false;
        {
            std::scoped_lock lock(mutex_);
            if (closed_) {
                return application::PortSubmitResult::Closed;
            }
            cancelQueuedLocked(application::CancellationReason::Superseded, &displaced);
            interrupt = cancelActiveLocked(application::CancellationReason::Superseded);
            controlQueue_.push_back(std::move(operation));
        }
        if (interrupt) {
            interruptRequested_.store(true, std::memory_order_release);
        }
        postCanceledAll(displaced);
        condition_.notify_one();
        return application::PortSubmitResult::Accepted;
    }

    void cancel(const application::PlaybackRequestContext& context) noexcept {
        std::vector<std::shared_ptr<ProviderOperation>> displaced;
        bool interrupt = false;
        {
            std::scoped_lock lock(mutex_);
            cancelMatchingQueueLocked(
                controlQueue_, context, application::CancellationReason::UserRequested, &displaced);
            cancelMatchingQueueLocked(
                exactQueue_, context, application::CancellationReason::UserRequested, &displaced);
            cancelMatchingQueueLocked(sequentialQueue_,
                                      context,
                                      application::CancellationReason::UserRequested,
                                      &displaced);
            cancelMatchingQueueLocked(prefetchQueue_,
                                      context,
                                      application::CancellationReason::UserRequested,
                                      &displaced);
            if (activeOperation_ != nullptr &&
                samePlaybackScope(playbackContext(*activeOperation_), context)) {
                interrupt = activeOperation_->requestCancellation(
                    application::CancellationReason::UserRequested);
            }
        }
        if (interrupt) {
            interruptRequested_.store(true, std::memory_order_release);
        }
        postCanceledAll(displaced);
        condition_.notify_one();
    }

private:
    struct ActiveSession final {
        application::PlaybackRequestContext context;
        std::vector<domain::ComparisonSource> sources;
        domain::CanonicalTimeline timeline;
    };

    struct SetTableEntry final {
        application::FrameRequestContext context;
        domain::FrameId frameId;
        application::FrameRequestPriority priority;
        application::FrameSet set;
        std::uint64_t insertionOrder;
    };

    [[nodiscard]] std::size_t queuedCountLocked() const noexcept {
        return controlQueue_.size() + exactQueue_.size() + sequentialQueue_.size() +
               prefetchQueue_.size();
    }

    [[nodiscard]] std::deque<std::shared_ptr<ProviderOperation>>&
    queueForPriority(const application::FrameRequestPriority priority) noexcept {
        switch (priority) {
        case application::FrameRequestPriority::Exact:
            return exactQueue_;
        case application::FrameRequestPriority::Sequential:
            return sequentialQueue_;
        case application::FrameRequestPriority::Prefetch:
            return prefetchQueue_;
        }
        std::terminate();
    }

    void
    cancelQueueLocked(std::deque<std::shared_ptr<ProviderOperation>>& queue,
                      const application::CancellationReason reason,
                      std::vector<std::shared_ptr<ProviderOperation>>* const displaced) noexcept {
        while (!queue.empty()) {
            std::shared_ptr<ProviderOperation> operation = std::move(queue.front());
            queue.pop_front();
            if (operation->requestCancellation(reason)) {
                displaced->push_back(std::move(operation));
            }
        }
    }

    void
    cancelQueuedLocked(const application::CancellationReason reason,
                       std::vector<std::shared_ptr<ProviderOperation>>* const displaced) noexcept {
        cancelQueueLocked(controlQueue_, reason, displaced);
        cancelQueueLocked(exactQueue_, reason, displaced);
        cancelQueueLocked(sequentialQueue_, reason, displaced);
        cancelQueueLocked(prefetchQueue_, reason, displaced);
    }

    void
    cancelFrontLocked(std::deque<std::shared_ptr<ProviderOperation>>& queue,
                      const application::CancellationReason reason,
                      std::vector<std::shared_ptr<ProviderOperation>>* const displaced) noexcept {
        if (queue.empty()) {
            return;
        }
        std::shared_ptr<ProviderOperation> operation = std::move(queue.front());
        queue.pop_front();
        if (operation->requestCancellation(reason)) {
            displaced->push_back(std::move(operation));
        }
    }

    void cancelFarthestPrefetchLocked(
        const domain::FrameId incomingFrame,
        std::vector<std::shared_ptr<ProviderOperation>>* const displaced) noexcept {
        if (prefetchQueue_.empty()) {
            return;
        }
        const domain::FrameId anchor = latestExactFrame_.value_or(incomingFrame);
        const auto farthest = std::max_element(
            prefetchQueue_.begin(),
            prefetchQueue_.end(),
            [&anchor](const std::shared_ptr<ProviderOperation>& left,
                      const std::shared_ptr<ProviderOperation>& right) {
                const auto& leftRequest = std::get<application::FrameRequest>(left->request);
                const auto& rightRequest = std::get<application::FrameRequest>(right->request);
                return frameDistance(leftRequest.frameId, anchor) <
                       frameDistance(rightRequest.frameId, anchor);
            });
        std::shared_ptr<ProviderOperation> operation = std::move(*farthest);
        prefetchQueue_.erase(farthest);
        if (operation->requestCancellation(application::CancellationReason::Superseded)) {
            displaced->push_back(std::move(operation));
        }
    }

    [[nodiscard]] bool cancelActiveLocked(const application::CancellationReason reason) noexcept {
        return activeOperation_ != nullptr && activeOperation_->requestCancellation(reason);
    }

    [[nodiscard]] bool cancelActiveExactLocked() noexcept {
        if (activeOperation_ == nullptr ||
            activeOperation_->kind != ProviderOperationKind::kFrame) {
            return false;
        }
        const auto& request = std::get<application::FrameRequest>(activeOperation_->request);
        return request.priority == application::FrameRequestPriority::Exact &&
               activeOperation_->requestCancellation(application::CancellationReason::Superseded);
    }

    void cancelOutdatedFrameQueuesLocked(
        const application::PlaybackRequestContext& newestContext,
        std::vector<std::shared_ptr<ProviderOperation>>* const displaced) noexcept {
        cancelOutdatedFramesLocked(exactQueue_, newestContext, displaced);
        cancelOutdatedFramesLocked(sequentialQueue_, newestContext, displaced);
        cancelOutdatedFramesLocked(prefetchQueue_, newestContext, displaced);
    }

    void cancelOutdatedFramesLocked(
        std::deque<std::shared_ptr<ProviderOperation>>& queue,
        const application::PlaybackRequestContext& newestContext,
        std::vector<std::shared_ptr<ProviderOperation>>* const displaced) noexcept {
        auto iterator = queue.begin();
        while (iterator != queue.end()) {
            const auto& request = std::get<application::FrameRequest>((*iterator)->request);
            if (!sameSessionEpoch(request.context.playback, newestContext) ||
                request.context.playback.playbackGeneration == newestContext.playbackGeneration) {
                ++iterator;
                continue;
            }
            std::shared_ptr<ProviderOperation> canceled = std::move(*iterator);
            iterator = queue.erase(iterator);
            if (canceled->requestCancellation(application::CancellationReason::Superseded)) {
                displaced->push_back(std::move(canceled));
            }
        }
    }

    [[nodiscard]] bool cancelActiveOutdatedFrameLocked(
        const application::PlaybackRequestContext& newestContext) noexcept {
        if (activeOperation_ == nullptr ||
            activeOperation_->kind != ProviderOperationKind::kFrame) {
            return false;
        }
        const auto& request = std::get<application::FrameRequest>(activeOperation_->request);
        return sameSessionEpoch(request.context.playback, newestContext) &&
               request.context.playback.playbackGeneration != newestContext.playbackGeneration &&
               activeOperation_->requestCancellation(application::CancellationReason::Superseded);
    }

    void cancelMatchingQueueLocked(
        std::deque<std::shared_ptr<ProviderOperation>>& queue,
        const application::PlaybackRequestContext& context,
        const application::CancellationReason reason,
        std::vector<std::shared_ptr<ProviderOperation>>* const displaced) noexcept {
        auto iterator = queue.begin();
        while (iterator != queue.end()) {
            const std::shared_ptr<ProviderOperation>& operation = *iterator;
            if (!samePlaybackScope(playbackContext(*operation), context)) {
                ++iterator;
                continue;
            }
            std::shared_ptr<ProviderOperation> canceled = std::move(*iterator);
            iterator = queue.erase(iterator);
            if (canceled->requestCancellation(reason)) {
                displaced->push_back(std::move(canceled));
            }
        }
    }

    static void
    postCanceledAll(const std::vector<std::shared_ptr<ProviderOperation>>& operations) noexcept {
        for (const std::shared_ptr<ProviderOperation>& operation : operations) {
            postCanceled(operation);
        }
    }

    [[nodiscard]] std::shared_ptr<ProviderOperation> takeNextLocked() {
        if (!controlQueue_.empty()) {
            std::shared_ptr<ProviderOperation> operation = std::move(controlQueue_.front());
            controlQueue_.pop_front();
            return operation;
        }
        if (!exactQueue_.empty()) {
            std::shared_ptr<ProviderOperation> operation = std::move(exactQueue_.front());
            exactQueue_.pop_front();
            return operation;
        }
        if (!sequentialQueue_.empty()) {
            std::shared_ptr<ProviderOperation> operation = std::move(sequentialQueue_.front());
            sequentialQueue_.pop_front();
            return operation;
        }
        std::shared_ptr<ProviderOperation> operation = std::move(prefetchQueue_.front());
        prefetchQueue_.pop_front();
        return operation;
    }

    [[nodiscard]] bool hasPendingLocked() const noexcept {
        return !controlQueue_.empty() || !exactQueue_.empty() || !sequentialQueue_.empty() ||
               !prefetchQueue_.empty();
    }

    void closeDecoders() noexcept {
        for (auto& decoder : decoders_) {
            if (decoder != nullptr) {
                decoder->close();
            }
        }
        decoders_.clear();
        activeSession_.reset();
        setTable_.clear();
        sequentialSetFrame_.reset();
        sequentialSetReady_ = false;
    }

    void executeOpen(const std::shared_ptr<ProviderOperation>& operation) noexcept {
        if (operation->isCanceled()) {
            postCanceled(operation);
            return;
        }

        const auto& request = std::get<application::FrameProviderOpenRequest>(operation->request);

        if (request.sources.size() < 2U || request.sources.size() > 3U) {
            postFailed(operation,
                       providerError(domain::MediaErrorCode::kInvalidArgument,
                                     std::nullopt,
                                     "The frame provider requires 2 or 3 comparison sources."));
            return;
        }

        const domain::MediaDescriptor& canonicalDescriptor = request.sources.front().descriptor;
        if (domain::isVariableFrameRate(request.timeline)) {
            const auto& variableTimeline =
                std::get<std::shared_ptr<const domain::FrameTimeline>>(request.timeline);
            if (!variableTimeline) {
                postFailed(operation,
                           providerError(domain::MediaErrorCode::kFrameTimelineInvalid,
                                         std::nullopt,
                                         "A variable-frame-rate timeline must be non-null."));
                return;
            }
            if (canonicalDescriptor.frameRate.has_value()) {
                postFailed(
                    operation,
                    providerError(
                        domain::MediaErrorCode::kSourceFrameRateMismatch,
                        std::nullopt,
                        "A VFR timeline requires the canonical source to declare no rational frame rate."));
                return;
            }
            if (variableTimeline->frameCount() != canonicalDescriptor.frameCount.value) {
                postFailed(operation,
                           providerError(domain::MediaErrorCode::kInvalidFrameCount,
                                         std::nullopt,
                                         "The VFR timeline frame count does not match the canonical "
                                         "source frame count."));
                return;
            }
        } else if (!canonicalDescriptor.frameRate.has_value() ||
                   std::get<domain::RationalRate>(request.timeline) !=
                       canonicalDescriptor.frameRate.value()) {
            postFailed(operation,
                       providerError(
                           domain::MediaErrorCode::kSourceFrameRateMismatch,
                           std::nullopt,
                           "The rational timeline must match the canonical source frame rate."));
            return;
        }

        closeDecoders();
        interruptRequested_.store(false, std::memory_order_release);
        try {
            decoders_.reserve(request.sources.size());
            for (std::size_t slot = 0; slot < request.sources.size(); ++slot) {
                decoders_.push_back(
                    std::make_unique<internal::SoftwareDecoder>(request.sources[slot].id,
                                                                request.sources[slot].descriptor,
                                                                frameBudget_,
                                                                &interruptRequested_));
            }

            for (std::size_t slot = 0; slot < decoders_.size(); ++slot) {
                const domain::Status opened = decoders_[slot]->open(operation->cancellationRequested);
                if (operation->isCanceled()) {
                    closeDecoders();
                    postCanceled(operation);
                    return;
                }
                if (!opened) {
                    closeDecoders();
                    postFailed(operation, opened.error());
                    return;
                }
            }

            activeSession_ = ActiveSession{
                .context = request.context,
                .sources = request.sources,
                .timeline = request.timeline,
            };
            {
                std::scoped_lock lock(mutex_);
                if (!latestFrameContext_.has_value() ||
                    !sameSessionEpoch(*latestFrameContext_, request.context) ||
                    latestFrameContext_->playbackGeneration.value() <
                        request.context.playbackGeneration.value()) {
                    latestFrameContext_ = request.context;
                }
            }
            postSucceeded(operation);
        } catch (const std::exception& exception) {
            closeDecoders();
            postFailed(operation,
                       providerError(domain::MediaErrorCode::kMediaDecodeFailed,
                                     std::nullopt,
                                     "Unexpected multi-source provider open exception: " +
                                         std::string{exception.what()},
                                     true));
        } catch (...) {
            closeDecoders();
            postFailed(operation,
                       providerError(domain::MediaErrorCode::kMediaDecodeFailed,
                                     std::nullopt,
                                     "Unexpected non-standard multi-source provider open exception.",
                                     true));
        }
    }

    void cacheSet(const application::FrameRequest& request, const application::FrameSet& set) {
        // Continuous playback has one set in flight and never re-requests an already presented
        // sequential frame. Retaining those CPU resources would make memory grow with playback
        // history and exhaust the shared budget before the fixed-size table can evict 4K sets.
        // Exact and prefetch entries keep their existing reuse and eviction semantics.
        if (request.priority == application::FrameRequestPriority::Sequential) {
            return;
        }

        const auto matchesRequest = [&request](const SetTableEntry& entry) {
            return entry.context == request.context && entry.frameId == request.frameId;
        };
        setTable_.erase(std::remove_if(setTable_.begin(), setTable_.end(), matchesRequest),
                        setTable_.end());

        if (request.priority == application::FrameRequestPriority::Exact) {
            setTable_.erase(std::remove_if(setTable_.begin(),
                                           setTable_.end(),
                                           [](const SetTableEntry& entry) {
                                               return entry.priority ==
                                                      application::FrameRequestPriority::Exact;
                                           }),
                            setTable_.end());
        }

        if (setTable_.size() >= kSetTableCapacity) {
            const auto evictable =
                std::min_element(setTable_.begin(),
                                 setTable_.end(),
                                 [](const SetTableEntry& left, const SetTableEntry& right) {
                                     const bool leftExact =
                                         left.priority == application::FrameRequestPriority::Exact;
                                     const bool rightExact =
                                         right.priority == application::FrameRequestPriority::Exact;
                                     if (leftExact != rightExact) {
                                         return !leftExact;
                                     }
                                     return left.insertionOrder < right.insertionOrder;
                                 });
            if (evictable == setTable_.end() ||
                evictable->priority == application::FrameRequestPriority::Exact) {
                return;
            }
            setTable_.erase(evictable);
        }

        setTable_.push_back(SetTableEntry{
            .context = request.context,
            .frameId = request.frameId,
            .priority = request.priority,
            .set = set,
            .insertionOrder = nextSetInsertionOrder_++,
        });
    }

    [[nodiscard]] std::optional<application::FrameSet>
    cachedSet(const application::FrameRequest& request) const {
        const auto cached =
            std::find_if(setTable_.begin(), setTable_.end(), [&request](const auto& entry) {
                return entry.context == request.context && entry.frameId == request.frameId;
            });
        if (cached == setTable_.end()) {
            return std::nullopt;
        }
        return cached->set;
    }

    void executeFrame(const std::shared_ptr<ProviderOperation>& operation) noexcept {
        if (operation->isCanceled()) {
            postCanceled(operation);
            return;
        }
        const auto& request = std::get<application::FrameRequest>(operation->request);
        if (!activeSession_.has_value() || decoders_.empty()) {
            postFailed(operation,
                       providerError(domain::MediaErrorCode::kMediaDecodeFailed,
                                     std::nullopt,
                                     "No comparison sources are open for this frame request.",
                                     true));
            return;
        }
        std::optional<application::PlaybackRequestContext> latestContext;
        {
            std::scoped_lock lock(mutex_);
            latestContext = latestFrameContext_;
        }
        if (!latestContext.has_value() ||
            !samePlaybackScope(request.context.playback, *latestContext) ||
            !sameSessionEpoch(request.context.playback, activeSession_->context)) {
            static_cast<void>(
                operation->requestCancellation(application::CancellationReason::Superseded));
            postCanceled(operation);
            return;
        }
        if (!samePlaybackScope(request.context.playback, activeSession_->context)) {
            activeSession_->context = request.context.playback;
            setTable_.clear();
        }
        if (const std::optional<application::FrameSet> cached = cachedSet(request)) {
            sequentialSetReady_ = false;
            postFrameSetSucceeded(operation, *cached);
            return;
        }

        const auto canonicalTime =
            domain::canonicalFrameStartTime(activeSession_->timeline, request.frameId);
        if (!canonicalTime) {
            domain::MediaError error = canonicalTime.error();
            error.operation = domain::MediaOperation::kMediaDecode;
            error.source = std::nullopt;
            postFailed(operation, std::move(error));
            return;
        }

        interruptRequested_.store(false, std::memory_order_release);
        const bool continueSequentially =
            request.priority == application::FrameRequestPriority::Sequential &&
            sequentialSetReady_ && sequentialSetFrame_.has_value() &&
            request.frameId.value() > sequentialSetFrame_->value();
        sequentialSetReady_ = false;
        try {
            std::vector<application::MappedSourceFrame> entries;
            entries.reserve(decoders_.size());

            for (std::size_t slot = 0; slot < decoders_.size(); ++slot) {
                const domain::ComparisonSource& source = activeSession_->sources[slot];
                const domain::SourceId sourceId = source.id;
                const std::int64_t sourceFrameCount = source.descriptor.frameCount.value;

                if (!request.frameId.isValid() || request.frameId.value() >= sourceFrameCount) {
                    entries.push_back(application::MappedSourceFrame{
                        .sourceId = sourceId,
                        .sourceFrameId = std::nullopt,
                        .frame = std::nullopt,
                        .presentationTime = domain::MediaTime{0},
                        .matchKind = application::FrameMatchKind::Missing,
                        .alignmentConfidence = 1.0F,
                    });
                    continue;
                }

                auto decoded =
                    continueSequentially
                        ? decoders_[slot]->decodeSequential(request.frameId,
                                                           operation->cancellationRequested)
                        : decoders_[slot]->decodeExact(request.frameId,
                                                       operation->cancellationRequested);
                if (operation->isCanceled()) {
                    postCanceled(operation);
                    return;
                }
                if (!decoded) {
                    entries.push_back(application::MappedSourceFrame{
                        .sourceId = sourceId,
                        .sourceFrameId = std::nullopt,
                        .frame = std::nullopt,
                        .presentationTime = domain::MediaTime{0},
                        .matchKind = application::FrameMatchKind::Missing,
                        .alignmentConfidence = 1.0F,
                    });
                    continue;
                }

                entries.push_back(application::MappedSourceFrame{
                    .sourceId = sourceId,
                    .sourceFrameId = request.frameId,
                    .frame = std::move(decoded.value().handle),
                    .presentationTime = decoded.value().presentationTime,
                    .matchKind = application::FrameMatchKind::ExactIndex,
                    .alignmentConfidence = 1.0F,
                });
            }

            auto set = application::FrameSet::create(request.frameId, canonicalTime.value(), std::move(entries));
            if (!set) {
                postFailed(operation,
                           providerError(
                               domain::MediaErrorCode::kMediaDecodeFailed,
                               std::nullopt,
                               "The decoded source frames could not form a valid frame set."));
                return;
            }
            if (operation->isCanceled()) {
                postCanceled(operation);
                return;
            }

            sequentialSetFrame_ = request.frameId;
            sequentialSetReady_ = true;
            cacheSet(request, *set);
            postFrameSetSucceeded(operation, *set);
        } catch (const std::exception& exception) {
            postFailed(operation,
                       providerError(domain::MediaErrorCode::kMediaDecodeFailed,
                                     std::nullopt,
                                     "Unexpected multi-source frame decode exception: " +
                                         std::string{exception.what()},
                                     true));
        } catch (...) {
            postFailed(operation,
                       providerError(domain::MediaErrorCode::kMediaDecodeFailed,
                                     std::nullopt,
                                     "Unexpected non-standard multi-source frame decode exception.",
                                     true));
        }
    }

    void executeClose(const std::shared_ptr<ProviderOperation>& operation) noexcept {
        if (operation->isCanceled()) {
            postCanceled(operation);
            return;
        }
        closeDecoders();
        {
            std::scoped_lock lock(mutex_);
            if (latestFrameContext_.has_value() &&
                sameSessionEpoch(
                    *latestFrameContext_,
                    std::get<application::FrameProviderCloseRequest>(operation->request).context)) {
                latestFrameContext_.reset();
            }
        }
        postSucceeded(operation);
    }

    void execute(const std::shared_ptr<ProviderOperation>& operation) noexcept {
        switch (operation->kind) {
        case ProviderOperationKind::kOpen:
            executeOpen(operation);
            return;
        case ProviderOperationKind::kFrame:
            executeFrame(operation);
            return;
        case ProviderOperationKind::kClose:
            executeClose(operation);
            return;
        }
        std::terminate();
    }

    void run() noexcept {
        for (;;) {
            std::shared_ptr<ProviderOperation> operation;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] { return closed_ || hasPendingLocked(); });
                if (!hasPendingLocked()) {
                    if (closed_) {
                        return;
                    }
                    continue;
                }
                operation = takeNextLocked();
                activeOperation_ = operation;
            }

            execute(operation);

            {
                std::scoped_lock lock(mutex_);
                if (activeOperation_ == operation) {
                    activeOperation_.reset();
                }
            }
        }
    }

    void shutdown() noexcept {
        std::vector<std::shared_ptr<ProviderOperation>> displaced;
        bool interrupt = false;
        {
            std::scoped_lock lock(mutex_);
            if (closed_) {
                return;
            }
            closed_ = true;
            cancelQueuedLocked(application::CancellationReason::Shutdown, &displaced);
            interrupt = cancelActiveLocked(application::CancellationReason::Shutdown);
        }
        if (interrupt) {
            interruptRequested_.store(true, std::memory_order_release);
        }
        postCanceledAll(displaced);
        condition_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        closeDecoders();
    }

    platform::FrameBudget& frameBudget_;
    std::size_t queueCapacity_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::shared_ptr<ProviderOperation>> controlQueue_;
    std::deque<std::shared_ptr<ProviderOperation>> exactQueue_;
    std::deque<std::shared_ptr<ProviderOperation>> sequentialQueue_;
    std::deque<std::shared_ptr<ProviderOperation>> prefetchQueue_;
    std::shared_ptr<ProviderOperation> activeOperation_;
    std::optional<domain::FrameId> latestExactFrame_;
    std::optional<application::PlaybackRequestContext> latestFrameContext_;
    std::optional<domain::FrameId> sequentialSetFrame_;
    bool sequentialSetReady_ = false;
    bool closed_ = false;
    std::atomic<bool> interruptRequested_ = false;
    std::thread worker_;

    std::vector<std::unique_ptr<internal::SoftwareDecoder>> decoders_;
    std::optional<ActiveSession> activeSession_;
    std::deque<SetTableEntry> setTable_;
    std::uint64_t nextSetInsertionOrder_ = 0;
};

MultiSourceFrameProvider::MultiSourceFrameProvider(platform::FrameBudget& frameBudget,
                                                   const std::size_t requestCapacity)
    : impl_(std::make_unique<Impl>(frameBudget, requestCapacity)) {}

MultiSourceFrameProvider::~MultiSourceFrameProvider() = default;

application::PortSubmitResult
MultiSourceFrameProvider::submit(const application::FrameProviderOpenRequest& request,
                                 std::shared_ptr<application::IApplicationEventSink> events) {
    return impl_->submit(request, events);
}

application::PortSubmitResult
MultiSourceFrameProvider::submit(const application::FrameRequest& request,
                                 std::shared_ptr<application::IApplicationEventSink> events) {
    return impl_->submit(request, events);
}

application::PortSubmitResult
MultiSourceFrameProvider::submit(const application::FrameProviderCloseRequest& request,
                                 std::shared_ptr<application::IApplicationEventSink> events) {
    return impl_->submit(request, events);
}

void MultiSourceFrameProvider::cancel(const application::PlaybackRequestContext& context) noexcept {
    impl_->cancel(context);
}

} // namespace dvs::media
