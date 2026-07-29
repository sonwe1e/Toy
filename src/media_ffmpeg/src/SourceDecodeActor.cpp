#include "SourceDecodeActor.h"

#include "dvs/domain/MediaError.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace dvs::media::internal {
namespace {

constexpr std::size_t kExactCapacity = 1U;
constexpr std::size_t kSequentialCapacity = 2U;
constexpr std::size_t kPrefetchCapacity = 8U;
constexpr std::size_t kAnalysisCapacity = 1U;

[[nodiscard]] domain::MediaError
actorError(const domain::SourceId sourceId, std::string detail, const bool recoverable = true) {
    return domain::makeMediaError(domain::MediaErrorCode::kMediaDecodeFailed,
                                  domain::MediaOperation::kMediaDecode,
                                  sourceId,
                                  recoverable,
                                  std::move(detail));
}

[[nodiscard]] std::size_t capacityFor(const SourceDecodePriority priority) noexcept {
    switch (priority) {
    case SourceDecodePriority::Exact:
        return kExactCapacity;
    case SourceDecodePriority::Sequential:
        return kSequentialCapacity;
    case SourceDecodePriority::Prefetch:
        return kPrefetchCapacity;
    case SourceDecodePriority::Analysis:
        return kAnalysisCapacity;
    }
    std::terminate();
}

[[nodiscard]] bool olderPlaybackContext(const application::FrameRequestContext& candidate,
                                        const application::FrameRequestContext& newest) noexcept {
    const auto& candidatePlayback = candidate.playback;
    const auto& newestPlayback = newest.playback;
    return candidatePlayback.request.sessionId == newestPlayback.request.sessionId &&
           candidatePlayback.request.sessionEpoch == newestPlayback.request.sessionEpoch &&
           candidatePlayback.playbackGeneration.value() < newestPlayback.playbackGeneration.value();
}

} // namespace

SourceDecodeActor::SourceDecodeActor(const domain::SourceId sourceId,
                                     domain::MediaDescriptor descriptor,
                                     platform::FrameBudget& frameBudget,
                                     const std::atomic<bool>* const externalInterrupt,
                                     const bool lowPriority)
    : sourceId_(sourceId), decoder_(std::make_unique<SoftwareDecoder>(
                               sourceId, std::move(descriptor), frameBudget, externalInterrupt)),
      worker_([this] { run(); }) {
    if (lowPriority) {
        static_cast<void>(SetThreadPriority(worker_.native_handle(), THREAD_PRIORITY_BELOW_NORMAL));
    }
    std::unique_lock lock{mutex_};
    condition_.wait(lock, [this] { return started_; });
}

SourceDecodeActor::~SourceDecodeActor() {
    shutdown();
}

domain::Status SourceDecodeActor::open(const std::atomic<bool>& cancellationRequested) {
    ControlJob job{
        .kind = ControlKind::Open,
        .cancellationRequested = &cancellationRequested,
    };
    std::future<domain::Status> completion = job.completion.get_future();
    {
        std::scoped_lock lock{mutex_};
        if (stopping_) {
            return domain::Status::failure(
                actorError(sourceId_, "The source decode actor is closed."));
        }
        controlQueue_.push_back(std::move(job));
    }
    condition_.notify_one();
    return completion.get();
}

SourceDecodeSubmission SourceDecodeActor::submit(SourceDecodeRequest request) {
    if (request.cancellationRequested == nullptr || !request.frameId.isValid()) {
        return SourceDecodeSubmission{.status = application::PortSubmitResult::Closed};
    }

    DecodeJob job{.request = std::move(request)};
    std::future<domain::Result<DecodedFrame>> completion = job.completion.get_future();
    std::vector<DecodeJob> displaced;
    {
        std::scoped_lock lock{mutex_};
        if (stopping_) {
            return SourceDecodeSubmission{.status = application::PortSubmitResult::Closed};
        }

        if (job.request.context.has_value()) {
            if (latestContext_.has_value() &&
                olderPlaybackContext(*job.request.context, *latestContext_)) {
                return SourceDecodeSubmission{.status = application::PortSubmitResult::Closed};
            }
            if (!latestContext_.has_value() ||
                olderPlaybackContext(*latestContext_, *job.request.context)) {
                latestContext_ = job.request.context;
                const auto discardOlder = [this, &displaced](std::deque<DecodeJob>& queue) {
                    auto iterator = queue.begin();
                    while (iterator != queue.end()) {
                        if (iterator->request.context.has_value() &&
                            olderPlaybackContext(*iterator->request.context, *latestContext_)) {
                            displaced.push_back(std::move(*iterator));
                            iterator = queue.erase(iterator);
                        } else {
                            ++iterator;
                        }
                    }
                };
                discardOlder(exactQueue_);
                discardOlder(sequentialQueue_);
                discardOlder(prefetchQueue_);
            }
        }

        if (job.request.priority == SourceDecodePriority::Exact) {
            while (!prefetchQueue_.empty()) {
                displaced.push_back(std::move(prefetchQueue_.front()));
                prefetchQueue_.pop_front();
            }
        }

        std::deque<DecodeJob>& queue = queueFor(job.request.priority);
        while (queue.size() >= capacityFor(job.request.priority)) {
            displaced.push_back(std::move(queue.front()));
            queue.pop_front();
        }
        queue.push_back(std::move(job));
    }
    for (DecodeJob& canceled : displaced) {
        completeCanceled(std::move(canceled));
    }
    condition_.notify_one();
    return SourceDecodeSubmission{
        .status = application::PortSubmitResult::Accepted,
        .completion = std::move(completion),
    };
}

void SourceDecodeActor::close() noexcept {
    ControlJob job{.kind = ControlKind::Close};
    std::future<domain::Status> completion = job.completion.get_future();
    {
        std::scoped_lock lock{mutex_};
        if (stopping_) {
            return;
        }
        cancelQueuedLocked();
        controlQueue_.push_back(std::move(job));
    }
    requestInterrupt();
    condition_.notify_one();
    static_cast<void>(completion.get());
}

void SourceDecodeActor::requestInterrupt() noexcept {
    decoder_->requestInterrupt();
}

void SourceDecodeActor::shutdown() noexcept {
    {
        std::scoped_lock lock{mutex_};
        if (stopping_) {
            return;
        }
        stopping_ = true;
        cancelQueuedLocked();
    }
    requestInterrupt();
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::thread::id SourceDecodeActor::workerThreadId() const noexcept {
    std::scoped_lock lock{mutex_};
    return workerThreadId_;
}

std::thread::id SourceDecodeActor::lastDecodeThreadId() const noexcept {
    std::scoped_lock lock{mutex_};
    return lastDecodeThreadId_;
}

std::uint64_t SourceDecodeActor::completedDecodeCount() const noexcept {
    std::scoped_lock lock{mutex_};
    return completedDecodeCount_;
}

std::deque<SourceDecodeActor::DecodeJob>&
SourceDecodeActor::queueFor(const SourceDecodePriority priority) noexcept {
    switch (priority) {
    case SourceDecodePriority::Exact:
        return exactQueue_;
    case SourceDecodePriority::Sequential:
        return sequentialQueue_;
    case SourceDecodePriority::Prefetch:
        return prefetchQueue_;
    case SourceDecodePriority::Analysis:
        return analysisQueue_;
    }
    std::terminate();
}

bool SourceDecodeActor::hasPendingLocked() const noexcept {
    return !controlQueue_.empty() || !exactQueue_.empty() || !sequentialQueue_.empty() ||
           !prefetchQueue_.empty() || !analysisQueue_.empty();
}

std::optional<SourceDecodeActor::DecodeJob> SourceDecodeActor::takeNextDecodeLocked() {
    const auto take = [](std::deque<DecodeJob>& queue) -> std::optional<DecodeJob> {
        if (queue.empty()) {
            return std::nullopt;
        }
        DecodeJob job = std::move(queue.front());
        queue.pop_front();
        return job;
    };
    if (std::optional<DecodeJob> job = take(exactQueue_)) {
        return job;
    }
    if (std::optional<DecodeJob> job = take(sequentialQueue_)) {
        return job;
    }
    if (std::optional<DecodeJob> job = take(prefetchQueue_)) {
        return job;
    }
    return take(analysisQueue_);
}

void SourceDecodeActor::run() noexcept {
    {
        std::scoped_lock lock{mutex_};
        workerThreadId_ = std::this_thread::get_id();
        started_ = true;
    }
    condition_.notify_all();

    for (;;) {
        std::optional<ControlJob> control;
        std::optional<DecodeJob> decode;
        {
            std::unique_lock lock{mutex_};
            condition_.wait(lock, [this] { return stopping_ || hasPendingLocked(); });
            if (stopping_) {
                break;
            }
            if (!controlQueue_.empty()) {
                control = std::move(controlQueue_.front());
                controlQueue_.pop_front();
            } else {
                decode = takeNextDecodeLocked();
            }
        }

        if (control.has_value()) {
            if (control->kind == ControlKind::Open && control->cancellationRequested != nullptr) {
                control->completion.set_value(decoder_->open(*control->cancellationRequested));
            } else {
                decoder_->close();
                control->completion.set_value(domain::Status::success());
            }
            continue;
        }
        if (!decode.has_value()) {
            continue;
        }

        domain::Result<DecodedFrame> result =
            decode->request.continueSequentially
                ? decoder_->decodeSequential(decode->request.frameId,
                                             *decode->request.cancellationRequested)
                : decoder_->decodeExact(decode->request.frameId,
                                        *decode->request.cancellationRequested);
        {
            std::scoped_lock lock{mutex_};
            lastDecodeThreadId_ = std::this_thread::get_id();
            ++completedDecodeCount_;
        }
        decode->completion.set_value(std::move(result));
    }
    decoder_->close();
}

void SourceDecodeActor::cancelQueuedLocked() {
    const auto cancel = [this](std::deque<DecodeJob>& queue) {
        while (!queue.empty()) {
            DecodeJob job = std::move(queue.front());
            queue.pop_front();
            completeCanceled(std::move(job));
        }
    };
    cancel(exactQueue_);
    cancel(sequentialQueue_);
    cancel(prefetchQueue_);
    cancel(analysisQueue_);
}

void SourceDecodeActor::completeCanceled(DecodeJob job) noexcept {
    try {
        job.completion.set_value(domain::Result<DecodedFrame>::failure(
            actorError(sourceId_, "The queued source decode request was superseded.")));
    } catch (...) {
    }
}

} // namespace dvs::media::internal
