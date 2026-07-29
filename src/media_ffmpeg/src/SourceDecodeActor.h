#pragma once

#include "dvs/application/Ports.h"
#include "dvs/domain/Result.h"

#include "SoftwareDecoder.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace dvs::platform {
class FrameBudget;
}

namespace dvs::media::internal {

enum class SourceDecodePriority {
    Exact,
    Sequential,
    Prefetch,
    Analysis,
};

struct SourceDecodeRequest final {
    domain::FrameId frameId{0};
    SourceDecodePriority priority = SourceDecodePriority::Exact;
    bool continueSequentially = false;
    const std::atomic<bool>* cancellationRequested = nullptr;
    std::optional<application::FrameRequestContext> context;
};

struct SourceDecodeSubmission final {
    application::PortSubmitResult status = application::PortSubmitResult::Closed;
    std::future<domain::Result<DecodedFrame>> completion;
};

// One long-lived worker owns one SoftwareDecoder for the complete open session. Its bounded
// priority mailboxes eliminate per-frame thread creation and keep decoder state single-threaded.
class SourceDecodeActor final {
public:
    SourceDecodeActor(domain::SourceId sourceId,
                      domain::MediaDescriptor descriptor,
                      platform::FrameBudget& frameBudget,
                      const std::atomic<bool>* externalInterrupt,
                      bool lowPriority = false);
    ~SourceDecodeActor();

    SourceDecodeActor(const SourceDecodeActor&) = delete;
    SourceDecodeActor& operator=(const SourceDecodeActor&) = delete;
    SourceDecodeActor(SourceDecodeActor&&) = delete;
    SourceDecodeActor& operator=(SourceDecodeActor&&) = delete;

    [[nodiscard]] domain::Status open(const std::atomic<bool>& cancellationRequested);
    [[nodiscard]] SourceDecodeSubmission submit(SourceDecodeRequest request);
    void close() noexcept;
    void requestInterrupt() noexcept;
    void shutdown() noexcept;

    // Component-test diagnostic: every decode for this actor must execute on this stable worker.
    [[nodiscard]] std::thread::id workerThreadId() const noexcept;
    [[nodiscard]] std::thread::id lastDecodeThreadId() const noexcept;
    [[nodiscard]] std::uint64_t completedDecodeCount() const noexcept;

private:
    enum class ControlKind {
        Open,
        Close,
    };

    struct DecodeJob final {
        SourceDecodeRequest request;
        std::promise<domain::Result<DecodedFrame>> completion;
    };

    struct ControlJob final {
        ControlKind kind;
        const std::atomic<bool>* cancellationRequested = nullptr;
        std::promise<domain::Status> completion;
    };

    [[nodiscard]] std::deque<DecodeJob>& queueFor(SourceDecodePriority priority) noexcept;
    [[nodiscard]] bool hasPendingLocked() const noexcept;
    [[nodiscard]] std::optional<DecodeJob> takeNextDecodeLocked();
    void run() noexcept;
    void cancelQueuedLocked();
    void completeCanceled(DecodeJob job) noexcept;

    domain::SourceId sourceId_;
    std::unique_ptr<SoftwareDecoder> decoder_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<ControlJob> controlQueue_;
    std::deque<DecodeJob> exactQueue_;
    std::deque<DecodeJob> sequentialQueue_;
    std::deque<DecodeJob> prefetchQueue_;
    std::deque<DecodeJob> analysisQueue_;
    std::optional<application::FrameRequestContext> latestContext_;
    bool stopping_ = false;
    bool started_ = false;
    std::thread worker_;
    std::thread::id workerThreadId_;
    std::thread::id lastDecodeThreadId_;
    std::uint64_t completedDecodeCount_ = 0U;
};

} // namespace dvs::media::internal
