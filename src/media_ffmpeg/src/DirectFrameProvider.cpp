#include "dvs/media/DirectFrameProvider.h"

#include "dvs/domain/FrameTimeline.h"
#include "dvs/domain/SourcePairValidator.h"
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
inline constexpr std::size_t kPairTableCapacity = 16U;

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
                                               const domain::SourceRole sourceRole,
                                               std::string technicalDetail,
                                               const bool recoverable = false) {
    return domain::makeMediaError(code,
                                  domain::MediaOperation::kMediaDecode,
                                  sourceRole,
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

void postFramePairSucceeded(const std::shared_ptr<ProviderOperation>& operation,
                            application::FramePair pair) noexcept {
    if (!operation->claimTerminal()) {
        postCanceled(operation);
        return;
    }
    const application::FrameRequestContext context =
        std::get<application::FrameRequest>(operation->request).context;
    postCritical(operation->events,
                 application::ApplicationEvent{application::FramePairReady{
                     .context = context,
                     .pair = std::move(pair),
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

class DirectFrameProvider::Impl final {
public:
    explicit Impl(platform::FrameBudget& frameBudget, const std::size_t requestCapacity)
        : frameBudget_(frameBudget), queueCapacity_(std::max(kPairTableCapacity, requestCapacity)),
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
        domain::MediaDescriptor sourceA;
        domain::MediaDescriptor sourceB;
        domain::CanonicalTimeline timeline;
    };

    struct PairTableEntry final {
        application::FrameRequestContext context;
        domain::FrameId frameId;
        application::FrameRequestPriority priority;
        application::FramePair pair;
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
        if (decoderA_ != nullptr) {
            decoderA_->close();
            decoderA_.reset();
        }
        if (decoderB_ != nullptr) {
            decoderB_->close();
            decoderB_.reset();
        }
        activeSession_.reset();
        pairTable_.clear();
        sequentialPairFrame_.reset();
        sequentialPairReady_ = false;
    }

    void executeOpen(const std::shared_ptr<ProviderOperation>& operation) noexcept {
        if (operation->isCanceled()) {
            postCanceled(operation);
            return;
        }

        const auto& request = std::get<application::FrameProviderOpenRequest>(operation->request);

        const auto validatedSources =
            domain::SourcePairValidator::validate(request.sourceA, request.sourceB);
        if (!validatedSources) {
            domain::MediaError error = validatedSources.error();
            error.operation = domain::MediaOperation::kMediaDecode;
            postFailed(operation, std::move(error));
            return;
        }

        const domain::MediaDescriptor& validatedSourceA = validatedSources.value().sourceA();
        if (domain::isVariableFrameRate(request.timeline)) {
            const auto& variableTimeline =
                std::get<std::shared_ptr<const domain::FrameTimeline>>(request.timeline);
            if (!variableTimeline) {
                postFailed(operation,
                           providerError(domain::MediaErrorCode::kFrameTimelineInvalid,
                                         domain::SourceRole::kPair,
                                         "A variable-frame-rate timeline must be non-null."));
                return;
            }
            if (validatedSourceA.frameRate.has_value()) {
                postFailed(
                    operation,
                    providerError(
                        domain::MediaErrorCode::kSourceFrameRateMismatch,
                        domain::SourceRole::kPair,
                        "A VFR timeline requires Source A to declare no rational frame rate."));
                return;
            }
            if (variableTimeline->frameCount() != validatedSourceA.frameCount.value) {
                postFailed(operation,
                           providerError(domain::MediaErrorCode::kInvalidFrameCount,
                                         domain::SourceRole::kPair,
                                         "The VFR timeline frame count does not match the source "
                                         "pair frame count."));
                return;
            }
        } else if (!validatedSourceA.frameRate.has_value() ||
                   std::get<domain::RationalRate>(request.timeline) !=
                       validatedSourceA.frameRate.value()) {
            postFailed(operation,
                       providerError(
                           domain::MediaErrorCode::kSourceFrameRateMismatch,
                           domain::SourceRole::kPair,
                           "The rational timeline must match the non-null Source A frame rate."));
            return;
        }

        closeDecoders();
        interruptRequested_.store(false, std::memory_order_release);
        try {
            decoderA_ =
                std::make_unique<internal::SoftwareDecoder>(domain::SourceRole::kA,
                                                            validatedSources.value().sourceA(),
                                                            frameBudget_,
                                                            &interruptRequested_);
            decoderB_ =
                std::make_unique<internal::SoftwareDecoder>(domain::SourceRole::kB,
                                                            validatedSources.value().sourceB(),
                                                            frameBudget_,
                                                            &interruptRequested_);

            const domain::Status sourceAOpened = decoderA_->open(operation->cancellationRequested);
            if (operation->isCanceled()) {
                closeDecoders();
                postCanceled(operation);
                return;
            }
            if (!sourceAOpened) {
                closeDecoders();
                postFailed(operation, sourceAOpened.error());
                return;
            }

            const domain::Status sourceBOpened = decoderB_->open(operation->cancellationRequested);
            if (operation->isCanceled()) {
                closeDecoders();
                postCanceled(operation);
                return;
            }
            if (!sourceBOpened) {
                closeDecoders();
                postFailed(operation, sourceBOpened.error());
                return;
            }

            activeSession_ = ActiveSession{
                .context = request.context,
                .sourceA = validatedSources.value().sourceA(),
                .sourceB = validatedSources.value().sourceB(),
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
                                     domain::SourceRole::kPair,
                                     "Unexpected direct provider open exception: " +
                                         std::string{exception.what()},
                                     true));
        } catch (...) {
            closeDecoders();
            postFailed(operation,
                       providerError(domain::MediaErrorCode::kMediaDecodeFailed,
                                     domain::SourceRole::kPair,
                                     "Unexpected non-standard direct provider open exception.",
                                     true));
        }
    }

    void cachePair(const application::FrameRequest& request, const application::FramePair& pair) {
        // Continuous playback has one pair in flight and never re-requests an already presented
        // sequential frame. Retaining those CPU resources would make memory grow with playback
        // history and exhaust the shared budget before the fixed-size table can evict 4K pairs.
        // Exact and prefetch entries keep their existing reuse and eviction semantics.
        if (request.priority == application::FrameRequestPriority::Sequential) {
            return;
        }

        const auto matchesRequest = [&request](const PairTableEntry& entry) {
            return entry.context == request.context && entry.frameId == request.frameId;
        };
        pairTable_.erase(std::remove_if(pairTable_.begin(), pairTable_.end(), matchesRequest),
                         pairTable_.end());

        if (request.priority == application::FrameRequestPriority::Exact) {
            pairTable_.erase(std::remove_if(pairTable_.begin(),
                                            pairTable_.end(),
                                            [](const PairTableEntry& entry) {
                                                return entry.priority ==
                                                       application::FrameRequestPriority::Exact;
                                            }),
                             pairTable_.end());
        }

        if (pairTable_.size() >= kPairTableCapacity) {
            const auto evictable =
                std::min_element(pairTable_.begin(),
                                 pairTable_.end(),
                                 [](const PairTableEntry& left, const PairTableEntry& right) {
                                     const bool leftExact =
                                         left.priority == application::FrameRequestPriority::Exact;
                                     const bool rightExact =
                                         right.priority == application::FrameRequestPriority::Exact;
                                     if (leftExact != rightExact) {
                                         return !leftExact;
                                     }
                                     return left.insertionOrder < right.insertionOrder;
                                 });
            if (evictable == pairTable_.end() ||
                evictable->priority == application::FrameRequestPriority::Exact) {
                return;
            }
            pairTable_.erase(evictable);
        }

        pairTable_.push_back(PairTableEntry{
            .context = request.context,
            .frameId = request.frameId,
            .priority = request.priority,
            .pair = pair,
            .insertionOrder = nextPairInsertionOrder_++,
        });
    }

    [[nodiscard]] std::optional<application::FramePair>
    cachedPair(const application::FrameRequest& request) const {
        const auto cached =
            std::find_if(pairTable_.begin(), pairTable_.end(), [&request](const auto& entry) {
                return entry.context == request.context && entry.frameId == request.frameId;
            });
        if (cached == pairTable_.end()) {
            return std::nullopt;
        }
        return cached->pair;
    }

    void executeFrame(const std::shared_ptr<ProviderOperation>& operation) noexcept {
        if (operation->isCanceled()) {
            postCanceled(operation);
            return;
        }
        const auto& request = std::get<application::FrameRequest>(operation->request);
        if (!activeSession_.has_value() || decoderA_ == nullptr || decoderB_ == nullptr) {
            postFailed(operation,
                       providerError(domain::MediaErrorCode::kMediaDecodeFailed,
                                     domain::SourceRole::kPair,
                                     "No direct source pair is open for this frame request.",
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
            pairTable_.clear();
        }
        if (const std::optional<application::FramePair> cached = cachedPair(request)) {
            sequentialPairReady_ = false;
            postFramePairSucceeded(operation, *cached);
            return;
        }

        const auto canonicalTime =
            domain::canonicalFrameStartTime(activeSession_->timeline, request.frameId);
        if (!canonicalTime) {
            domain::MediaError error = canonicalTime.error();
            error.operation = domain::MediaOperation::kMediaDecode;
            error.sourceRole = domain::SourceRole::kPair;
            postFailed(operation, std::move(error));
            return;
        }

        interruptRequested_.store(false, std::memory_order_release);
        const bool continueSequentially =
            request.priority == application::FrameRequestPriority::Sequential &&
            sequentialPairReady_ && sequentialPairFrame_.has_value() &&
            request.frameId.value() > sequentialPairFrame_->value();
        sequentialPairReady_ = false;
        try {
            auto sourceA =
                continueSequentially
                    ? decoderA_->decodeSequential(request.frameId, operation->cancellationRequested)
                    : decoderA_->decodeExact(request.frameId, operation->cancellationRequested);
            if (operation->isCanceled()) {
                postCanceled(operation);
                return;
            }
            if (!sourceA) {
                postFailed(operation, sourceA.error());
                return;
            }

            auto sourceB =
                continueSequentially
                    ? decoderB_->decodeSequential(request.frameId, operation->cancellationRequested)
                    : decoderB_->decodeExact(request.frameId, operation->cancellationRequested);
            if (operation->isCanceled()) {
                postCanceled(operation);
                return;
            }
            if (!sourceB) {
                postFailed(operation, sourceB.error());
                return;
            }

            const std::optional<application::FramePair> pair =
                application::FramePair::create(request.frameId,
                                               canonicalTime.value(),
                                               sourceA.value().handle,
                                               sourceA.value().presentationTime,
                                               sourceB.value().handle,
                                               sourceB.value().presentationTime);
            if (!pair) {
                postFailed(operation,
                           providerError(
                               domain::MediaErrorCode::kMediaDecodeFailed,
                               domain::SourceRole::kPair,
                               "The decoded source frames could not form a complete frame pair."));
                return;
            }
            if (operation->isCanceled()) {
                postCanceled(operation);
                return;
            }

            sequentialPairFrame_ = request.frameId;
            sequentialPairReady_ = true;
            cachePair(request, *pair);
            postFramePairSucceeded(operation, *pair);
        } catch (const std::exception& exception) {
            postFailed(operation,
                       providerError(domain::MediaErrorCode::kMediaDecodeFailed,
                                     domain::SourceRole::kPair,
                                     "Unexpected direct frame decode exception: " +
                                         std::string{exception.what()},
                                     true));
        } catch (...) {
            postFailed(operation,
                       providerError(domain::MediaErrorCode::kMediaDecodeFailed,
                                     domain::SourceRole::kPair,
                                     "Unexpected non-standard direct frame decode exception.",
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
    std::optional<domain::FrameId> sequentialPairFrame_;
    bool sequentialPairReady_ = false;
    bool closed_ = false;
    std::atomic<bool> interruptRequested_ = false;
    std::thread worker_;

    std::unique_ptr<internal::SoftwareDecoder> decoderA_;
    std::unique_ptr<internal::SoftwareDecoder> decoderB_;
    std::optional<ActiveSession> activeSession_;
    std::deque<PairTableEntry> pairTable_;
    std::uint64_t nextPairInsertionOrder_ = 0;
};

DirectFrameProvider::DirectFrameProvider(platform::FrameBudget& frameBudget,
                                         const std::size_t requestCapacity)
    : impl_(std::make_unique<Impl>(frameBudget, requestCapacity)) {}

DirectFrameProvider::~DirectFrameProvider() = default;

application::PortSubmitResult
DirectFrameProvider::submit(const application::FrameProviderOpenRequest& request,
                            std::shared_ptr<application::IApplicationEventSink> events) {
    return impl_->submit(request, events);
}

application::PortSubmitResult
DirectFrameProvider::submit(const application::FrameRequest& request,
                            std::shared_ptr<application::IApplicationEventSink> events) {
    return impl_->submit(request, events);
}

application::PortSubmitResult
DirectFrameProvider::submit(const application::FrameProviderCloseRequest& request,
                            std::shared_ptr<application::IApplicationEventSink> events) {
    return impl_->submit(request, events);
}

void DirectFrameProvider::cancel(const application::PlaybackRequestContext& context) noexcept {
    impl_->cancel(context);
}

} // namespace dvs::media
