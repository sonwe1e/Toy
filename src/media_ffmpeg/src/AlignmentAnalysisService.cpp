#include "dvs/media/AlignmentAnalysisService.h"

#include "dvs/media/MultiSourceFrameProvider.h"
#include "dvs/platform/FrameBudget.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace dvs::media {
namespace {

using AnalysisRequest =
    std::variant<application::AlignmentEstimateRequest, application::SequenceAlignmentRequest>;

[[nodiscard]] const application::PlaybackRequestContext&
context(const AnalysisRequest& request) noexcept {
    return std::visit(
        [](const auto& value) -> const application::PlaybackRequestContext& {
            return value.context;
        },
        request);
}

[[nodiscard]] application::AlignmentAnalysisJobId jobId(const AnalysisRequest& request) noexcept {
    return std::visit([](const auto& value) { return value.jobId; }, request);
}

[[nodiscard]] application::AlignmentAnalysisKind kind(const AnalysisRequest& request) noexcept {
    return std::holds_alternative<application::AlignmentEstimateRequest>(request)
               ? application::AlignmentAnalysisKind::GlobalOffset
               : application::AlignmentAnalysisKind::Sequence;
}

[[nodiscard]] std::uint64_t totalFrames(const AnalysisRequest& request) noexcept {
    return std::visit(
        [](const auto& value) {
            std::uint64_t total = 0U;
            for (const domain::ComparisonSource& source : value.sources) {
                if (source.descriptor.frameCount.value > 0) {
                    total += static_cast<std::uint64_t>(source.descriptor.frameCount.value);
                }
            }
            return total;
        },
        request);
}

[[nodiscard]] domain::MediaError serviceError(std::string detail) {
    return domain::makeMediaError(domain::MediaErrorCode::kMediaDecodeFailed,
                                  domain::MediaOperation::kMediaDecode,
                                  std::nullopt,
                                  true,
                                  std::move(detail));
}

class EventQueue final : public application::IApplicationEventSink {
public:
    [[nodiscard]] application::EventPostResult
    postCritical(application::ApplicationEvent event) noexcept override {
        {
            std::scoped_lock lock(mutex_);
            if (closed_) {
                return application::EventPostResult::Closed;
            }
            critical_.push_back(std::move(event));
        }
        condition_.notify_one();
        return application::EventPostResult::Accepted;
    }

    [[nodiscard]] application::EventPostResult
    postRealtime(application::ApplicationEvent event) noexcept override {
        {
            std::scoped_lock lock(mutex_);
            if (closed_) {
                return application::EventPostResult::Closed;
            }
            realtime_ = std::move(event);
        }
        condition_.notify_one();
        return application::EventPostResult::Accepted;
    }

    void closeRealtimeIngress() noexcept override {
        std::scoped_lock lock(mutex_);
        realtime_.reset();
    }

    void closeCriticalIngress() noexcept override {
        {
            std::scoped_lock lock(mutex_);
            closed_ = true;
        }
        condition_.notify_all();
    }

    [[nodiscard]] std::optional<application::ApplicationEvent> wait() {
        std::unique_lock lock(mutex_);
        condition_.wait(lock,
                        [this] { return closed_ || !critical_.empty() || realtime_.has_value(); });
        if (!critical_.empty()) {
            application::ApplicationEvent event = std::move(critical_.front());
            critical_.pop_front();
            return event;
        }
        if (realtime_.has_value()) {
            application::ApplicationEvent event = std::move(*realtime_);
            realtime_.reset();
            return event;
        }
        return std::nullopt;
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<application::ApplicationEvent> critical_;
    std::optional<application::ApplicationEvent> realtime_;
    bool closed_ = false;
};

struct AnalysisOperation final {
    AnalysisRequest request;
    std::weak_ptr<application::IApplicationEventSink> events;
    std::atomic<bool> canceled = false;

    AnalysisOperation(AnalysisRequest requestValue,
                      std::weak_ptr<application::IApplicationEventSink> eventsValue)
        : request(std::move(requestValue)), events(std::move(eventsValue)) {}
};

void post(const std::weak_ptr<application::IApplicationEventSink>& events,
          application::ApplicationEvent event,
          const bool realtime = false) noexcept {
    if (const std::shared_ptr<application::IApplicationEventSink> sink = events.lock()) {
        if (realtime) {
            static_cast<void>(sink->postRealtime(std::move(event)));
        } else {
            static_cast<void>(sink->postCritical(std::move(event)));
        }
    }
}

} // namespace

class AlignmentAnalysisService::Impl final {
public:
    Impl(platform::FrameBudget& frameBudget, const std::size_t queueCapacity)
        : queueCapacity_(std::max<std::size_t>(1U, queueCapacity)),
          provider_(std::make_unique<MultiSourceFrameProvider>(frameBudget, 16U, true)),
          worker_([this] { run(); }) {}

    ~Impl() {
        shutdown();
    }

    [[nodiscard]] application::PortSubmitResult
    submit(AnalysisRequest request,
           const std::shared_ptr<application::IApplicationEventSink>& events) {
        if (!events || jobId(request).value == 0U) {
            return application::PortSubmitResult::Closed;
        }
        const auto operation = std::make_shared<AnalysisOperation>(
            std::move(request), std::weak_ptr<application::IApplicationEventSink>{events});
        {
            std::scoped_lock lock(mutex_);
            if (closed_) {
                return application::PortSubmitResult::Closed;
            }
            if (queue_.size() >= queueCapacity_) {
                return application::PortSubmitResult::Busy;
            }
            queue_.push_back(operation);
        }
        condition_.notify_one();
        return application::PortSubmitResult::Accepted;
    }

    void cancel(const application::AlignmentAnalysisJobId requestedJobId) noexcept {
        std::shared_ptr<AnalysisOperation> queued;
        std::shared_ptr<AnalysisOperation> active;
        {
            std::scoped_lock lock(mutex_);
            const auto found =
                std::find_if(queue_.begin(), queue_.end(), [requestedJobId](const auto& item) {
                    return jobId(item->request) == requestedJobId;
                });
            if (found != queue_.end()) {
                queued = std::move(*found);
                queue_.erase(found);
                queued->canceled.store(true, std::memory_order_release);
            }
            if (active_ && jobId(active_->request) == requestedJobId) {
                active = active_;
                active->canceled.store(true, std::memory_order_release);
            }
        }
        if (active) {
            provider_->cancel(context(active->request));
        }
        if (queued) {
            postCanceled(*queued, application::CancellationReason::UserRequested);
        }
    }

private:
    void postCanceled(const AnalysisOperation& operation,
                      const application::CancellationReason reason) const noexcept {
        post(operation.events,
             application::ApplicationEvent{application::AlignmentAnalysisCanceled{
                 .jobId = jobId(operation.request),
                 .context = context(operation.request),
                 .kind = kind(operation.request),
                 .reason = reason,
             }});
    }

    void postFailed(const AnalysisOperation& operation, domain::MediaError error) const noexcept {
        error.requestId = context(operation.request).request.requestId;
        post(operation.events,
             application::ApplicationEvent{application::AlignmentAnalysisFailed{
                 .jobId = jobId(operation.request),
                 .context = context(operation.request),
                 .kind = kind(operation.request),
                 .error = std::move(error),
             }});
    }

    [[nodiscard]] bool waitForOpen(const std::shared_ptr<AnalysisOperation>& operation,
                                   const std::shared_ptr<EventQueue>& queue) {
        for (;;) {
            const std::optional<application::ApplicationEvent> event = queue->wait();
            if (!event.has_value()) {
                return false;
            }
            const auto* terminal = std::get_if<application::RequestTerminal>(&*event);
            if (terminal == nullptr) {
                continue;
            }
            if (const auto* failed = std::get_if<application::RequestFailed>(terminal)) {
                postFailed(*operation, failed->error);
                return false;
            }
            if (const auto* canceled = std::get_if<application::RequestCanceled>(terminal)) {
                postCanceled(*operation, canceled->reason);
                return false;
            }
            return true;
        }
    }

    void waitForAnalysis(const std::shared_ptr<AnalysisOperation>& operation,
                         const std::shared_ptr<EventQueue>& queue) {
        std::vector<application::GlobalOffsetEstimate> estimates;
        std::vector<application::SequenceAlignmentResult> sequenceResults;
        bool hasPayload = false;
        for (;;) {
            const std::optional<application::ApplicationEvent> event = queue->wait();
            if (!event.has_value()) {
                postFailed(*operation, serviceError("The analysis event stream closed."));
                return;
            }
            if (std::holds_alternative<application::AlignmentAnalysisProgress>(*event)) {
                post(operation->events, *event, true);
                continue;
            }
            if (const auto* result = std::get_if<application::AlignmentEstimated>(&*event)) {
                estimates = result->estimates;
                hasPayload = true;
                continue;
            }
            if (const auto* result = std::get_if<application::SequenceAlignmentAnalyzed>(&*event)) {
                sequenceResults = result->results;
                hasPayload = true;
                continue;
            }
            const auto* terminal = std::get_if<application::RequestTerminal>(&*event);
            if (terminal == nullptr) {
                continue;
            }
            if (const auto* failed = std::get_if<application::RequestFailed>(terminal)) {
                postFailed(*operation, failed->error);
                return;
            }
            if (const auto* canceled = std::get_if<application::RequestCanceled>(terminal)) {
                postCanceled(*operation, canceled->reason);
                return;
            }
            if (!hasPayload) {
                postFailed(*operation,
                           serviceError("Analysis completed without a result payload."));
                return;
            }
            post(operation->events,
                 application::ApplicationEvent{application::AlignmentAnalysisCompleted{
                     .jobId = jobId(operation->request),
                     .context = context(operation->request),
                     .kind = kind(operation->request),
                     .estimates = std::move(estimates),
                     .sequenceResults = std::move(sequenceResults),
                 }});
            return;
        }
    }

    void execute(const std::shared_ptr<AnalysisOperation>& operation) {
        if (operation->canceled.load(std::memory_order_acquire)) {
            postCanceled(*operation, application::CancellationReason::UserRequested);
            return;
        }
        post(operation->events,
             application::ApplicationEvent{application::AlignmentAnalysisStarted{
                 .jobId = jobId(operation->request),
                 .context = context(operation->request),
                 .kind = kind(operation->request),
                 .totalFrames = totalFrames(operation->request),
             }});

        const auto queue = std::make_shared<EventQueue>();
        const application::FrameProviderOpenRequest open{
            .context = context(operation->request),
            .sources =
                std::visit([](const auto& value) { return value.sources; }, operation->request),
            .canonicalSourceId = std::visit(
                [](const auto& value) { return value.canonicalSourceId; }, operation->request),
            .timeline =
                *std::visit([](const auto& value) { return value.timeline; }, operation->request),
        };
        if (provider_->submit(open, queue) != application::PortSubmitResult::Accepted) {
            postFailed(*operation, serviceError("The analysis decoder rejected source opening."));
            return;
        }
        if (!waitForOpen(operation, queue)) {
            return;
        }
        const application::PortSubmitResult admitted = std::visit(
            [this, &queue](const auto& request) { return provider_->submit(request, queue); },
            operation->request);
        if (admitted != application::PortSubmitResult::Accepted) {
            postFailed(*operation, serviceError("The analysis decoder rejected the job."));
            return;
        }
        waitForAnalysis(operation, queue);
    }

    void run() {
        static_cast<void>(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL));
        for (;;) {
            std::shared_ptr<AnalysisOperation> operation;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] { return closed_ || !queue_.empty(); });
                if (queue_.empty()) {
                    if (closed_) {
                        return;
                    }
                    continue;
                }
                operation = std::move(queue_.front());
                queue_.pop_front();
                active_ = operation;
            }
            try {
                const bool hasSourcesAndTimeline = std::visit(
                    [](const auto& request) {
                        return request.sources.size() >= 2U && request.timeline.has_value();
                    },
                    operation->request);
                if (!hasSourcesAndTimeline) {
                    postFailed(*operation,
                               serviceError("The analysis job is missing source metadata."));
                } else {
                    execute(operation);
                }
            } catch (...) {
                postFailed(*operation, serviceError("Unexpected analysis service exception."));
            }
            {
                std::scoped_lock lock(mutex_);
                if (active_ == operation) {
                    active_.reset();
                }
            }
        }
    }

    void shutdown() noexcept {
        std::deque<std::shared_ptr<AnalysisOperation>> queued;
        std::shared_ptr<AnalysisOperation> active;
        {
            std::scoped_lock lock(mutex_);
            if (closed_) {
                return;
            }
            closed_ = true;
            queued = std::move(queue_);
            active = active_;
            if (active) {
                active->canceled.store(true, std::memory_order_release);
            }
        }
        for (const auto& operation : queued) {
            postCanceled(*operation, application::CancellationReason::Shutdown);
        }
        if (active) {
            provider_->cancel(context(active->request));
        }
        condition_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        provider_.reset();
    }

    std::size_t queueCapacity_;
    std::unique_ptr<MultiSourceFrameProvider> provider_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<std::shared_ptr<AnalysisOperation>> queue_;
    std::shared_ptr<AnalysisOperation> active_;
    bool closed_ = false;
    std::thread worker_;
};

AlignmentAnalysisService::AlignmentAnalysisService(platform::FrameBudget& frameBudget,
                                                   const std::size_t queueCapacity)
    : impl_(std::make_unique<Impl>(frameBudget, queueCapacity)) {}

AlignmentAnalysisService::~AlignmentAnalysisService() = default;

application::PortSubmitResult
AlignmentAnalysisService::submit(const application::AlignmentEstimateRequest& request,
                                 std::shared_ptr<application::IApplicationEventSink> events) {
    return impl_->submit(AnalysisRequest{request}, events);
}

application::PortSubmitResult
AlignmentAnalysisService::submit(const application::SequenceAlignmentRequest& request,
                                 std::shared_ptr<application::IApplicationEventSink> events) {
    return impl_->submit(AnalysisRequest{request}, events);
}

void AlignmentAnalysisService::cancel(
    const application::AlignmentAnalysisJobId jobIdValue) noexcept {
    impl_->cancel(jobIdValue);
}

} // namespace dvs::media
