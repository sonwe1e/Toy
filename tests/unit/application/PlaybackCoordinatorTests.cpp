#include "dvs/application/PlaybackCoordinator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace dvs::application {
namespace {

using namespace std::chrono_literals;

class TestFrameResource final : public IFrameResource {};

class FakeMediaProbe final : public IMediaProbe {
public:
    [[nodiscard]] PortSubmitResult submit(const MediaProbeRequest& request,
                                          std::shared_ptr<IApplicationEventSink> events) override {
        if (!events) {
            return PortSubmitResult::Closed;
        }
        PortSubmitResult result = PortSubmitResult::Accepted;
        {
            std::scoped_lock lock(mutex_);
            result = request.sourceRole == domain::SourceRole::kA ? sourceAResult_ : sourceBResult_;
            if (result == PortSubmitResult::Accepted) {
                requests_.push_back(CapturedRequest{
                    .request = request,
                    .events = events,
                });
            }
        }
        condition_.notify_all();
        return result;
    }

    void cancel(const RequestContext& context) noexcept override {
        {
            std::scoped_lock lock(mutex_);
            canceled_.push_back(context);
        }
        condition_.notify_all();
    }

    void setSubmitResult(const domain::SourceRole sourceRole, const PortSubmitResult result) {
        std::scoped_lock lock(mutex_);
        if (sourceRole == domain::SourceRole::kA) {
            sourceAResult_ = result;
        } else {
            sourceBResult_ = result;
        }
    }

    [[nodiscard]] bool waitForRequestCount(const std::size_t count) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, 5s, [this, count] { return requests_.size() >= count; });
    }

    [[nodiscard]] bool waitForCancelCount(const std::size_t count) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, 5s, [this, count] { return canceled_.size() >= count; });
    }

    [[nodiscard]] std::optional<MediaProbeRequest> request(const std::size_t index) const {
        std::scoped_lock lock(mutex_);
        if (index >= requests_.size()) {
            return std::nullopt;
        }
        return requests_[index].request;
    }

    [[nodiscard]] std::vector<RequestContext> canceledContexts() const {
        std::scoped_lock lock(mutex_);
        return canceled_;
    }

    [[nodiscard]] std::shared_ptr<IApplicationEventSink>
    lockEventSink(const std::size_t index) const {
        const std::optional<CapturedRequest> captured = capturedRequest(index);
        return captured.has_value() ? captured->events.lock() : nullptr;
    }

    [[nodiscard]] bool postCompleted(const std::size_t index,
                                     domain::MediaDescriptor descriptor) const {
        const std::optional<CapturedRequest> captured = capturedRequest(index);
        const std::shared_ptr<IApplicationEventSink> events =
            captured.has_value() ? captured->events.lock() : nullptr;
        return events && events->postCritical(ApplicationEvent{ProbeCompleted{
                             .context = captured->request.context,
                             .sourceRole = captured->request.sourceRole,
                             .descriptor = std::move(descriptor),
                         }}) == EventPostResult::Accepted;
    }

    [[nodiscard]] bool postSucceeded(const std::size_t index) const {
        const std::optional<CapturedRequest> captured = capturedRequest(index);
        const std::shared_ptr<IApplicationEventSink> events =
            captured.has_value() ? captured->events.lock() : nullptr;
        return events && events->postCritical(ApplicationEvent{RequestTerminal{RequestSucceeded{
                             .context = EventContext{captured->request.context},
                         }}}) == EventPostResult::Accepted;
    }

    [[nodiscard]] bool postFailed(const std::size_t index, domain::MediaError error) const {
        const std::optional<CapturedRequest> captured = capturedRequest(index);
        const std::shared_ptr<IApplicationEventSink> events =
            captured.has_value() ? captured->events.lock() : nullptr;
        return events && events->postCritical(ApplicationEvent{RequestTerminal{RequestFailed{
                             .context = EventContext{captured->request.context},
                             .error = std::move(error),
                         }}}) == EventPostResult::Accepted;
    }

private:
    struct CapturedRequest final {
        MediaProbeRequest request;
        std::weak_ptr<IApplicationEventSink> events;
    };

    [[nodiscard]] std::optional<CapturedRequest> capturedRequest(const std::size_t index) const {
        std::scoped_lock lock(mutex_);
        if (index >= requests_.size()) {
            return std::nullopt;
        }
        return requests_[index];
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<CapturedRequest> requests_;
    std::vector<RequestContext> canceled_;
    PortSubmitResult sourceAResult_ = PortSubmitResult::Accepted;
    PortSubmitResult sourceBResult_ = PortSubmitResult::Accepted;
};

class FakeDeadlineScheduler final : public IDeadlineScheduler {
public:
    [[nodiscard]] PortSubmitResult
    schedule(const DeadlineRequest& request,
             std::shared_ptr<IApplicationEventSink> events) override {
        if (!events) {
            return PortSubmitResult::Closed;
        }
        {
            std::scoped_lock lock(mutex_);
            scheduled_.push_back(ScheduledDeadline{
                .request = request,
                .events = events,
            });
        }
        condition_.notify_all();
        return submitResult_;
    }

    [[nodiscard]] bool cancel(const std::uint64_t timerId) noexcept override {
        {
            std::scoped_lock lock(mutex_);
            canceled_.push_back(timerId);
        }
        condition_.notify_all();
        return cancelResult_;
    }

    [[nodiscard]] bool waitForScheduleCount(const std::size_t count) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, 5s, [this, count] { return scheduled_.size() >= count; });
    }

    [[nodiscard]] bool waitForCancelCount(const std::size_t count) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, 5s, [this, count] { return canceled_.size() >= count; });
    }

    [[nodiscard]] std::optional<DeadlineRequest> request(const std::size_t index) const {
        std::scoped_lock lock(mutex_);
        if (index >= scheduled_.size()) {
            return std::nullopt;
        }
        return scheduled_[index].request;
    }

    [[nodiscard]] std::size_t scheduleCount() const {
        std::scoped_lock lock(mutex_);
        return scheduled_.size();
    }

    [[nodiscard]] std::size_t cancelCount() const {
        std::scoped_lock lock(mutex_);
        return canceled_.size();
    }

    [[nodiscard]] bool fire(const std::size_t index) const {
        const std::optional<ScheduledDeadline> scheduled = deadline(index);
        const auto events = scheduled.has_value() ? scheduled->events.lock() : nullptr;
        return events && events->postCritical(ApplicationEvent{DeadlineElapsed{
                             .context = scheduled->request.context,
                             .timerId = scheduled->request.timerId,
                         }}) == EventPostResult::Accepted;
    }

private:
    struct ScheduledDeadline final {
        DeadlineRequest request;
        std::weak_ptr<IApplicationEventSink> events;
    };

    [[nodiscard]] std::optional<ScheduledDeadline> deadline(const std::size_t index) const {
        std::scoped_lock lock(mutex_);
        if (index >= scheduled_.size()) {
            return std::nullopt;
        }
        return scheduled_[index];
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<ScheduledDeadline> scheduled_;
    std::vector<std::uint64_t> canceled_;
    PortSubmitResult submitResult_ = PortSubmitResult::Accepted;
    bool cancelResult_ = true;
};

class FakeSteadyClock final : public ISteadyClock {
public:
    explicit FakeSteadyClock(const std::chrono::steady_clock::time_point now =
                                 std::chrono::steady_clock::time_point{std::chrono::seconds{123}})
        : now_(now) {}

    [[nodiscard]] std::chrono::steady_clock::time_point now() const noexcept override {
        std::scoped_lock lock(mutex_);
        return now_;
    }

    void set(const std::chrono::steady_clock::time_point now) noexcept {
        std::scoped_lock lock(mutex_);
        now_ = now;
    }

    void advance(const std::chrono::steady_clock::duration amount) noexcept {
        std::scoped_lock lock(mutex_);
        now_ += amount;
    }

private:
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point now_;
};

class FakeFrameProvider final : public IFrameProvider {
public:
    [[nodiscard]] PortSubmitResult submit(const FrameProviderOpenRequest& request,
                                          std::shared_ptr<IApplicationEventSink> events) override {
        if (!events) {
            return PortSubmitResult::Closed;
        }
        {
            std::scoped_lock lock(mutex_);
            openRequests_.push_back(request);
            events_ = std::move(events);
        }
        condition_.notify_all();
        return PortSubmitResult::Accepted;
    }

    [[nodiscard]] PortSubmitResult submit(const FrameRequest& request,
                                          std::shared_ptr<IApplicationEventSink> events) override {
        if (!events) {
            return PortSubmitResult::Closed;
        }
        {
            std::scoped_lock lock(mutex_);
            frameRequests_.push_back(request);
            events_ = std::move(events);
        }
        condition_.notify_all();
        return PortSubmitResult::Accepted;
    }

    [[nodiscard]] PortSubmitResult submit(const FrameProviderCloseRequest& request,
                                          std::shared_ptr<IApplicationEventSink> events) override {
        if (!events) {
            return PortSubmitResult::Closed;
        }
        {
            std::scoped_lock lock(mutex_);
            closeRequests_.push_back(request);
            events_ = std::move(events);
        }
        condition_.notify_all();
        return PortSubmitResult::Accepted;
    }

    void cancel(const PlaybackRequestContext& context) noexcept override {
        {
            std::scoped_lock lock(mutex_);
            canceledContexts_.push_back(context);
        }
        condition_.notify_all();
    }

    [[nodiscard]] bool waitForOpenRequestCount(const std::size_t count) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(
            lock, 5s, [this, count] { return openRequests_.size() >= count; });
    }

    [[nodiscard]] bool waitForFrameRequestCount(const std::size_t count) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(
            lock, 5s, [this, count] { return frameRequests_.size() >= count; });
    }

    [[nodiscard]] bool waitForCloseRequestCount(const std::size_t count) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(
            lock, 5s, [this, count] { return closeRequests_.size() >= count; });
    }

    [[nodiscard]] bool waitForCancelCount(const std::size_t count) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(
            lock, 5s, [this, count] { return canceledContexts_.size() >= count; });
    }

    [[nodiscard]] std::optional<FrameProviderOpenRequest>
    openRequest(const std::size_t index = 0U) const {
        std::scoped_lock lock(mutex_);
        if (index >= openRequests_.size()) {
            return std::nullopt;
        }
        return openRequests_[index];
    }

    [[nodiscard]] std::optional<FrameRequest> frameRequest(const std::size_t index) const {
        std::scoped_lock lock(mutex_);
        if (index >= frameRequests_.size()) {
            return std::nullopt;
        }
        return frameRequests_[index];
    }

    [[nodiscard]] std::optional<FrameProviderCloseRequest>
    closeRequest(const std::size_t index = 0U) const {
        std::scoped_lock lock(mutex_);
        if (index >= closeRequests_.size()) {
            return std::nullopt;
        }
        return closeRequests_[index];
    }

    [[nodiscard]] std::vector<PlaybackRequestContext> canceledContexts() const {
        std::scoped_lock lock(mutex_);
        return canceledContexts_;
    }

    [[nodiscard]] std::size_t frameRequestCount() const {
        std::scoped_lock lock(mutex_);
        return frameRequests_.size();
    }

    [[nodiscard]] std::size_t openRequestCount() const {
        std::scoped_lock lock(mutex_);
        return openRequests_.size();
    }

    [[nodiscard]] bool postOpenSucceeded(const FrameProviderOpenRequest& request) const {
        const std::shared_ptr<IApplicationEventSink> events = eventSink();
        return events && events->postCritical(ApplicationEvent{RequestTerminal{RequestSucceeded{
                             .context = EventContext{request.context},
                         }}}) == EventPostResult::Accepted;
    }

    [[nodiscard]] bool postOpenFailed(const FrameProviderOpenRequest& request,
                                      domain::MediaError error) const {
        const std::shared_ptr<IApplicationEventSink> events = eventSink();
        return events && events->postCritical(ApplicationEvent{RequestTerminal{RequestFailed{
                             .context = EventContext{request.context},
                             .error = std::move(error),
                         }}}) == EventPostResult::Accepted;
    }

    [[nodiscard]] bool postFrameReady(const FrameRequest& request, FramePair pair) const {
        const std::shared_ptr<IApplicationEventSink> events = eventSink();
        return events && events->postCritical(ApplicationEvent{FramePairReady{
                             .context = request.context,
                             .pair = std::move(pair),
                         }}) == EventPostResult::Accepted;
    }

    [[nodiscard]] bool postFrameSucceeded(const FrameRequest& request) const {
        const std::shared_ptr<IApplicationEventSink> events = eventSink();
        return events && events->postCritical(ApplicationEvent{RequestTerminal{RequestSucceeded{
                             .context = EventContext{request.context},
                         }}}) == EventPostResult::Accepted;
    }

    [[nodiscard]] bool postFrameFailed(const FrameRequest& request,
                                       domain::MediaError error) const {
        const std::shared_ptr<IApplicationEventSink> events = eventSink();
        return events && events->postCritical(ApplicationEvent{RequestTerminal{RequestFailed{
                             .context = EventContext{request.context},
                             .error = std::move(error),
                         }}}) == EventPostResult::Accepted;
    }

    [[nodiscard]] bool postCloseSucceeded(const FrameProviderCloseRequest& request) const {
        const std::shared_ptr<IApplicationEventSink> events = eventSink();
        return events && events->postCritical(ApplicationEvent{RequestTerminal{RequestSucceeded{
                             .context = EventContext{request.context},
                         }}}) == EventPostResult::Accepted;
    }

private:
    [[nodiscard]] std::shared_ptr<IApplicationEventSink> eventSink() const {
        std::scoped_lock lock(mutex_);
        return events_.lock();
    }

    mutable std::mutex mutex_;
    mutable std::condition_variable condition_;
    std::weak_ptr<IApplicationEventSink> events_;
    std::vector<FrameProviderOpenRequest> openRequests_;
    std::vector<FrameRequest> frameRequests_;
    std::vector<FrameProviderCloseRequest> closeRequests_;
    std::vector<PlaybackRequestContext> canceledContexts_;
};

class FakeRenderChannel final : public IRenderChannel {
public:
    [[nodiscard]] RenderPublishResult publish(const FrameRequestContext& context,
                                              FramePair pair) noexcept override {
        {
            std::scoped_lock lock(mutex_);
            published_.push_back(PublishedPair{
                .context = context,
                .pair = std::move(pair),
            });
        }
        condition_.notify_all();
        return RenderPublishResult::Accepted;
    }

    void clear(const PlaybackRequestContext& context) noexcept override {
        {
            std::scoped_lock lock(mutex_);
            clearContexts_.push_back(context);
            for (PublishedPair& published : published_) {
                if (samePlaybackScope(published.context.playback, context)) {
                    published.invalidated = true;
                }
            }
        }
        condition_.notify_all();
    }

    [[nodiscard]] bool waitForPublishedCount(const std::size_t count) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, 5s, [this, count] { return published_.size() >= count; });
    }

    [[nodiscard]] bool waitForClearCount(const std::size_t count) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(
            lock, 5s, [this, count] { return clearContexts_.size() >= count; });
    }

    [[nodiscard]] std::vector<PlaybackRequestContext> clearContexts() const {
        std::scoped_lock lock(mutex_);
        return clearContexts_;
    }

    [[nodiscard]] std::size_t publishedCount() const {
        std::scoped_lock lock(mutex_);
        return published_.size();
    }

    [[nodiscard]] std::optional<FramePairPresented> presentation(const std::size_t index) const {
        std::scoped_lock lock(mutex_);
        if (index >= published_.size()) {
            return std::nullopt;
        }
        return FramePairPresented{
            .context = published_[index].context,
            .frameId = published_[index].pair.frameId(),
        };
    }

    [[nodiscard]] std::optional<FramePairPresented> consumePresentation(const std::size_t index) {
        std::scoped_lock lock(mutex_);
        if (index >= published_.size() || published_[index].invalidated ||
            published_[index].consumed) {
            return std::nullopt;
        }
        published_[index].consumed = true;
        return FramePairPresented{
            .context = published_[index].context,
            .frameId = published_[index].pair.frameId(),
        };
    }

private:
    [[nodiscard]] static bool samePlaybackScope(const PlaybackRequestContext& lhs,
                                                const PlaybackRequestContext& rhs) noexcept {
        return lhs.request.sessionId == rhs.request.sessionId &&
               lhs.request.sessionEpoch == rhs.request.sessionEpoch &&
               lhs.playbackGeneration == rhs.playbackGeneration;
    }

    struct PublishedPair final {
        FrameRequestContext context;
        FramePair pair;
        bool invalidated = false;
        bool consumed = false;
    };

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<PublishedPair> published_;
    std::vector<PlaybackRequestContext> clearContexts_;
};

class IdentityOnlyRenderChannel final : public IRenderChannel {
public:
    [[nodiscard]] RenderPublishResult publish(const FrameRequestContext& context,
                                              FramePair pair) noexcept override {
        {
            std::scoped_lock lock(mutex_);
            published_.push_back(PublishedIdentity{
                .context = context,
                .frameId = pair.frameId(),
            });
        }
        condition_.notify_all();
        return RenderPublishResult::Accepted;
    }

    void clear(const PlaybackRequestContext& context) noexcept override {
        std::scoped_lock lock(mutex_);
        for (PublishedIdentity& published : published_) {
            if (published.context.playback == context) {
                published.invalidated = true;
            }
        }
    }

    [[nodiscard]] bool waitForPublishedCount(const std::size_t count) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, 5s, [this, count] { return published_.size() >= count; });
    }

    [[nodiscard]] std::optional<FramePairPresented> presentation(const std::size_t index) const {
        std::scoped_lock lock(mutex_);
        if (index >= published_.size() || published_[index].invalidated) {
            return std::nullopt;
        }
        return FramePairPresented{
            .context = published_[index].context,
            .frameId = published_[index].frameId,
        };
    }

private:
    struct PublishedIdentity final {
        FrameRequestContext context;
        domain::FrameId frameId;
        bool invalidated = false;
    };

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<PublishedIdentity> published_;
};

class BlockingRenderChannel final : public IRenderChannel {
public:
    [[nodiscard]] RenderPublishResult publish(const FrameRequestContext&,
                                              FramePair) noexcept override {
        std::unique_lock lock(mutex_);
        publishBlocked_ = true;
        condition_.notify_all();
        condition_.wait(lock, [this] { return released_; });
        return RenderPublishResult::Accepted;
    }

    void clear(const PlaybackRequestContext&) noexcept override {}

    [[nodiscard]] bool waitUntilPublishBlocked() {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, 5s, [this] { return publishBlocked_; });
    }

    void release() {
        {
            std::scoped_lock lock(mutex_);
            released_ = true;
        }
        condition_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool publishBlocked_ = false;
    bool released_ = false;
};

[[nodiscard]] domain::RationalRate makeRate() {
    auto rate = domain::RationalRate::create(30, 1);
    EXPECT_TRUE(rate.hasValue());
    return std::move(rate).value();
}

[[nodiscard]] domain::MediaDescriptor makeDescriptor(const std::filesystem::path& path,
                                                     const domain::MediaExtent extent,
                                                     const std::int64_t frameCount = 12) {
    return domain::MediaDescriptor{
        .normalizedPath = path,
        .extent = extent,
        .frameRate = makeRate(),
        .frameCount =
            domain::FrameCountInfo{
                .value = frameCount,
                .origin = domain::FrameCountOrigin::kReported,
            },
        .duration = domain::MediaTime{frameCount * 1'000'000 / 30},
        .codecId = "h264",
        .pixelFormatId = "yuv420p",
        .bitDepth = 8,
        .decodeCapabilities =
            domain::DecodeCapabilities{
                .softwareDecode = true,
                .d3d11VaDecode = false,
            },
        .timingConfidence = domain::TimingConfidence::kDeclaredCfr,
        .sourceIdentity = std::nullopt,
    };
}

[[nodiscard]] domain::MediaDescriptor makeVfrDescriptor(const std::filesystem::path& path,
                                                        const domain::MediaExtent extent,
                                                        const std::int64_t frameCount,
                                                        const domain::MediaTime duration) {
    return domain::MediaDescriptor{
        .normalizedPath = path,
        .extent = extent,
        .frameRate = std::nullopt,
        .frameCount =
            domain::FrameCountInfo{
                .value = frameCount,
                .origin = domain::FrameCountOrigin::kIndexed,
            },
        .duration = duration,
        .codecId = "h264",
        .pixelFormatId = "yuv420p",
        .bitDepth = 8,
        .decodeCapabilities =
            domain::DecodeCapabilities{
                .softwareDecode = true,
                .d3d11VaDecode = false,
            },
        .timingConfidence = domain::TimingConfidence::kVariableFrameRate,
        .sourceIdentity = std::nullopt,
    };
}

[[nodiscard]] std::shared_ptr<const domain::FrameTimeline> makeVfrTimeline() {
    const auto result = domain::FrameTimeline::create({domain::MediaTime{0},
                                                       domain::MediaTime{10000},
                                                       domain::MediaTime{50000},
                                                       domain::MediaTime{69800}});
    if (!result.hasValue()) {
        return nullptr;
    }
    return std::make_shared<const domain::FrameTimeline>(std::move(result).value());
}

[[nodiscard]] FramePair makePair(const domain::FrameId frameId) {
    const FrameGeometry geometryA{
        .width = 320,
        .height = 180,
        .textureRegion = TextureRegion{},
    };
    const FrameGeometry geometryB{
        .width = 160,
        .height = 90,
        .textureRegion = TextureRegion{},
    };
    const auto frameA =
        FrameHandle::create(std::make_shared<const TestFrameResource>(), geometryA, 100);
    const auto frameB =
        FrameHandle::create(std::make_shared<const TestFrameResource>(), geometryB, 100);
    EXPECT_TRUE(frameA.has_value());
    EXPECT_TRUE(frameB.has_value());
    const auto pair = FramePair::create(frameId,
                                        domain::MediaTime{frameId.value() * 33'333},
                                        *frameA,
                                        domain::MediaTime{frameId.value() * 33'333},
                                        *frameB,
                                        domain::MediaTime{frameId.value() * 33'333},
                                        ActiveFrameSource::Direct);
    EXPECT_TRUE(pair.has_value());
    return *pair;
}

struct ObservableFramePair final {
    FramePair pair;
    std::weak_ptr<const IFrameResource> sourceA;
    std::weak_ptr<const IFrameResource> sourceB;
};

[[nodiscard]] ObservableFramePair makeObservablePair(const domain::FrameId frameId) {
    const FrameGeometry geometryA{
        .width = 320,
        .height = 180,
        .textureRegion = TextureRegion{},
    };
    const FrameGeometry geometryB{
        .width = 160,
        .height = 90,
        .textureRegion = TextureRegion{},
    };
    const auto resourceA = std::make_shared<const TestFrameResource>();
    const auto resourceB = std::make_shared<const TestFrameResource>();
    const auto frameA = FrameHandle::create(resourceA, geometryA, 100);
    const auto frameB = FrameHandle::create(resourceB, geometryB, 100);
    EXPECT_TRUE(frameA.has_value());
    EXPECT_TRUE(frameB.has_value());
    auto pair = FramePair::create(frameId,
                                  domain::MediaTime{frameId.value() * 33'333},
                                  *frameA,
                                  domain::MediaTime{frameId.value() * 33'333},
                                  *frameB,
                                  domain::MediaTime{frameId.value() * 33'333},
                                  ActiveFrameSource::Direct);
    EXPECT_TRUE(pair.has_value());
    return ObservableFramePair{
        .pair = std::move(*pair),
        .sourceA = resourceA,
        .sourceB = resourceB,
    };
}

[[nodiscard]] ApplicationEvent ignoredCriticalEvent() {
    return ApplicationEvent{DeadlineElapsed{
        .context =
            PlaybackRequestContext{
                .request =
                    RequestContext{
                        .sessionId = domain::SessionId{1},
                        .sessionEpoch = domain::SessionEpoch{1},
                        .requestId = domain::RequestId{1},
                    },
                .playbackGeneration = domain::PlaybackGeneration{1},
            },
        .timerId = 1U,
    }};
}

template <typename Predicate> [[nodiscard]] bool waitUntil(Predicate&& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
}

[[nodiscard]] std::vector<CommandTerminal>
waitForTerminals(const std::shared_ptr<PlaybackCoordinator>& coordinator, const std::size_t count) {
    std::vector<CommandTerminal> terminals;
    const bool completed = waitUntil([&coordinator, &terminals, count] {
        std::vector<CommandTerminal> next = coordinator->takeCompletedCommands();
        terminals.insert(terminals.end(), next.begin(), next.end());
        return terminals.size() >= count;
    });
    EXPECT_TRUE(completed);
    return terminals;
}

void presentPublished(const std::shared_ptr<PlaybackCoordinator>& coordinator,
                      const std::shared_ptr<FakeRenderChannel>& render,
                      const std::size_t index) {
    const auto presented = render->consumePresentation(index);
    ASSERT_TRUE(presented.has_value());
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{*presented}), EventPostResult::Accepted);
}

void openReady(const std::shared_ptr<PlaybackCoordinator>& coordinator,
               const std::shared_ptr<FakeFrameProvider>& provider,
               const std::shared_ptr<FakeRenderChannel>& render,
               const domain::CommandId commandId = domain::CommandId{1}) {
    const std::shared_ptr<const SessionSnapshot> initial = coordinator->snapshot();
    ASSERT_NE(initial, nullptr);
    ASSERT_EQ(
        coordinator->submit(OpenDirectSourcesCommand{
            .context =
                CommandContext{
                    .sessionId = initial->sessionId,
                    .sessionEpoch = initial->sessionEpoch,
                    .commandId = commandId,
                },
            .sourceA = makeDescriptor("a.mp4", domain::MediaExtent{.width = 320, .height = 180}),
            .sourceB = makeDescriptor("b.mp4", domain::MediaExtent{.width = 160, .height = 90}),
        }),
        PortSubmitResult::Accepted);
    ASSERT_TRUE(provider->waitForOpenRequestCount(1U));
    const std::optional<FrameProviderOpenRequest> open = provider->openRequest();
    ASSERT_TRUE(open.has_value());
    ASSERT_EQ(open->context.request.sessionEpoch, domain::SessionEpoch{1});
    ASSERT_EQ(open->context.playbackGeneration, domain::PlaybackGeneration{1});
    ASSERT_TRUE(provider->postOpenSucceeded(*open));

    ASSERT_TRUE(provider->waitForFrameRequestCount(1U));
    const std::optional<FrameRequest> frame = provider->frameRequest(0U);
    ASSERT_TRUE(frame.has_value());
    ASSERT_EQ(frame->frameId, domain::FrameId{0});
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(frame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(1U));
    EXPECT_TRUE(coordinator->takeCompletedCommands().empty());
    ASSERT_TRUE(provider->postFrameSucceeded(*frame));
    EXPECT_TRUE(coordinator->takeCompletedCommands().empty());
    presentPublished(coordinator, render, 0U);

    const std::vector<CommandTerminal> terminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminals.size(), 1U);
    EXPECT_EQ(terminals.front().context.commandId, commandId);
    EXPECT_EQ(terminals.front().outcome, CommandOutcome::Succeeded);
}

void openVfrReady(const std::shared_ptr<PlaybackCoordinator>& coordinator,
                  const std::shared_ptr<FakeMediaProbe>& probe,
                  const std::shared_ptr<FakeFrameProvider>& provider,
                  const std::shared_ptr<FakeRenderChannel>& render,
                  const std::shared_ptr<const domain::FrameTimeline>& timeline,
                  const domain::MediaDescriptor& descriptorA,
                  const domain::MediaDescriptor& descriptorB,
                  const domain::CommandId commandId = domain::CommandId{1}) {
    const std::shared_ptr<const SessionSnapshot> initial = coordinator->snapshot();
    ASSERT_NE(initial, nullptr);
    ASSERT_EQ(coordinator->submit(OpenSourcePathsCommand{
                  .context =
                      CommandContext{
                          .sessionId = initial->sessionId,
                          .sessionEpoch = initial->sessionEpoch,
                          .commandId = commandId,
                      },
                  .sourceAPath = "C:/media/a.mp4",
                  .sourceBPath = "C:/media/b.mp4",
              }),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(probe->waitForRequestCount(2U));
    const std::optional<MediaProbeRequest> requestA = probe->request(0U);
    const std::optional<MediaProbeRequest> requestB = probe->request(1U);
    ASSERT_TRUE(requestA.has_value());
    ASSERT_TRUE(requestB.has_value());

    // Source A is VFR: its probe must publish the shared runtime timeline alongside the
    // descriptor. The coordinator captures that timeline and shares it with the provider.
    const std::shared_ptr<IApplicationEventSink> sinkA = probe->lockEventSink(0U);
    ASSERT_NE(sinkA, nullptr);
    ASSERT_EQ(sinkA->postCritical(ApplicationEvent{ProbeCompleted{
                  .context = requestA->context,
                  .sourceRole = domain::SourceRole::kA,
                  .descriptor = descriptorA,
                  .timeline = timeline,
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(probe->postSucceeded(0U));
    // Source B is the equal-count CFR pair; it must not publish a runtime timeline.
    ASSERT_TRUE(probe->postCompleted(1U, descriptorB));
    ASSERT_TRUE(probe->postSucceeded(1U));

    ASSERT_TRUE(provider->waitForOpenRequestCount(1U));
    const std::optional<FrameProviderOpenRequest> open = provider->openRequest();
    ASSERT_TRUE(open.has_value());
    ASSERT_TRUE(domain::isVariableFrameRate(open->timeline));
    ASSERT_EQ(std::get<std::shared_ptr<const domain::FrameTimeline>>(open->timeline).get(),
              timeline.get());
    ASSERT_TRUE(provider->postOpenSucceeded(*open));
    ASSERT_TRUE(provider->waitForFrameRequestCount(1U));
    const std::optional<FrameRequest> frame = provider->frameRequest(0U);
    ASSERT_TRUE(frame.has_value());
    ASSERT_EQ(frame->frameId, domain::FrameId{0});
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(frame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(1U));
    ASSERT_TRUE(provider->postFrameSucceeded(*frame));
    presentPublished(coordinator, render, 0U);
    const std::vector<CommandTerminal> terminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminals.size(), 1U);
    EXPECT_EQ(terminals.front().context.commandId, commandId);
    EXPECT_EQ(terminals.front().outcome, CommandOutcome::Succeeded);
}

[[nodiscard]] std::shared_ptr<PlaybackCoordinator>
makeCoordinator(const std::shared_ptr<FakeFrameProvider>& provider,
                const std::shared_ptr<FakeRenderChannel>& render,
                std::shared_ptr<IMediaProbe> mediaProbe = std::make_shared<FakeMediaProbe>(),
                std::shared_ptr<IDeadlineScheduler> deadlineScheduler =
                    std::make_shared<FakeDeadlineScheduler>(),
                std::shared_ptr<ISteadyClock> clock = std::make_shared<FakeSteadyClock>()) {
    return PlaybackCoordinator::create(domain::SessionId{91},
                                       PlaybackCoordinator::Dependencies{
                                           .mediaProbe = std::move(mediaProbe),
                                           .directFrameProvider = provider,
                                           .deadlineScheduler = std::move(deadlineScheduler),
                                           .clock = std::move(clock),
                                           .renderChannel = render,
                                       });
}

void markGraphicsReady(const std::shared_ptr<PlaybackCoordinator>& coordinator,
                       const domain::DeviceGeneration generation = domain::DeviceGeneration{2}) {
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceReady{
                  .context = GraphicsEventContext{.deviceGeneration = generation},
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(waitUntil([&coordinator, generation] {
        const auto snapshot = coordinator->snapshot();
        return snapshot->graphicsReady && snapshot->deviceGeneration == generation;
    }));
}

[[nodiscard]] CommandContext commandContext(const std::shared_ptr<PlaybackCoordinator>& coordinator,
                                            const domain::CommandId commandId) {
    const auto snapshot = coordinator->snapshot();
    return CommandContext{
        .sessionId = snapshot->sessionId,
        .sessionEpoch = snapshot->sessionEpoch,
        .commandId = commandId,
    };
}

TEST(PlaybackCoordinatorTests, PlayUsesAbsoluteRationalCadenceAndSequentialPairs) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto clock = std::make_shared<FakeSteadyClock>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator =
        makeCoordinator(provider, render, std::make_shared<FakeMediaProbe>(), scheduler, clock);
    markGraphicsReady(coordinator);
    openReady(coordinator, provider, render);

    const auto ready = coordinator->snapshot();
    ASSERT_EQ(coordinator->submit(PlayCommand{
                  .context =
                      CommandContext{
                          .sessionId = ready->sessionId,
                          .sessionEpoch = ready->sessionEpoch,
                          .commandId = domain::CommandId{2},
                      },
              }),
              PortSubmitResult::Accepted);
    const auto playTerminal = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(playTerminal.size(), 1U);
    EXPECT_EQ(playTerminal.front().outcome, CommandOutcome::Succeeded);
    ASSERT_TRUE(scheduler->waitForScheduleCount(2U));
    const auto firstCadence = scheduler->request(1U);
    ASSERT_TRUE(firstCadence.has_value());
    EXPECT_EQ(firstCadence->due, clock->now() + 33'334us);
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPlaying);
    EXPECT_EQ(provider->frameRequestCount(), 1U);

    clock->set(firstCadence->due);
    ASSERT_TRUE(scheduler->fire(1U));
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const auto frame = provider->frameRequest(1U);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->frameId, domain::FrameId{1});
    EXPECT_EQ(frame->priority, FrameRequestPriority::Sequential);
    ASSERT_TRUE(waitUntil([&coordinator] {
        return coordinator->snapshot()->playbackState == domain::PlaybackState::kBuffering;
    }));

    ASSERT_TRUE(provider->postFrameSucceeded(*frame));
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(frame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(2U));
    presentPublished(coordinator, render, 1U);
    ASSERT_TRUE(scheduler->waitForScheduleCount(4U));
    ASSERT_TRUE(waitUntil(
        [&coordinator] { return coordinator->snapshot()->displayedFrame == domain::FrameId{1}; }));
    const auto secondCadence = scheduler->request(3U);
    ASSERT_TRUE(secondCadence.has_value());
    EXPECT_EQ(secondCadence->due, firstCadence->due + 33'333us);
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPlaying);
}

TEST(PlaybackCoordinatorTests, PauseBeforeCadenceCancelsRunAndRejectsTheStaleTick) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator =
        makeCoordinator(provider, render, std::make_shared<FakeMediaProbe>(), scheduler);
    markGraphicsReady(coordinator);
    openReady(coordinator, provider, render);

    ASSERT_EQ(coordinator->submit(
                  PlayCommand{.context = commandContext(coordinator, domain::CommandId{2})}),
              PortSubmitResult::Accepted);
    EXPECT_EQ(waitForTerminals(coordinator, 1U).front().outcome, CommandOutcome::Succeeded);
    ASSERT_TRUE(scheduler->waitForScheduleCount(2U));
    ASSERT_EQ(coordinator->submit(StepFramesCommand{
                  .context = commandContext(coordinator, domain::CommandId{3}),
                  .delta = 1,
              }),
              PortSubmitResult::Accepted);
    EXPECT_EQ(waitForTerminals(coordinator, 1U).front().outcome, CommandOutcome::Busy);
    ASSERT_EQ(coordinator->submit(
                  PauseCommand{.context = commandContext(coordinator, domain::CommandId{4})}),
              PortSubmitResult::Accepted);
    EXPECT_EQ(waitForTerminals(coordinator, 1U).front().outcome, CommandOutcome::Succeeded);
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPaused);
    EXPECT_FALSE(coordinator->snapshot()->requestedFrame.has_value());
    EXPECT_GE(scheduler->cancelCount(), 2U);

    ASSERT_TRUE(scheduler->fire(1U));
    markGraphicsReady(coordinator, domain::DeviceGeneration{3});
    EXPECT_EQ(provider->frameRequestCount(), 1U);
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
}

TEST(PlaybackCoordinatorTests, PauseDuringDecodeCancelsGenerationAndIgnoresLateResults) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto clock = std::make_shared<FakeSteadyClock>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator =
        makeCoordinator(provider, render, std::make_shared<FakeMediaProbe>(), scheduler, clock);
    markGraphicsReady(coordinator);
    openReady(coordinator, provider, render);

    ASSERT_EQ(coordinator->submit(
                  PlayCommand{.context = commandContext(coordinator, domain::CommandId{2})}),
              PortSubmitResult::Accepted);
    static_cast<void>(waitForTerminals(coordinator, 1U));
    ASSERT_TRUE(scheduler->waitForScheduleCount(2U));
    const auto cadence = scheduler->request(1U);
    ASSERT_TRUE(cadence.has_value());
    clock->set(cadence->due);
    ASSERT_TRUE(scheduler->fire(1U));
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const auto lateFrame = provider->frameRequest(1U);
    ASSERT_TRUE(lateFrame.has_value());

    ASSERT_EQ(coordinator->submit(
                  PauseCommand{.context = commandContext(coordinator, domain::CommandId{3})}),
              PortSubmitResult::Accepted);
    EXPECT_EQ(waitForTerminals(coordinator, 1U).front().outcome, CommandOutcome::Succeeded);
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPaused);
    EXPECT_FALSE(coordinator->snapshot()->requestedFrame.has_value());
    EXPECT_TRUE(provider->waitForCancelCount(1U));

    ASSERT_TRUE(provider->postFrameReady(*lateFrame, makePair(lateFrame->frameId)));
    ASSERT_TRUE(provider->postFrameSucceeded(*lateFrame));
    markGraphicsReady(coordinator, domain::DeviceGeneration{3});
    EXPECT_EQ(render->publishedCount(), 1U);
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
}

TEST(PlaybackCoordinatorTests, PauseAfterPublicationDrainsOnlyThatAtomicPair) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto clock = std::make_shared<FakeSteadyClock>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator =
        makeCoordinator(provider, render, std::make_shared<FakeMediaProbe>(), scheduler, clock);
    markGraphicsReady(coordinator);
    openReady(coordinator, provider, render);

    ASSERT_EQ(coordinator->submit(
                  PlayCommand{.context = commandContext(coordinator, domain::CommandId{2})}),
              PortSubmitResult::Accepted);
    static_cast<void>(waitForTerminals(coordinator, 1U));
    ASSERT_TRUE(scheduler->waitForScheduleCount(2U));
    const auto cadence = scheduler->request(1U);
    ASSERT_TRUE(cadence.has_value());
    clock->set(cadence->due);
    ASSERT_TRUE(scheduler->fire(1U));
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const auto frame = provider->frameRequest(1U);
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(frame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(2U));

    ASSERT_EQ(coordinator->submit(
                  PauseCommand{.context = commandContext(coordinator, domain::CommandId{3})}),
              PortSubmitResult::Accepted);
    EXPECT_EQ(waitForTerminals(coordinator, 1U).front().outcome, CommandOutcome::Succeeded);
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPaused);
    EXPECT_EQ(coordinator->snapshot()->requestedFrame, domain::FrameId{1});

    presentPublished(coordinator, render, 1U);
    ASSERT_TRUE(provider->postFrameSucceeded(*frame));
    ASSERT_TRUE(waitUntil([&coordinator] {
        const auto snapshot = coordinator->snapshot();
        return snapshot->displayedFrame == domain::FrameId{1} &&
               !snapshot->requestedFrame.has_value();
    }));
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPaused);
    EXPECT_EQ(scheduler->scheduleCount(), 3U);
    EXPECT_EQ(provider->frameRequestCount(), 2U);
}

TEST(PlaybackCoordinatorTests, SlowDecodeDropsCompletePairsAndKeepsOneRequestInFlight) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto clock = std::make_shared<FakeSteadyClock>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator =
        makeCoordinator(provider, render, std::make_shared<FakeMediaProbe>(), scheduler, clock);
    markGraphicsReady(coordinator);
    openReady(coordinator, provider, render);

    ASSERT_EQ(coordinator->submit(
                  PlayCommand{.context = commandContext(coordinator, domain::CommandId{2})}),
              PortSubmitResult::Accepted);
    static_cast<void>(waitForTerminals(coordinator, 1U));
    ASSERT_TRUE(scheduler->waitForScheduleCount(2U));
    const auto cadence = scheduler->request(1U);
    ASSERT_TRUE(cadence.has_value());
    clock->set(cadence->due + 166'666us);
    ASSERT_TRUE(scheduler->fire(1U));
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const auto firstPlaybackFrame = provider->frameRequest(1U);
    ASSERT_TRUE(firstPlaybackFrame.has_value());
    EXPECT_EQ(firstPlaybackFrame->frameId, domain::FrameId{6});
    EXPECT_EQ(provider->frameRequestCount(), 2U);

    clock->advance(100ms);
    ASSERT_TRUE(
        provider->postFrameReady(*firstPlaybackFrame, makePair(firstPlaybackFrame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(2U));
    ASSERT_TRUE(provider->postFrameSucceeded(*firstPlaybackFrame));
    presentPublished(coordinator, render, 1U);
    ASSERT_TRUE(provider->waitForFrameRequestCount(3U));
    const auto skippedPlaybackFrame = provider->frameRequest(2U);
    ASSERT_TRUE(skippedPlaybackFrame.has_value());
    EXPECT_EQ(skippedPlaybackFrame->frameId, domain::FrameId{9});
    EXPECT_EQ(skippedPlaybackFrame->priority, FrameRequestPriority::Sequential);
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{6});
    EXPECT_EQ(coordinator->snapshot()->requestedFrame, domain::FrameId{9});
}

TEST(PlaybackCoordinatorTests, FinalFrameAutoPausesAndPlayFromEndRestartsAtZero) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto clock = std::make_shared<FakeSteadyClock>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator =
        makeCoordinator(provider, render, std::make_shared<FakeMediaProbe>(), scheduler, clock);
    markGraphicsReady(coordinator);
    openReady(coordinator, provider, render);

    ASSERT_EQ(coordinator->submit(
                  PlayCommand{.context = commandContext(coordinator, domain::CommandId{2})}),
              PortSubmitResult::Accepted);
    static_cast<void>(waitForTerminals(coordinator, 1U));
    ASSERT_TRUE(scheduler->waitForScheduleCount(2U));
    const auto cadence = scheduler->request(1U);
    ASSERT_TRUE(cadence.has_value());
    clock->set(cadence->due + 1s);
    ASSERT_TRUE(scheduler->fire(1U));
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const auto finalFrame = provider->frameRequest(1U);
    ASSERT_TRUE(finalFrame.has_value());
    EXPECT_EQ(finalFrame->frameId, domain::FrameId{11});
    ASSERT_TRUE(provider->postFrameSucceeded(*finalFrame));
    ASSERT_TRUE(provider->postFrameReady(*finalFrame, makePair(finalFrame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(2U));
    presentPublished(coordinator, render, 1U);
    ASSERT_TRUE(waitUntil(
        [&coordinator] { return coordinator->snapshot()->displayedFrame == domain::FrameId{11}; }));
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPaused);

    ASSERT_EQ(coordinator->submit(
                  PlayCommand{.context = commandContext(coordinator, domain::CommandId{3})}),
              PortSubmitResult::Accepted);
    EXPECT_EQ(waitForTerminals(coordinator, 1U).front().outcome, CommandOutcome::Succeeded);
    // Restart-from-end commits frame 0 as an atomic submission without arming a one-frame
    // cadence, so the replay request is already in flight once Play succeeds.
    ASSERT_TRUE(provider->waitForFrameRequestCount(3U));
    const auto replayFrame = provider->frameRequest(2U);
    ASSERT_TRUE(replayFrame.has_value());
    EXPECT_EQ(replayFrame->frameId, domain::FrameId{0});
    EXPECT_EQ(replayFrame->priority, FrameRequestPriority::Sequential);
    ASSERT_TRUE(scheduler->waitForScheduleCount(4U));

    ASSERT_TRUE(provider->postFrameSucceeded(*replayFrame));
    ASSERT_TRUE(provider->postFrameReady(*replayFrame, makePair(replayFrame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(3U));
    presentPublished(coordinator, render, 2U);
    // After frame 0 commits, the coordinator re-anchors on the absolute Source A timeline at
    // frame 0 and schedules frame 1 at the next canonical interval.
    ASSERT_TRUE(scheduler->waitForScheduleCount(5U));
    const auto replayNextCadence = scheduler->request(4U);
    ASSERT_TRUE(replayNextCadence.has_value());
    EXPECT_EQ(replayNextCadence->due, clock->now() + 33'334us);
    clock->set(replayNextCadence->due);
    ASSERT_TRUE(scheduler->fire(4U));
    ASSERT_TRUE(provider->waitForFrameRequestCount(4U));
    const auto nextFrame = provider->frameRequest(3U);
    ASSERT_TRUE(nextFrame.has_value());
    EXPECT_EQ(nextFrame->frameId, domain::FrameId{1});
    EXPECT_EQ(nextFrame->priority, FrameRequestPriority::Sequential);
}

TEST(PlaybackCoordinatorTests, PlaybackTimeoutPausesAndRejectsLateProviderResults) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto clock = std::make_shared<FakeSteadyClock>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator =
        makeCoordinator(provider, render, std::make_shared<FakeMediaProbe>(), scheduler, clock);
    markGraphicsReady(coordinator);
    openReady(coordinator, provider, render);

    ASSERT_EQ(coordinator->submit(
                  PlayCommand{.context = commandContext(coordinator, domain::CommandId{2})}),
              PortSubmitResult::Accepted);
    static_cast<void>(waitForTerminals(coordinator, 1U));
    ASSERT_TRUE(scheduler->waitForScheduleCount(2U));
    const auto cadence = scheduler->request(1U);
    ASSERT_TRUE(cadence.has_value());
    clock->set(cadence->due);
    ASSERT_TRUE(scheduler->fire(1U));
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    ASSERT_TRUE(scheduler->waitForScheduleCount(3U));
    const auto lateFrame = provider->frameRequest(1U);
    ASSERT_TRUE(lateFrame.has_value());

    ASSERT_TRUE(scheduler->fire(2U));
    ASSERT_TRUE(waitUntil([&coordinator] {
        const auto snapshot = coordinator->snapshot();
        return snapshot->lastError.has_value() &&
               snapshot->lastError->code == domain::MediaErrorCode::kFramePresentationTimedOut;
    }));
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPaused);
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
    EXPECT_FALSE(coordinator->snapshot()->requestedFrame.has_value());

    ASSERT_TRUE(provider->postFrameReady(*lateFrame, makePair(lateFrame->frameId)));
    ASSERT_TRUE(provider->postFrameSucceeded(*lateFrame));
    markGraphicsReady(coordinator, domain::DeviceGeneration{3});
    EXPECT_EQ(render->publishedCount(), 1U);
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
}

TEST(PlaybackCoordinatorTests, PlaybackProviderFailureKeepsLastCommittedPair) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto clock = std::make_shared<FakeSteadyClock>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator =
        makeCoordinator(provider, render, std::make_shared<FakeMediaProbe>(), scheduler, clock);
    markGraphicsReady(coordinator);
    openReady(coordinator, provider, render);

    ASSERT_EQ(coordinator->submit(
                  PlayCommand{.context = commandContext(coordinator, domain::CommandId{2})}),
              PortSubmitResult::Accepted);
    static_cast<void>(waitForTerminals(coordinator, 1U));
    ASSERT_TRUE(scheduler->waitForScheduleCount(2U));
    const auto cadence = scheduler->request(1U);
    ASSERT_TRUE(cadence.has_value());
    clock->set(cadence->due);
    ASSERT_TRUE(scheduler->fire(1U));
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const auto frame = provider->frameRequest(1U);
    ASSERT_TRUE(frame.has_value());
    const domain::MediaError failure =
        domain::makeMediaError(domain::MediaErrorCode::kMediaDecodeFailed,
                               domain::MediaOperation::kMediaDecode,
                               domain::SourceRole::kPair,
                               true,
                               "Synthetic sequential decode failure.");
    ASSERT_TRUE(provider->postFrameFailed(*frame, failure));
    ASSERT_TRUE(waitUntil([&coordinator] {
        const auto snapshot = coordinator->snapshot();
        return snapshot->lastError.has_value() &&
               snapshot->lastError->code == domain::MediaErrorCode::kMediaDecodeFailed;
    }));
    EXPECT_EQ(coordinator->snapshot()->sessionState, domain::SessionState::kReady);
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPaused);
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
    EXPECT_FALSE(coordinator->snapshot()->requestedFrame.has_value());
    EXPECT_EQ(render->publishedCount(), 1U);
}

TEST(PlaybackCoordinatorTests, GraphicsLossInvalidatesPublishedPlaybackPair) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto clock = std::make_shared<FakeSteadyClock>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator =
        makeCoordinator(provider, render, std::make_shared<FakeMediaProbe>(), scheduler, clock);
    markGraphicsReady(coordinator);
    openReady(coordinator, provider, render);

    ASSERT_EQ(coordinator->submit(
                  PlayCommand{.context = commandContext(coordinator, domain::CommandId{2})}),
              PortSubmitResult::Accepted);
    static_cast<void>(waitForTerminals(coordinator, 1U));
    ASSERT_TRUE(scheduler->waitForScheduleCount(2U));
    const auto cadence = scheduler->request(1U);
    ASSERT_TRUE(cadence.has_value());
    clock->set(cadence->due);
    ASSERT_TRUE(scheduler->fire(1U));
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const auto frame = provider->frameRequest(1U);
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(frame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(2U));
    const auto latePresentation = render->presentation(1U);
    ASSERT_TRUE(latePresentation.has_value());

    const domain::MediaError deviceLost =
        domain::makeMediaError(domain::MediaErrorCode::kGraphicsDeviceLost,
                               domain::MediaOperation::kGraphicsInitialization,
                               domain::SourceRole::kPair,
                               true,
                               "Device loss during continuous playback.");
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceLost{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{2}},
                  .error = deviceLost,
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(waitUntil([&coordinator] {
        const auto snapshot = coordinator->snapshot();
        return !snapshot->graphicsReady && snapshot->lastError.has_value() &&
               snapshot->lastError->code == domain::MediaErrorCode::kGraphicsDeviceLost;
    }));
    ASSERT_TRUE(render->waitForClearCount(1U));
    EXPECT_FALSE(render->consumePresentation(1U).has_value());
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPaused);
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});

    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{*latePresentation}),
              EventPostResult::Accepted);
    ASSERT_TRUE(provider->postFrameSucceeded(*frame));
    markGraphicsReady(coordinator, domain::DeviceGeneration{3});
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
}

TEST(PlaybackCoordinatorTests, ShutdownCancelsAndInvalidatesPublishedPlaybackPair) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto clock = std::make_shared<FakeSteadyClock>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    auto coordinator =
        makeCoordinator(provider, render, std::make_shared<FakeMediaProbe>(), scheduler, clock);
    markGraphicsReady(coordinator);
    openReady(coordinator, provider, render);

    ASSERT_EQ(coordinator->submit(
                  PlayCommand{.context = commandContext(coordinator, domain::CommandId{2})}),
              PortSubmitResult::Accepted);
    static_cast<void>(waitForTerminals(coordinator, 1U));
    ASSERT_TRUE(scheduler->waitForScheduleCount(2U));
    const auto cadence = scheduler->request(1U);
    ASSERT_TRUE(cadence.has_value());
    clock->set(cadence->due);
    ASSERT_TRUE(scheduler->fire(1U));
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const auto frame = provider->frameRequest(1U);
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(frame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(2U));

    coordinator.reset();
    ASSERT_TRUE(render->waitForClearCount(1U));
    EXPECT_FALSE(render->consumePresentation(1U).has_value());
    EXPECT_TRUE(provider->waitForCancelCount(1U));
    EXPECT_GE(scheduler->cancelCount(), 2U);
}

TEST(PlaybackCoordinatorTests, AdapterCallbackLeaseCannotOwnCoordinatorDuringShutdown) {
    const auto probe = std::make_shared<FakeMediaProbe>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    auto coordinator = makeCoordinator(provider, render, probe);
    ASSERT_NE(coordinator, nullptr);

    const auto initial = coordinator->snapshot();
    ASSERT_NE(initial, nullptr);
    ASSERT_EQ(coordinator->submit(OpenSourcePathsCommand{
                  .context =
                      CommandContext{
                          .sessionId = initial->sessionId,
                          .sessionEpoch = initial->sessionEpoch,
                          .commandId = domain::CommandId{1},
                      },
                  .sourceAPath = "C:/media/a.mp4",
                  .sourceBPath = "C:/media/b.mp4",
              }),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(probe->waitForRequestCount(2U));
    std::shared_ptr<IApplicationEventSink> callbackLease = probe->lockEventSink(0U);
    ASSERT_NE(callbackLease, nullptr);

    std::mutex workerMutex;
    std::condition_variable workerCondition;
    bool workerHoldsLease = false;
    bool releaseWorker = false;
    std::atomic postResult{EventPostResult::Accepted};
    std::thread adapterWorker{[events = std::move(callbackLease),
                               &workerMutex,
                               &workerCondition,
                               &workerHoldsLease,
                               &releaseWorker,
                               &postResult] {
        {
            std::unique_lock lock(workerMutex);
            workerHoldsLease = true;
            workerCondition.notify_all();
            workerCondition.wait(lock, [&releaseWorker] { return releaseWorker; });
        }
        postResult.store(events->postCritical(ignoredCriticalEvent()), std::memory_order_release);
    }};

    {
        std::unique_lock lock(workerMutex);
        EXPECT_TRUE(
            workerCondition.wait_for(lock, 5s, [&workerHoldsLease] { return workerHoldsLease; }));
    }
    const std::weak_ptr<PlaybackCoordinator> weakCoordinator = coordinator;
    coordinator.reset();
    EXPECT_TRUE(weakCoordinator.expired());

    {
        std::scoped_lock lock(workerMutex);
        releaseWorker = true;
    }
    workerCondition.notify_all();
    adapterWorker.join();
    EXPECT_EQ(postResult.load(std::memory_order_acquire), EventPostResult::Closed);
}

TEST(PlaybackCoordinatorTests, TracksGraphicsReadinessByIndependentDeviceGeneration) {
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render);
    ASSERT_NE(coordinator, nullptr);
    ASSERT_FALSE(coordinator->snapshot()->graphicsReady);

    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceReady{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{2}},
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(waitUntil([&coordinator] {
        const auto snapshot = coordinator->snapshot();
        return snapshot->graphicsReady && snapshot->deviceGeneration == domain::DeviceGeneration{2};
    }));

    const domain::MediaError deviceLost =
        domain::makeMediaError(domain::MediaErrorCode::kGraphicsDeviceLost,
                               domain::MediaOperation::kGraphicsInitialization,
                               domain::SourceRole::kPair,
                               true,
                               "ID3D11Device::GetDeviceRemovedReason returned 0x887A0005.");
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceLost{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{3}},
                  .error = deviceLost,
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(waitUntil([&coordinator] {
        const auto snapshot = coordinator->snapshot();
        return !snapshot->graphicsReady && snapshot->lastError.has_value() &&
               snapshot->deviceGeneration == domain::DeviceGeneration{3};
    }));

    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceReady{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{2}},
              }}),
              EventPostResult::Accepted);
    const domain::MediaError generationBarrier =
        domain::makeMediaError(domain::MediaErrorCode::kGraphicsDeviceLost,
                               domain::MediaOperation::kGraphicsInitialization,
                               domain::SourceRole::kPair,
                               true,
                               "The current device generation remains unavailable.");
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceLost{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{3}},
                  .error = generationBarrier,
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(waitUntil([&coordinator, &generationBarrier] {
        const auto snapshot = coordinator->snapshot();
        return snapshot->lastError.has_value() &&
               snapshot->lastError->technicalDetail == generationBarrier.technicalDetail;
    }));
    EXPECT_FALSE(coordinator->snapshot()->graphicsReady);
    EXPECT_EQ(coordinator->snapshot()->deviceGeneration, domain::DeviceGeneration{3});

    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceReady{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{4}},
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(waitUntil([&coordinator] {
        const auto snapshot = coordinator->snapshot();
        return snapshot->graphicsReady && !snapshot->lastError.has_value() &&
               snapshot->deviceGeneration == domain::DeviceGeneration{4};
    }));
}

TEST(PlaybackCoordinatorTests, OpensPathsAfterBThenAProbePayloadsAndTerminals) {
    const auto probe = std::make_shared<FakeMediaProbe>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render, probe);
    ASSERT_NE(coordinator, nullptr);

    const auto initial = coordinator->snapshot();
    ASSERT_EQ(coordinator->submit(OpenSourcePathsCommand{
                  .context =
                      CommandContext{
                          .sessionId = initial->sessionId,
                          .sessionEpoch = initial->sessionEpoch,
                          .commandId = domain::CommandId{1},
                      },
                  .sourceAPath = "C:/media/a.mp4",
                  .sourceBPath = "C:/media/b.mp4",
              }),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(probe->waitForRequestCount(2U));
    const auto requestA = probe->request(0U);
    const auto requestB = probe->request(1U);
    ASSERT_TRUE(requestA.has_value());
    ASSERT_TRUE(requestB.has_value());
    EXPECT_EQ(requestA->sourceRole, domain::SourceRole::kA);
    EXPECT_EQ(requestB->sourceRole, domain::SourceRole::kB);
    EXPECT_NE(requestA->context.requestId, requestB->context.requestId);

    ASSERT_TRUE(probe->postCompleted(
        1U, makeDescriptor("C:/media/b.mp4", domain::MediaExtent{.width = 160, .height = 90})));
    ASSERT_TRUE(probe->postSucceeded(1U));
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceReady{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{1}},
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(waitUntil([&coordinator] { return coordinator->snapshot()->graphicsReady; }));
    EXPECT_EQ(provider->openRequestCount(), 0U);

    ASSERT_TRUE(probe->postCompleted(
        0U, makeDescriptor("C:/media/a.mp4", domain::MediaExtent{.width = 320, .height = 180})));
    ASSERT_TRUE(probe->postSucceeded(0U));
    ASSERT_TRUE(provider->waitForOpenRequestCount(1U));
    const auto open = provider->openRequest();
    ASSERT_TRUE(open.has_value());
    EXPECT_EQ(open->sourceA.normalizedPath, std::filesystem::path{"C:/media/a.mp4"});
    EXPECT_EQ(open->sourceB.normalizedPath, std::filesystem::path{"C:/media/b.mp4"});
    EXPECT_EQ(open->context.request.sessionEpoch, domain::SessionEpoch{1});
    ASSERT_TRUE(provider->postOpenSucceeded(*open));
    ASSERT_TRUE(provider->waitForFrameRequestCount(1U));
    const auto frame = provider->frameRequest(0U);
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(domain::FrameId{0})));
    ASSERT_TRUE(render->waitForPublishedCount(1U));
    ASSERT_TRUE(provider->postFrameSucceeded(*frame));
    presentPublished(coordinator, render, 0U);
    const auto terminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminals.size(), 1U);
    EXPECT_EQ(terminals.front().context.commandId, domain::CommandId{1});
    EXPECT_EQ(terminals.front().outcome, CommandOutcome::Succeeded);
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
}

TEST(PlaybackCoordinatorTests, AcceptsSamePathAndOpensProviderExactlyOnceAfterAThenB) {
    const auto probe = std::make_shared<FakeMediaProbe>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render, probe);
    const auto initial = coordinator->snapshot();
    ASSERT_EQ(coordinator->submit(OpenSourcePathsCommand{
                  .context =
                      CommandContext{
                          .sessionId = initial->sessionId,
                          .sessionEpoch = initial->sessionEpoch,
                          .commandId = domain::CommandId{1},
                      },
                  .sourceAPath = "C:/media/same.mp4",
                  .sourceBPath = "C:/media/same.mp4",
              }),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(probe->waitForRequestCount(2U));
    const auto descriptorA =
        makeDescriptor("C:/media/same.mp4", domain::MediaExtent{.width = 320, .height = 180});
    const auto descriptorB =
        makeDescriptor("C:/media/same.mp4", domain::MediaExtent{.width = 320, .height = 180});
    ASSERT_TRUE(probe->postCompleted(0U, descriptorA));
    ASSERT_TRUE(probe->postSucceeded(0U));
    ASSERT_TRUE(probe->postCompleted(1U, descriptorB));
    ASSERT_TRUE(probe->postSucceeded(1U));
    ASSERT_TRUE(provider->waitForOpenRequestCount(1U));

    ASSERT_TRUE(probe->postCompleted(0U, descriptorA));
    ASSERT_TRUE(probe->postSucceeded(0U));
    ASSERT_TRUE(probe->postCompleted(1U, descriptorB));
    ASSERT_TRUE(probe->postSucceeded(1U));
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceReady{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{2}},
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(waitUntil([&coordinator] {
        return coordinator->snapshot()->deviceGeneration == domain::DeviceGeneration{2};
    }));
    EXPECT_EQ(provider->openRequestCount(), 1U);
    const auto open = provider->openRequest();
    ASSERT_TRUE(open.has_value());
    EXPECT_EQ(open->sourceA.normalizedPath, open->sourceB.normalizedPath);
    ASSERT_TRUE(provider->postOpenSucceeded(*open));
    ASSERT_TRUE(provider->waitForFrameRequestCount(1U));
    const auto frame = provider->frameRequest(0U);
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(domain::FrameId{0})));
    ASSERT_TRUE(render->waitForPublishedCount(1U));
    ASSERT_TRUE(provider->postFrameSucceeded(*frame));
    presentPublished(coordinator, render, 0U);
    EXPECT_EQ(waitForTerminals(coordinator, 1U).front().outcome, CommandOutcome::Succeeded);
}

TEST(PlaybackCoordinatorTests, ProbeFailureCancelsSiblingAndPreservesReadyReplacementSession) {
    const auto probe = std::make_shared<FakeMediaProbe>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render, probe);
    openReady(coordinator, provider, render);
    const auto ready = coordinator->snapshot();

    ASSERT_EQ(coordinator->submit(OpenSourcePathsCommand{
                  .context =
                      CommandContext{
                          .sessionId = ready->sessionId,
                          .sessionEpoch = ready->sessionEpoch,
                          .commandId = domain::CommandId{2},
                      },
                  .sourceAPath = "C:/media/replacement-a.mp4",
                  .sourceBPath = "C:/media/replacement-b.mp4",
              }),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(probe->waitForRequestCount(2U));
    const domain::MediaError failure =
        domain::makeMediaError(domain::MediaErrorCode::kMediaProbeFailed,
                               domain::MediaOperation::kMediaProbe,
                               domain::SourceRole::kA,
                               true,
                               "Source A could not be inspected.");
    ASSERT_TRUE(probe->postFailed(0U, failure));
    const auto terminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminals.size(), 1U);
    EXPECT_EQ(terminals.front().outcome, CommandOutcome::Failed);
    ASSERT_TRUE(probe->waitForCancelCount(2U));
    const auto canceled = probe->canceledContexts();
    const auto requestB = probe->request(1U);
    ASSERT_TRUE(requestB.has_value());
    EXPECT_NE(std::find(canceled.begin(), canceled.end(), requestB->context), canceled.end());

    const auto after = coordinator->snapshot();
    EXPECT_EQ(after->sessionState, domain::SessionState::kReady);
    EXPECT_EQ(after->displayedFrame, domain::FrameId{0});
    EXPECT_EQ(provider->openRequestCount(), 1U);
    EXPECT_TRUE(render->clearContexts().empty());

    ASSERT_TRUE(
        probe->postCompleted(1U,
                             makeDescriptor("C:/media/replacement-b.mp4",
                                            domain::MediaExtent{.width = 160, .height = 90})));
    ASSERT_TRUE(probe->postSucceeded(1U));
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceReady{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{3}},
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(waitUntil([&coordinator] {
        return coordinator->snapshot()->deviceGeneration == domain::DeviceGeneration{3};
    }));
    EXPECT_EQ(provider->openRequestCount(), 1U);
}

TEST(PlaybackCoordinatorTests, IncompatibleProbedPairBecomesInvalidWithoutOpeningProvider) {
    const auto probe = std::make_shared<FakeMediaProbe>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render, probe);
    const auto initial = coordinator->snapshot();
    ASSERT_EQ(coordinator->submit(OpenSourcePathsCommand{
                  .context =
                      CommandContext{
                          .sessionId = initial->sessionId,
                          .sessionEpoch = initial->sessionEpoch,
                          .commandId = domain::CommandId{1},
                      },
                  .sourceAPath = "C:/media/a.mp4",
                  .sourceBPath = "C:/media/b.mp4",
              }),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(probe->waitForRequestCount(2U));
    ASSERT_TRUE(probe->postCompleted(
        0U, makeDescriptor("C:/media/a.mp4", domain::MediaExtent{.width = 320, .height = 180})));
    ASSERT_TRUE(probe->postSucceeded(0U));
    ASSERT_TRUE(probe->postCompleted(
        1U, makeDescriptor("C:/media/b.mp4", domain::MediaExtent{.width = 160, .height = 90}, 11)));
    ASSERT_TRUE(probe->postSucceeded(1U));
    const auto terminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminals.size(), 1U);
    ASSERT_TRUE(terminals.front().error.has_value());
    EXPECT_EQ(terminals.front().error->code, domain::MediaErrorCode::kSourceFrameCountMismatch);
    EXPECT_EQ(provider->openRequestCount(), 0U);
    EXPECT_EQ(coordinator->snapshot()->sessionState, domain::SessionState::kInvalid);
}

TEST(PlaybackCoordinatorTests, ProbeAdmissionFailureCancelsAcceptedSiblingExactlyOnce) {
    const auto probe = std::make_shared<FakeMediaProbe>();
    probe->setSubmitResult(domain::SourceRole::kB, PortSubmitResult::Busy);
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render, probe);
    const auto initial = coordinator->snapshot();
    ASSERT_EQ(coordinator->submit(OpenSourcePathsCommand{
                  .context =
                      CommandContext{
                          .sessionId = initial->sessionId,
                          .sessionEpoch = initial->sessionEpoch,
                          .commandId = domain::CommandId{1},
                      },
                  .sourceAPath = "C:/media/a.mp4",
                  .sourceBPath = "C:/media/b.mp4",
              }),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(probe->waitForRequestCount(1U));
    const auto terminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminals.size(), 1U);
    EXPECT_EQ(terminals.front().outcome, CommandOutcome::Busy);
    EXPECT_EQ(provider->openRequestCount(), 0U);
    EXPECT_EQ(coordinator->snapshot()->sessionState, domain::SessionState::kError);
}

TEST(PlaybackCoordinatorTests, PostValidationOpenFailureKeepsPriorPixelsButIsNotReady) {
    const auto probe = std::make_shared<FakeMediaProbe>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render, probe);
    openReady(coordinator, provider, render);
    const auto ready = coordinator->snapshot();
    ASSERT_EQ(render->publishedCount(), 1U);

    ASSERT_EQ(coordinator->submit(OpenSourcePathsCommand{
                  .context =
                      CommandContext{
                          .sessionId = ready->sessionId,
                          .sessionEpoch = ready->sessionEpoch,
                          .commandId = domain::CommandId{2},
                      },
                  .sourceAPath = "C:/media/new-a.mp4",
                  .sourceBPath = "C:/media/new-b.mp4",
              }),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(probe->waitForRequestCount(2U));
    ASSERT_TRUE(probe->postCompleted(
        0U,
        makeDescriptor("C:/media/new-a.mp4", domain::MediaExtent{.width = 320, .height = 180})));
    ASSERT_TRUE(probe->postSucceeded(0U));
    ASSERT_TRUE(probe->postCompleted(
        1U, makeDescriptor("C:/media/new-b.mp4", domain::MediaExtent{.width = 160, .height = 90})));
    ASSERT_TRUE(probe->postSucceeded(1U));
    ASSERT_TRUE(provider->waitForOpenRequestCount(2U));
    EXPECT_TRUE(render->clearContexts().empty());
    EXPECT_EQ(coordinator->snapshot()->sessionState, domain::SessionState::kLoading);
    const auto replacementOpen = provider->openRequest(1U);
    ASSERT_TRUE(replacementOpen.has_value());
    const domain::MediaError openFailure =
        domain::makeMediaError(domain::MediaErrorCode::kMediaDecodeFailed,
                               domain::MediaOperation::kMediaDecode,
                               domain::SourceRole::kPair,
                               true,
                               "The replacement decoder could not be opened.");
    ASSERT_TRUE(provider->postOpenFailed(*replacementOpen, openFailure));
    const auto terminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminals.size(), 1U);
    EXPECT_EQ(terminals.front().outcome, CommandOutcome::Failed);
    EXPECT_EQ(coordinator->snapshot()->sessionState, domain::SessionState::kError);
    EXPECT_FALSE(coordinator->snapshot()->displayedFrame.has_value());
    EXPECT_EQ(render->publishedCount(), 1U);
    EXPECT_TRUE(render->clearContexts().empty());
}

TEST(PlaybackCoordinatorTests, RequiresProviderTerminalAndPresentationInEitherOrder) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator =
        makeCoordinator(provider, render, std::make_shared<FakeMediaProbe>(), scheduler);
    const auto initial = coordinator->snapshot();
    ASSERT_EQ(
        coordinator->submit(OpenDirectSourcesCommand{
            .context =
                CommandContext{
                    .sessionId = initial->sessionId,
                    .sessionEpoch = initial->sessionEpoch,
                    .commandId = domain::CommandId{1},
                },
            .sourceA = makeDescriptor("a.mp4", domain::MediaExtent{.width = 320, .height = 180}),
            .sourceB = makeDescriptor("b.mp4", domain::MediaExtent{.width = 160, .height = 90}),
        }),
        PortSubmitResult::Accepted);
    ASSERT_TRUE(provider->waitForOpenRequestCount(1U));
    const auto open = provider->openRequest();
    ASSERT_TRUE(open.has_value());
    ASSERT_TRUE(provider->postOpenSucceeded(*open));
    ASSERT_TRUE(provider->waitForFrameRequestCount(1U));
    const auto frame = provider->frameRequest(0U);
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(domain::FrameId{0})));
    ASSERT_TRUE(render->waitForPublishedCount(1U));
    ASSERT_TRUE(scheduler->waitForScheduleCount(1U));

    presentPublished(coordinator, render, 0U);
    const auto loading = coordinator->snapshot();
    ASSERT_EQ(coordinator->submit(FirstFrameCommand{
                  .context =
                      CommandContext{
                          .sessionId = loading->sessionId,
                          .sessionEpoch = loading->sessionEpoch,
                          .commandId = domain::CommandId{2},
                      },
              }),
              PortSubmitResult::Accepted);
    const auto busy = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(busy.size(), 1U);
    EXPECT_EQ(busy.front().context.commandId, domain::CommandId{2});
    EXPECT_EQ(busy.front().outcome, CommandOutcome::Busy);
    EXPECT_FALSE(coordinator->snapshot()->displayedFrame.has_value());

    ASSERT_TRUE(provider->postFrameSucceeded(*frame));
    const auto completed = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(completed.size(), 1U);
    EXPECT_EQ(completed.front().context.commandId, domain::CommandId{1});
    EXPECT_EQ(completed.front().outcome, CommandOutcome::Succeeded);
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
    EXPECT_TRUE(scheduler->waitForCancelCount(1U));

    ASSERT_TRUE(scheduler->fire(0U));
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceReady{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{4}},
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(waitUntil([&coordinator] {
        return coordinator->snapshot()->deviceGeneration == domain::DeviceGeneration{4};
    }));
    EXPECT_TRUE(coordinator->takeCompletedCommands().empty());
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
}

TEST(PlaybackCoordinatorTests, AcceptsProviderSuccessBeforeTheFramePair) {
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render);
    const auto initial = coordinator->snapshot();
    ASSERT_EQ(
        coordinator->submit(OpenDirectSourcesCommand{
            .context =
                CommandContext{
                    .sessionId = initial->sessionId,
                    .sessionEpoch = initial->sessionEpoch,
                    .commandId = domain::CommandId{1},
                },
            .sourceA = makeDescriptor("a.mp4", domain::MediaExtent{.width = 320, .height = 180}),
            .sourceB = makeDescriptor("b.mp4", domain::MediaExtent{.width = 160, .height = 90}),
        }),
        PortSubmitResult::Accepted);
    ASSERT_TRUE(provider->waitForOpenRequestCount(1U));
    const auto open = provider->openRequest();
    ASSERT_TRUE(open.has_value());
    ASSERT_TRUE(provider->postOpenSucceeded(*open));
    ASSERT_TRUE(provider->waitForFrameRequestCount(1U));
    const auto frame = provider->frameRequest(0U);
    ASSERT_TRUE(frame.has_value());

    ASSERT_TRUE(provider->postFrameSucceeded(*frame));
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(frame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(1U));
    EXPECT_TRUE(coordinator->takeCompletedCommands().empty());
    presentPublished(coordinator, render, 0U);
    const auto terminal = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminal.size(), 1U);
    EXPECT_EQ(terminal.front().outcome, CommandOutcome::Succeeded);
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
}

TEST(PlaybackCoordinatorTests, ExactDeadlineBoundsAProviderThatNeverPublishesAPair) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator =
        makeCoordinator(provider, render, std::make_shared<FakeMediaProbe>(), scheduler);
    const auto initial = coordinator->snapshot();
    ASSERT_EQ(
        coordinator->submit(OpenDirectSourcesCommand{
            .context =
                CommandContext{
                    .sessionId = initial->sessionId,
                    .sessionEpoch = initial->sessionEpoch,
                    .commandId = domain::CommandId{1},
                },
            .sourceA = makeDescriptor("a.mp4", domain::MediaExtent{.width = 320, .height = 180}),
            .sourceB = makeDescriptor("b.mp4", domain::MediaExtent{.width = 160, .height = 90}),
        }),
        PortSubmitResult::Accepted);
    ASSERT_TRUE(provider->waitForOpenRequestCount(1U));
    const auto open = provider->openRequest();
    ASSERT_TRUE(open.has_value());
    ASSERT_TRUE(provider->postOpenSucceeded(*open));
    ASSERT_TRUE(provider->waitForFrameRequestCount(1U));
    ASSERT_TRUE(scheduler->waitForScheduleCount(1U));

    ASSERT_TRUE(scheduler->fire(0U));
    const auto terminal = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminal.size(), 1U);
    ASSERT_TRUE(terminal.front().error.has_value());
    EXPECT_EQ(terminal.front().error->code, domain::MediaErrorCode::kFramePresentationTimedOut);
    EXPECT_EQ(coordinator->snapshot()->sessionState, domain::SessionState::kError);
    EXPECT_FALSE(coordinator->snapshot()->displayedFrame.has_value());
    EXPECT_TRUE(provider->waitForCancelCount(1U));
}

TEST(PlaybackCoordinatorTests, PresentationTimeoutKeepsPreviousFrameAndRejectsLateAck) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto clock = std::make_shared<FakeSteadyClock>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator =
        makeCoordinator(provider, render, std::make_shared<FakeMediaProbe>(), scheduler, clock);
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceReady{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{2}},
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(waitUntil([&coordinator] { return coordinator->snapshot()->graphicsReady; }));
    openReady(coordinator, provider, render);

    const auto ready = coordinator->snapshot();
    ASSERT_EQ(coordinator->submit(SeekFrameCommand{
                  .context =
                      CommandContext{
                          .sessionId = ready->sessionId,
                          .sessionEpoch = ready->sessionEpoch,
                          .commandId = domain::CommandId{2},
                      },
                  .frameId = domain::FrameId{7},
              }),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const auto frame = provider->frameRequest(1U);
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(frame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(2U));
    ASSERT_TRUE(provider->postFrameSucceeded(*frame));
    ASSERT_TRUE(scheduler->waitForScheduleCount(2U));
    const auto lateAcknowledgement = render->presentation(1U);
    ASSERT_TRUE(lateAcknowledgement.has_value());
    const auto deadline = scheduler->request(1U);
    ASSERT_TRUE(deadline.has_value());
    EXPECT_EQ(deadline->due, clock->now() + 5s);
    EXPECT_TRUE(coordinator->takeCompletedCommands().empty());

    ASSERT_TRUE(scheduler->fire(1U));
    const auto timedOut = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(timedOut.size(), 1U);
    ASSERT_TRUE(timedOut.front().error.has_value());
    EXPECT_EQ(timedOut.front().error->code, domain::MediaErrorCode::kFramePresentationTimedOut);
    EXPECT_EQ(timedOut.front().error->operation, domain::MediaOperation::kFramePresentation);
    EXPECT_EQ(coordinator->snapshot()->sessionState, domain::SessionState::kReady);
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
    ASSERT_TRUE(render->waitForClearCount(1U));
    EXPECT_FALSE(render->consumePresentation(1U).has_value());
    ASSERT_EQ(render->clearContexts().front(), frame->context.playback);

    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{*lateAcknowledgement}),
              EventPostResult::Accepted);
    const domain::MediaError deviceLost =
        domain::makeMediaError(domain::MediaErrorCode::kGraphicsDeviceLost,
                               domain::MediaOperation::kGraphicsInitialization,
                               domain::SourceRole::kPair,
                               true,
                               "Device generation barrier after the late acknowledgement.");
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceLost{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{2}},
                  .error = deviceLost,
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(waitUntil([&coordinator] {
        const auto snapshot = coordinator->snapshot();
        return snapshot->lastError.has_value() &&
               snapshot->lastError->code == domain::MediaErrorCode::kGraphicsDeviceLost;
    }));
    EXPECT_TRUE(coordinator->takeCompletedCommands().empty());
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
}

TEST(PlaybackCoordinatorTests, ShutdownInvalidatesAPublishedPendingExactPair) {
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    auto coordinator = makeCoordinator(provider, render);
    openReady(coordinator, provider, render);

    const auto ready = coordinator->snapshot();
    ASSERT_EQ(coordinator->submit(SeekFrameCommand{
                  .context =
                      CommandContext{
                          .sessionId = ready->sessionId,
                          .sessionEpoch = ready->sessionEpoch,
                          .commandId = domain::CommandId{2},
                      },
                  .frameId = domain::FrameId{7},
              }),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const auto frame = provider->frameRequest(1U);
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(frame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(2U));
    ASSERT_TRUE(render->presentation(1U).has_value());
    coordinator.reset();

    ASSERT_TRUE(render->waitForClearCount(1U));
    EXPECT_EQ(render->clearContexts().front(), frame->context.playback);
    EXPECT_FALSE(render->consumePresentation(1U).has_value());
}

TEST(PlaybackCoordinatorTests, GraphicsLossInvalidatesAPublishedPendingExactPair) {
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render);
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceReady{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{2}},
              }}),
              EventPostResult::Accepted);
    ASSERT_TRUE(waitUntil([&coordinator] { return coordinator->snapshot()->graphicsReady; }));
    openReady(coordinator, provider, render);

    const auto ready = coordinator->snapshot();
    ASSERT_EQ(coordinator->submit(SeekFrameCommand{
                  .context =
                      CommandContext{
                          .sessionId = ready->sessionId,
                          .sessionEpoch = ready->sessionEpoch,
                          .commandId = domain::CommandId{2},
                      },
                  .frameId = domain::FrameId{7},
              }),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const auto frame = provider->frameRequest(1U);
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(frame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(2U));

    const domain::MediaError deviceLost =
        domain::makeMediaError(domain::MediaErrorCode::kGraphicsDeviceLost,
                               domain::MediaOperation::kGraphicsInitialization,
                               domain::SourceRole::kPair,
                               true,
                               "Device loss while an exact pair awaited presentation.");
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{GraphicsDeviceLost{
                  .context = GraphicsEventContext{.deviceGeneration = domain::DeviceGeneration{2}},
                  .error = deviceLost,
              }}),
              EventPostResult::Accepted);

    const auto terminal = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminal.size(), 1U);
    EXPECT_EQ(terminal.front().outcome, CommandOutcome::Failed);
    ASSERT_TRUE(render->waitForClearCount(1U));
    EXPECT_EQ(render->clearContexts().front(), frame->context.playback);
    EXPECT_FALSE(render->consumePresentation(1U).has_value());
    EXPECT_FALSE(coordinator->snapshot()->graphicsReady);
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
}

TEST(PlaybackCoordinatorTests, ClampsAllEndpointCommandsForAOneFramePair) {
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render);
    const auto initial = coordinator->snapshot();
    ASSERT_EQ(
        coordinator->submit(OpenDirectSourcesCommand{
            .context =
                CommandContext{
                    .sessionId = initial->sessionId,
                    .sessionEpoch = initial->sessionEpoch,
                    .commandId = domain::CommandId{1},
                },
            .sourceA = makeDescriptor("a.mp4", domain::MediaExtent{.width = 320, .height = 180}, 1),
            .sourceB = makeDescriptor("b.mp4", domain::MediaExtent{.width = 160, .height = 90}, 1),
        }),
        PortSubmitResult::Accepted);
    ASSERT_TRUE(provider->waitForOpenRequestCount(1U));
    const auto open = provider->openRequest();
    ASSERT_TRUE(open.has_value());
    ASSERT_TRUE(provider->postOpenSucceeded(*open));
    ASSERT_TRUE(provider->waitForFrameRequestCount(1U));
    const auto openingFrame = provider->frameRequest(0U);
    ASSERT_TRUE(openingFrame.has_value());
    ASSERT_TRUE(provider->postFrameReady(*openingFrame, makePair(domain::FrameId{0})));
    ASSERT_TRUE(render->waitForPublishedCount(1U));
    ASSERT_TRUE(provider->postFrameSucceeded(*openingFrame));
    presentPublished(coordinator, render, 0U);
    EXPECT_EQ(waitForTerminals(coordinator, 1U).front().outcome, CommandOutcome::Succeeded);
    ASSERT_EQ(coordinator->snapshot()->canonicalFrameCount, 1U);

    const auto completeEndpoint = [&](PlaybackCommand command, const std::size_t requestIndex) {
        ASSERT_EQ(coordinator->submit(std::move(command)), PortSubmitResult::Accepted);
        ASSERT_TRUE(provider->waitForFrameRequestCount(requestIndex + 1U));
        const auto request = provider->frameRequest(requestIndex);
        ASSERT_TRUE(request.has_value());
        EXPECT_EQ(request->frameId, domain::FrameId{0});
        ASSERT_TRUE(provider->postFrameReady(*request, makePair(domain::FrameId{0})));
        ASSERT_TRUE(render->waitForPublishedCount(requestIndex + 1U));
        ASSERT_TRUE(provider->postFrameSucceeded(*request));
        presentPublished(coordinator, render, requestIndex);
        const auto terminal = waitForTerminals(coordinator, 1U);
        ASSERT_EQ(terminal.size(), 1U);
        EXPECT_EQ(terminal.front().outcome, CommandOutcome::Succeeded);
        EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{0});
    };

    const auto ready = coordinator->snapshot();
    completeEndpoint(
        StepFramesCommand{
            .context =
                CommandContext{
                    .sessionId = ready->sessionId,
                    .sessionEpoch = ready->sessionEpoch,
                    .commandId = domain::CommandId{2},
                },
            .delta = -1,
        },
        1U);
    completeEndpoint(
        StepFramesCommand{
            .context =
                CommandContext{
                    .sessionId = ready->sessionId,
                    .sessionEpoch = ready->sessionEpoch,
                    .commandId = domain::CommandId{3},
                },
            .delta = 1,
        },
        2U);
    completeEndpoint(
        FirstFrameCommand{
            .context =
                CommandContext{
                    .sessionId = ready->sessionId,
                    .sessionEpoch = ready->sessionEpoch,
                    .commandId = domain::CommandId{4},
                },
        },
        3U);
    completeEndpoint(
        LastFrameCommand{
            .context =
                CommandContext{
                    .sessionId = ready->sessionId,
                    .sessionEpoch = ready->sessionEpoch,
                    .commandId = domain::CommandId{5},
                },
        },
        4U);

    markGraphicsReady(coordinator);
    ASSERT_EQ(coordinator->submit(PlayCommand{
                  .context = commandContext(coordinator, domain::CommandId{6}),
              }),
              PortSubmitResult::Accepted);
    const auto rejectedPlay = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(rejectedPlay.size(), 1U);
    EXPECT_EQ(rejectedPlay.front().outcome, CommandOutcome::Failed);
    ASSERT_TRUE(rejectedPlay.front().error.has_value());
    EXPECT_EQ(rejectedPlay.front().error->code, domain::MediaErrorCode::kInvalidArgument);
    EXPECT_EQ(provider->frameRequestCount(), 5U);
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPaused);
}

TEST(PlaybackCoordinatorTests, OpensDirectSourcesOnlyAfterACompletePairAndProviderTerminal) {
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render);
    ASSERT_NE(coordinator, nullptr);

    openReady(coordinator, provider, render);

    const std::shared_ptr<const SessionSnapshot> snapshot = coordinator->snapshot();
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->isConsistent());
    EXPECT_EQ(snapshot->sessionState, domain::SessionState::kReady);
    EXPECT_EQ(snapshot->playbackState, domain::PlaybackState::kPaused);
    EXPECT_EQ(snapshot->activeFrameSource, ActiveFrameSource::Direct);
    EXPECT_EQ(snapshot->displayedFrame, domain::FrameId{0});
    EXPECT_FALSE(snapshot->requestedFrame.has_value());
    EXPECT_EQ(snapshot->canonicalFrameCount, 12U);
}

TEST(PlaybackCoordinatorTests, ReleasesCpuFrameResourcesAfterPresentationCommit) {
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<IdentityOnlyRenderChannel>();
    const auto coordinator = PlaybackCoordinator::create(
        domain::SessionId{91},
        PlaybackCoordinator::Dependencies{
            .mediaProbe = std::make_shared<FakeMediaProbe>(),
            .directFrameProvider = provider,
            .deadlineScheduler = std::make_shared<FakeDeadlineScheduler>(),
            .clock = std::make_shared<FakeSteadyClock>(),
            .renderChannel = render,
        });
    ASSERT_NE(coordinator, nullptr);

    const auto initial = coordinator->snapshot();
    ASSERT_EQ(
        coordinator->submit(OpenDirectSourcesCommand{
            .context =
                CommandContext{
                    .sessionId = initial->sessionId,
                    .sessionEpoch = initial->sessionEpoch,
                    .commandId = domain::CommandId{1},
                },
            .sourceA = makeDescriptor("a.mp4", domain::MediaExtent{.width = 320, .height = 180}),
            .sourceB = makeDescriptor("b.mp4", domain::MediaExtent{.width = 160, .height = 90}),
        }),
        PortSubmitResult::Accepted);
    ASSERT_TRUE(provider->waitForOpenRequestCount(1U));
    const auto open = provider->openRequest();
    ASSERT_TRUE(open.has_value());
    ASSERT_TRUE(provider->postOpenSucceeded(*open));
    ASSERT_TRUE(provider->waitForFrameRequestCount(1U));
    const auto frame = provider->frameRequest(0U);
    ASSERT_TRUE(frame.has_value());

    auto observable = makeObservablePair(frame->frameId);
    const std::weak_ptr<const IFrameResource> sourceA = observable.sourceA;
    const std::weak_ptr<const IFrameResource> sourceB = observable.sourceB;
    ASSERT_TRUE(provider->postFrameReady(*frame, std::move(observable.pair)));
    ASSERT_TRUE(render->waitForPublishedCount(1U));
    EXPECT_FALSE(sourceA.expired());
    EXPECT_FALSE(sourceB.expired());

    ASSERT_TRUE(provider->postFrameSucceeded(*frame));
    const auto presented = render->presentation(0U);
    ASSERT_TRUE(presented.has_value());
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{*presented}), EventPostResult::Accepted);
    const auto terminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminals.size(), 1U);
    EXPECT_EQ(terminals.front().outcome, CommandOutcome::Succeeded);

    EXPECT_TRUE(sourceA.expired());
    EXPECT_TRUE(sourceB.expired());
    const auto snapshot = coordinator->snapshot();
    EXPECT_EQ(snapshot->sessionState, domain::SessionState::kReady);
    EXPECT_EQ(snapshot->displayedFrame, frame->frameId);
}

TEST(PlaybackCoordinatorTests, SeekAdvancesGenerationCancelsOldScopeAndPublishesTheExactPair) {
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render);
    ASSERT_NE(coordinator, nullptr);
    openReady(coordinator, provider, render);

    const std::shared_ptr<const SessionSnapshot> beforeSeek = coordinator->snapshot();
    ASSERT_NE(beforeSeek, nullptr);
    ASSERT_EQ(coordinator->submit(SeekFrameCommand{
                  .context =
                      CommandContext{
                          .sessionId = beforeSeek->sessionId,
                          .sessionEpoch = beforeSeek->sessionEpoch,
                          .commandId = domain::CommandId{2},
                      },
                  .frameId = domain::FrameId{7},
              }),
              PortSubmitResult::Accepted);

    ASSERT_TRUE(provider->waitForCancelCount(1U));
    const std::vector<PlaybackRequestContext> canceled = provider->canceledContexts();
    ASSERT_EQ(canceled.size(), 1U);
    EXPECT_EQ(canceled.front().playbackGeneration, beforeSeek->playbackGeneration);
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const std::optional<FrameRequest> request = provider->frameRequest(1U);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->frameId, domain::FrameId{7});
    EXPECT_EQ(request->context.playback.playbackGeneration, domain::PlaybackGeneration{2});
    ASSERT_TRUE(provider->postFrameReady(*request, makePair(request->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(2U));
    ASSERT_TRUE(provider->postFrameSucceeded(*request));
    presentPublished(coordinator, render, 1U);

    const std::vector<CommandTerminal> terminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminals.size(), 1U);
    EXPECT_EQ(terminals.front().context.commandId, domain::CommandId{2});
    EXPECT_EQ(terminals.front().outcome, CommandOutcome::Succeeded);
    const std::shared_ptr<const SessionSnapshot> afterSeek = coordinator->snapshot();
    ASSERT_NE(afterSeek, nullptr);
    EXPECT_TRUE(afterSeek->isConsistent());
    EXPECT_EQ(afterSeek->displayedFrame, domain::FrameId{7});
    EXPECT_EQ(afterSeek->playbackGeneration, domain::PlaybackGeneration{2});
}

TEST(PlaybackCoordinatorTests, SuppressesDuplicateCommandsWithoutASecondProviderRequest) {
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render);
    ASSERT_NE(coordinator, nullptr);
    openReady(coordinator, provider, render);

    const std::shared_ptr<const SessionSnapshot> beforeSeek = coordinator->snapshot();
    ASSERT_NE(beforeSeek, nullptr);
    const SeekFrameCommand duplicate{
        .context =
            CommandContext{
                .sessionId = beforeSeek->sessionId,
                .sessionEpoch = beforeSeek->sessionEpoch,
                .commandId = domain::CommandId{2},
            },
        .frameId = domain::FrameId{1},
    };
    ASSERT_EQ(coordinator->submit(duplicate), PortSubmitResult::Accepted);
    ASSERT_EQ(coordinator->submit(duplicate), PortSubmitResult::Accepted);
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const std::optional<FrameRequest> firstRequest = provider->frameRequest(1U);
    ASSERT_TRUE(firstRequest.has_value());
    ASSERT_TRUE(provider->postFrameReady(*firstRequest, makePair(firstRequest->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(2U));
    ASSERT_TRUE(provider->postFrameSucceeded(*firstRequest));
    presentPublished(coordinator, render, 1U);

    const std::vector<CommandTerminal> firstTerminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(firstTerminals.size(), 1U);
    EXPECT_EQ(firstTerminals.front().context.commandId, domain::CommandId{2});
    EXPECT_EQ(firstTerminals.front().outcome, CommandOutcome::Succeeded);

    const std::shared_ptr<const SessionSnapshot> afterFirst = coordinator->snapshot();
    ASSERT_NE(afterFirst, nullptr);
    ASSERT_EQ(coordinator->submit(SeekFrameCommand{
                  .context =
                      CommandContext{
                          .sessionId = afterFirst->sessionId,
                          .sessionEpoch = afterFirst->sessionEpoch,
                          .commandId = domain::CommandId{3},
                      },
                  .frameId = domain::FrameId{2},
              }),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(provider->waitForFrameRequestCount(3U));
    EXPECT_EQ(provider->frameRequestCount(), 3U);
    const std::optional<FrameRequest> secondRequest = provider->frameRequest(2U);
    ASSERT_TRUE(secondRequest.has_value());
    ASSERT_TRUE(provider->postFrameReady(*secondRequest, makePair(secondRequest->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(3U));
    ASSERT_TRUE(provider->postFrameSucceeded(*secondRequest));
    presentPublished(coordinator, render, 2U);
    const std::vector<CommandTerminal> secondTerminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(secondTerminals.size(), 1U);
    EXPECT_EQ(secondTerminals.front().context.commandId, domain::CommandId{3});
    EXPECT_EQ(secondTerminals.front().outcome, CommandOutcome::Succeeded);
}

TEST(PlaybackCoordinatorTests, InvalidReplacementPreservesTheDisplayedReadySession) {
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render);
    ASSERT_NE(coordinator, nullptr);
    openReady(coordinator, provider, render);

    const std::shared_ptr<const SessionSnapshot> ready = coordinator->snapshot();
    ASSERT_NE(ready, nullptr);
    ASSERT_EQ(coordinator->submit(OpenDirectSourcesCommand{
                  .context =
                      CommandContext{
                          .sessionId = ready->sessionId,
                          .sessionEpoch = ready->sessionEpoch,
                          .commandId = domain::CommandId{2},
                      },
                  .sourceA =
                      makeDescriptor("new-a.mp4", domain::MediaExtent{.width = 320, .height = 180}),
                  .sourceB = makeDescriptor(
                      "new-b.mp4", domain::MediaExtent{.width = 160, .height = 90}, 11),
              }),
              PortSubmitResult::Accepted);

    const std::vector<CommandTerminal> terminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminals.size(), 1U);
    EXPECT_EQ(terminals.front().outcome, CommandOutcome::Failed);
    ASSERT_TRUE(terminals.front().error.has_value());
    EXPECT_EQ(terminals.front().error->code, domain::MediaErrorCode::kSourceFrameCountMismatch);
    EXPECT_EQ(provider->frameRequestCount(), 1U);
    const std::shared_ptr<const SessionSnapshot> after = coordinator->snapshot();
    ASSERT_NE(after, nullptr);
    EXPECT_TRUE(after->isConsistent());
    EXPECT_EQ(after->sessionState, domain::SessionState::kReady);
    EXPECT_EQ(after->displayedFrame, domain::FrameId{0});
    ASSERT_TRUE(after->lastError.has_value());
    EXPECT_EQ(after->lastError->code, domain::MediaErrorCode::kSourceFrameCountMismatch);
    EXPECT_TRUE(render->clearContexts().empty());
}

TEST(PlaybackCoordinatorTests, CloseClearsTheRenderChannelAndAdvancesTheSessionEpoch) {
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render);
    ASSERT_NE(coordinator, nullptr);
    openReady(coordinator, provider, render);

    const std::shared_ptr<const SessionSnapshot> ready = coordinator->snapshot();
    ASSERT_NE(ready, nullptr);
    ASSERT_EQ(coordinator->submit(CloseSessionCommand{
                  .context =
                      CommandContext{
                          .sessionId = ready->sessionId,
                          .sessionEpoch = ready->sessionEpoch,
                          .commandId = domain::CommandId{2},
                      },
              }),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(provider->waitForCloseRequestCount(1U));
    const std::optional<FrameProviderCloseRequest> close = provider->closeRequest();
    ASSERT_TRUE(close.has_value());
    ASSERT_TRUE(render->waitForClearCount(1U));
    const std::vector<PlaybackRequestContext> clears = render->clearContexts();
    ASSERT_EQ(clears.size(), 1U);
    EXPECT_EQ(clears.front(), close->context);
    ASSERT_TRUE(provider->postCloseSucceeded(*close));

    const std::vector<CommandTerminal> terminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminals.size(), 1U);
    EXPECT_EQ(terminals.front().outcome, CommandOutcome::Succeeded);
    const std::shared_ptr<const SessionSnapshot> after = coordinator->snapshot();
    ASSERT_NE(after, nullptr);
    EXPECT_TRUE(after->isConsistent());
    EXPECT_EQ(after->sessionState, domain::SessionState::kEmpty);
    EXPECT_EQ(after->sessionEpoch, domain::SessionEpoch{2});
    EXPECT_EQ(after->playbackGeneration, domain::PlaybackGeneration{2});
}

TEST(PlaybackCoordinatorTests, RejectsAnObsoleteEpochWithoutDispatchingProviderWork) {
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render);
    ASSERT_NE(coordinator, nullptr);
    openReady(coordinator, provider, render);

    ASSERT_EQ(coordinator->submit(SeekFrameCommand{
                  .context =
                      CommandContext{
                          .sessionId = domain::SessionId{91},
                          .sessionEpoch = domain::SessionEpoch{0},
                          .commandId = domain::CommandId{2},
                      },
                  .frameId = domain::FrameId{1},
              }),
              PortSubmitResult::Accepted);
    const std::vector<CommandTerminal> terminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminals.size(), 1U);
    EXPECT_EQ(terminals.front().outcome, CommandOutcome::Canceled);
    EXPECT_EQ(provider->frameRequestCount(), 1U);
}

TEST(PlaybackCoordinatorTests, PreservesCriticalTerminalsWhenTheBoundedQueueIsFull) {
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<BlockingRenderChannel>();
    const auto coordinator = PlaybackCoordinator::create(
        domain::SessionId{91},
        PlaybackCoordinator::Dependencies{
            .mediaProbe = std::make_shared<FakeMediaProbe>(),
            .directFrameProvider = provider,
            .deadlineScheduler = std::make_shared<FakeDeadlineScheduler>(),
            .clock = std::make_shared<FakeSteadyClock>(),
            .renderChannel = render,
        });
    ASSERT_NE(coordinator, nullptr);

    ASSERT_EQ(
        coordinator->submit(OpenDirectSourcesCommand{
            .context =
                CommandContext{
                    .sessionId = domain::SessionId{91},
                    .sessionEpoch = domain::SessionEpoch{0},
                    .commandId = domain::CommandId{1},
                },
            .sourceA = makeDescriptor("a.mp4", domain::MediaExtent{.width = 320, .height = 180}),
            .sourceB = makeDescriptor("b.mp4", domain::MediaExtent{.width = 160, .height = 90}),
        }),
        PortSubmitResult::Accepted);
    ASSERT_TRUE(provider->waitForOpenRequestCount(1U));
    const std::optional<FrameProviderOpenRequest> open = provider->openRequest();
    ASSERT_TRUE(open.has_value());
    ASSERT_TRUE(provider->postOpenSucceeded(*open));
    ASSERT_TRUE(provider->waitForFrameRequestCount(1U));
    const std::optional<FrameRequest> frame = provider->frameRequest(0U);
    ASSERT_TRUE(frame.has_value());
    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(frame->frameId)));
    ASSERT_TRUE(render->waitUntilPublishBlocked());

    for (std::size_t index = 0U; index < kCriticalEventCapacity; ++index) {
        ASSERT_EQ(coordinator->postCritical(ignoredCriticalEvent()), EventPostResult::Accepted);
    }

    std::thread releaser([&render] {
        std::this_thread::sleep_for(100ms);
        render->release();
    });
    EXPECT_EQ(coordinator->postCritical(ignoredCriticalEvent()), EventPostResult::Accepted);
    releaser.join();

    ASSERT_TRUE(provider->postFrameSucceeded(*frame));
    ASSERT_EQ(coordinator->postCritical(ApplicationEvent{FramePairPresented{
                  .context = frame->context,
                  .frameId = frame->frameId,
              }}),
              EventPostResult::Accepted);
    const std::vector<CommandTerminal> terminals = waitForTerminals(coordinator, 1U);
    ASSERT_EQ(terminals.size(), 1U);
    EXPECT_EQ(terminals.front().outcome, CommandOutcome::Succeeded);
}

TEST(PlaybackCoordinatorTests, VfrProbePayloadOpensWithSharedTimelineAndReachesReady) {
    const auto probe = std::make_shared<FakeMediaProbe>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render, probe);

    const std::shared_ptr<const domain::FrameTimeline> timeline = makeVfrTimeline();
    ASSERT_NE(timeline, nullptr);
    const domain::MediaDescriptor descriptorA =
        makeVfrDescriptor("C:/media/a.mp4",
                          domain::MediaExtent{.width = 320, .height = 180},
                          4,
                          domain::MediaTime{69800});
    const domain::MediaDescriptor descriptorB =
        makeDescriptor("C:/media/b.mp4", domain::MediaExtent{.width = 160, .height = 90}, 4);

    openVfrReady(coordinator, probe, provider, render, timeline, descriptorA, descriptorB);

    // The provider open request carries the VFR CanonicalTimeline that the probe published.
    const std::optional<FrameProviderOpenRequest> open = provider->openRequest();
    ASSERT_TRUE(open.has_value());
    EXPECT_TRUE(domain::isVariableFrameRate(open->timeline));
    EXPECT_EQ(std::get<std::shared_ptr<const domain::FrameTimeline>>(open->timeline).get(),
              timeline.get());

    const std::shared_ptr<const SessionSnapshot> ready = coordinator->snapshot();
    EXPECT_EQ(ready->sessionState, domain::SessionState::kReady);
    EXPECT_EQ(ready->displayedFrame, domain::FrameId{0});
    EXPECT_EQ(ready->canonicalFrameCount, 4U);
}

TEST(PlaybackCoordinatorTests, VfrContinuousPlayUsesAbsoluteNonuniformDeadlinesRelativeToAnchor) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto clock = std::make_shared<FakeSteadyClock>();
    const auto probe = std::make_shared<FakeMediaProbe>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render, probe, scheduler, clock);

    const std::shared_ptr<const domain::FrameTimeline> timeline = makeVfrTimeline();
    ASSERT_NE(timeline, nullptr);
    const domain::MediaDescriptor descriptorA =
        makeVfrDescriptor("C:/media/a.mp4",
                          domain::MediaExtent{.width = 320, .height = 180},
                          4,
                          domain::MediaTime{69800});
    const domain::MediaDescriptor descriptorB =
        makeDescriptor("C:/media/b.mp4", domain::MediaExtent{.width = 160, .height = 90}, 4);

    openVfrReady(coordinator, probe, provider, render, timeline, descriptorA, descriptorB);
    markGraphicsReady(coordinator);

    const std::shared_ptr<const SessionSnapshot> ready = coordinator->snapshot();
    ASSERT_EQ(coordinator->submit(PlayCommand{
                  .context =
                      CommandContext{
                          .sessionId = ready->sessionId,
                          .sessionEpoch = ready->sessionEpoch,
                          .commandId = domain::CommandId{2},
                      },
              }),
              PortSubmitResult::Accepted);
    EXPECT_EQ(waitForTerminals(coordinator, 1U).front().outcome, CommandOutcome::Succeeded);

    // Frame 1 sits at 10000us on the timeline, so the first cadence is the absolute 10000us
    // offset from the wall anchor, not a uniform 33333us CFR interval.
    ASSERT_TRUE(scheduler->waitForScheduleCount(2U));
    const std::optional<DeadlineRequest> firstCadence = scheduler->request(1U);
    ASSERT_TRUE(firstCadence.has_value());
    EXPECT_EQ(firstCadence->due, clock->now() + 10000us);
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPlaying);
    EXPECT_EQ(provider->frameRequestCount(), 1U);

    clock->set(firstCadence->due);
    ASSERT_TRUE(scheduler->fire(1U));
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const std::optional<FrameRequest> frame = provider->frameRequest(1U);
    ASSERT_TRUE(frame.has_value());
    EXPECT_EQ(frame->frameId, domain::FrameId{1});
    EXPECT_EQ(frame->priority, FrameRequestPriority::Sequential);

    ASSERT_TRUE(provider->postFrameReady(*frame, makePair(frame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(2U));
    presentPublished(coordinator, render, 1U);
    ASSERT_TRUE(provider->postFrameSucceeded(*frame));
    ASSERT_TRUE(waitUntil(
        [&coordinator] { return coordinator->snapshot()->displayedFrame == domain::FrameId{1}; }));

    // Until that deadline the current pair (frame 1) remains and no next frame is requested.
    ASSERT_TRUE(scheduler->waitForScheduleCount(4U));
    const std::optional<DeadlineRequest> secondCadence = scheduler->request(3U);
    ASSERT_TRUE(secondCadence.has_value());
    EXPECT_EQ(secondCadence->due, firstCadence->due + 40000us);
    EXPECT_EQ(provider->frameRequestCount(), 2U);
    EXPECT_EQ(coordinator->snapshot()->displayedFrame, domain::FrameId{1});
    EXPECT_FALSE(coordinator->snapshot()->requestedFrame.has_value());
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPlaying);
}

TEST(PlaybackCoordinatorTests, VfrSlowDecodeCatchUpSkipsCompletePairAndPreservesFinalSemantics) {
    const auto scheduler = std::make_shared<FakeDeadlineScheduler>();
    const auto clock = std::make_shared<FakeSteadyClock>();
    const auto probe = std::make_shared<FakeMediaProbe>();
    const auto provider = std::make_shared<FakeFrameProvider>();
    const auto render = std::make_shared<FakeRenderChannel>();
    const auto coordinator = makeCoordinator(provider, render, probe, scheduler, clock);

    const std::shared_ptr<const domain::FrameTimeline> timeline = makeVfrTimeline();
    ASSERT_NE(timeline, nullptr);
    const domain::MediaDescriptor descriptorA =
        makeVfrDescriptor("C:/media/a.mp4",
                          domain::MediaExtent{.width = 320, .height = 180},
                          4,
                          domain::MediaTime{69800});
    const domain::MediaDescriptor descriptorB =
        makeDescriptor("C:/media/b.mp4", domain::MediaExtent{.width = 160, .height = 90}, 4);

    openVfrReady(coordinator, probe, provider, render, timeline, descriptorA, descriptorB);
    markGraphicsReady(coordinator);

    const std::shared_ptr<const SessionSnapshot> ready = coordinator->snapshot();
    ASSERT_EQ(coordinator->submit(PlayCommand{
                  .context =
                      CommandContext{
                          .sessionId = ready->sessionId,
                          .sessionEpoch = ready->sessionEpoch,
                          .commandId = domain::CommandId{2},
                      },
              }),
              PortSubmitResult::Accepted);
    EXPECT_EQ(waitForTerminals(coordinator, 1U).front().outcome, CommandOutcome::Succeeded);

    ASSERT_TRUE(scheduler->waitForScheduleCount(2U));
    const std::optional<DeadlineRequest> firstCadence = scheduler->request(1U);
    ASSERT_TRUE(firstCadence.has_value());

    // Simulate a slow decode: advance the clock past frame 1 (10000us) and frame 2 (50000us)
    // before the cadence fires. Catch-up maps the elapsed wall time back through the timeline
    // with frameAtOrBefore, landing on frame 2 and skipping the complete frame-1 pair.
    clock->set(firstCadence->due + 50000us);
    ASSERT_TRUE(scheduler->fire(1U));
    ASSERT_TRUE(provider->waitForFrameRequestCount(2U));
    const std::optional<FrameRequest> catchUp = provider->frameRequest(1U);
    ASSERT_TRUE(catchUp.has_value());
    EXPECT_EQ(catchUp->frameId, domain::FrameId{2});
    EXPECT_EQ(catchUp->priority, FrameRequestPriority::Sequential);

    ASSERT_TRUE(provider->postFrameReady(*catchUp, makePair(catchUp->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(2U));
    presentPublished(coordinator, render, 1U);
    ASSERT_TRUE(provider->postFrameSucceeded(*catchUp));
    ASSERT_TRUE(waitUntil(
        [&coordinator] { return coordinator->snapshot()->displayedFrame == domain::FrameId{2}; }));

    // Frame 2 is not the final frame, so the next cadence targets the final frame 3. Committing
    // it auto-pauses, preserving the end-of-timeline semantics.
    ASSERT_TRUE(scheduler->waitForScheduleCount(4U));
    const std::optional<DeadlineRequest> finalCadence = scheduler->request(3U);
    ASSERT_TRUE(finalCadence.has_value());
    clock->set(finalCadence->due);
    ASSERT_TRUE(scheduler->fire(3U));
    ASSERT_TRUE(provider->waitForFrameRequestCount(3U));
    const std::optional<FrameRequest> finalFrame = provider->frameRequest(2U);
    ASSERT_TRUE(finalFrame.has_value());
    EXPECT_EQ(finalFrame->frameId, domain::FrameId{3});

    ASSERT_TRUE(provider->postFrameReady(*finalFrame, makePair(finalFrame->frameId)));
    ASSERT_TRUE(render->waitForPublishedCount(3U));
    presentPublished(coordinator, render, 2U);
    ASSERT_TRUE(provider->postFrameSucceeded(*finalFrame));
    ASSERT_TRUE(waitUntil(
        [&coordinator] { return coordinator->snapshot()->displayedFrame == domain::FrameId{3}; }));
    EXPECT_EQ(coordinator->snapshot()->playbackState, domain::PlaybackState::kPaused);
}

} // namespace
} // namespace dvs::application
