#include "dvs/domain/FrameTimeline.h"
#include "dvs/media/DirectFrameProvider.h"
#include "dvs/media/MediaProbe.h"
#include "dvs/platform/FrameBudget.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dvs::media {
namespace {

class RecordingEventSink final : public application::IApplicationEventSink {
public:
    [[nodiscard]] application::EventPostResult
    postCritical(application::ApplicationEvent event) noexcept override {
        {
            std::scoped_lock lock(mutex_);
            events_.push_back(std::move(event));
        }
        condition_.notify_all();
        return application::EventPostResult::Accepted;
    }

    [[nodiscard]] application::EventPostResult
    postRealtime(application::ApplicationEvent) noexcept override {
        return application::EventPostResult::Accepted;
    }

    void closeRealtimeIngress() noexcept override {}
    void closeCriticalIngress() noexcept override {}

    [[nodiscard]] bool waitForEventCount(const std::size_t count) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(
            lock, std::chrono::seconds{5}, [this, count] { return events_.size() >= count; });
    }

    [[nodiscard]] std::vector<application::ApplicationEvent> events() const {
        std::scoped_lock lock(mutex_);
        return events_;
    }

    void clear() {
        std::scoped_lock lock(mutex_);
        events_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<application::ApplicationEvent> events_;
};

[[nodiscard]] std::filesystem::path fixture(const std::string_view name) {
    return std::filesystem::path{DVS_MEDIA_FIXTURE_DIR} / std::string{name};
}

[[nodiscard]] application::PlaybackRequestContext
makePlaybackContext(const std::uint64_t requestId, const std::uint64_t generation = 8U) {
    return application::PlaybackRequestContext{
        .request =
            application::RequestContext{
                .sessionId = domain::SessionId{44},
                .sessionEpoch = domain::SessionEpoch{7},
                .requestId = domain::RequestId{requestId},
            },
        .playbackGeneration = domain::PlaybackGeneration{generation},
    };
}

[[nodiscard]] application::FrameRequestContext
makeFrameContext(const std::uint64_t requestId, const std::uint64_t generation = 8U) {
    return application::FrameRequestContext{
        .playback = makePlaybackContext(requestId, generation),
        .deviceGeneration = domain::DeviceGeneration{3},
    };
}

[[nodiscard]] domain::RationalRate makeRate(const std::int64_t numerator = 30,
                                            const std::int64_t denominator = 1) {
    return domain::RationalRate::create(numerator, denominator).value();
}

[[nodiscard]] application::FrameProviderOpenRequest
makeOpenRequest(const std::uint64_t requestId,
                domain::CanonicalTimeline timeline = domain::CanonicalTimeline{makeRate()}) {
    const auto sourceA =
        MediaProbe::inspect(fixture("h264_a_320x180_30fps_12.mp4"), domain::SourceRole::kA);
    const auto sourceB =
        MediaProbe::inspect(fixture("h264_b_160x90_30fps_12.mp4"), domain::SourceRole::kB);
    EXPECT_TRUE(sourceA);
    EXPECT_TRUE(sourceB);
    return application::FrameProviderOpenRequest{
        .context = makePlaybackContext(requestId),
        .source = application::ActiveFrameSource::Direct,
        .sourceA = sourceA.value(),
        .sourceB = sourceB.value(),
        .timeline = std::move(timeline),
    };
}

// A successful probe posts its ProbeCompleted payload first and the RequestSucceeded terminal
// second, so waiting for the terminal guarantees the payload has already arrived and reading it
// cannot race with the still-running probe worker. The descriptor and (for VFR sources) the Source
// A timeline are returned as copies: the RecordingEventSink only offers events() by value, so a
// pointer/reference into that vector would dangle the instant the temporary is destroyed.
struct ProbedSource final {
    domain::MediaDescriptor descriptor;
    std::shared_ptr<const domain::FrameTimeline> timeline;
};

[[nodiscard]] std::optional<ProbedSource>
probeSource(const std::filesystem::path& path,
            const domain::SourceRole role,
            const std::shared_ptr<RecordingEventSink>& events,
            const std::uint64_t requestId) {
    MediaProbe probe;
    events->clear();
    const application::MediaProbeRequest request{
        .context =
            application::RequestContext{
                .sessionId = domain::SessionId{44},
                .sessionEpoch = domain::SessionEpoch{7},
                .requestId = domain::RequestId{requestId},
            },
        .sourceRole = role,
        .sourcePath = path,
    };
    if (probe.submit(request, events) != application::PortSubmitResult::Accepted) {
        return std::nullopt;
    }
    if (!events->waitForEventCount(2U)) {
        return std::nullopt;
    }
    for (const application::ApplicationEvent& event : events->events()) {
        if (const auto* const done = std::get_if<application::ProbeCompleted>(&event)) {
            std::shared_ptr<const domain::FrameTimeline> timeline;
            if (done->timeline.has_value()) {
                timeline = *done->timeline;
            }
            return ProbedSource{done->descriptor, std::move(timeline)};
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<application::RequestSucceeded>
findPlaybackSucceeded(const std::shared_ptr<RecordingEventSink>& events,
                      const application::PlaybackRequestContext& playback) {
    for (const application::ApplicationEvent& event : events->events()) {
        if (const auto* const terminal = std::get_if<application::RequestTerminal>(&event)) {
            if (const auto* const succeeded =
                    std::get_if<application::RequestSucceeded>(terminal)) {
                if (const auto* const context =
                        std::get_if<application::PlaybackRequestContext>(&succeeded->context)) {
                    if (*context == playback) {
                        return *succeeded;
                    }
                }
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<application::FramePairReady>
findFramePairReady(const std::shared_ptr<RecordingEventSink>& events,
                   const application::FrameRequestContext& context) {
    for (const application::ApplicationEvent& event : events->events()) {
        if (const auto* const ready = std::get_if<application::FramePairReady>(&event)) {
            if (ready->context == context) {
                return *ready;
            }
        }
    }
    return std::nullopt;
}

TEST(DirectFrameProviderTests, OpensDirectSourcesAndPublishesOnlyACompleteExactPair) {
    platform::FrameBudget budget{4U * 1024U * 1024U};
    DirectFrameProvider provider{budget};
    const auto events = std::make_shared<RecordingEventSink>();
    const application::FrameProviderOpenRequest open = makeOpenRequest(700U);

    ASSERT_EQ(provider.submit(open, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(1U));

    const application::FrameRequest request{
        .context = makeFrameContext(701U),
        .frameId = domain::FrameId{11},
        .priority = application::FrameRequestPriority::Exact,
    };
    ASSERT_EQ(provider.submit(request, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(3U));

    const auto recorded = events->events();
    ASSERT_EQ(recorded.size(), 3U);
    const auto* const openTerminal = std::get_if<application::RequestTerminal>(&recorded[0]);
    ASSERT_NE(openTerminal, nullptr);
    const auto* const openSuccess = std::get_if<application::RequestSucceeded>(openTerminal);
    ASSERT_NE(openSuccess, nullptr);
    ASSERT_TRUE(std::holds_alternative<application::PlaybackRequestContext>(openSuccess->context));
    EXPECT_EQ(std::get<application::PlaybackRequestContext>(openSuccess->context), open.context);

    const auto* const ready = std::get_if<application::FramePairReady>(&recorded[1]);
    ASSERT_NE(ready, nullptr);
    EXPECT_EQ(ready->context, request.context);
    EXPECT_EQ(ready->pair.frameId(), request.frameId);
    EXPECT_EQ(ready->pair.source(), application::ActiveFrameSource::Direct);
    EXPECT_EQ(ready->pair.frameA().geometry().width, 320U);
    EXPECT_EQ(ready->pair.frameA().geometry().height, 180U);
    EXPECT_EQ(ready->pair.frameB().geometry().width, 160U);
    EXPECT_EQ(ready->pair.frameB().geometry().height, 90U);
    EXPECT_TRUE(ready->pair.frameA().isValid());
    EXPECT_TRUE(ready->pair.frameB().isValid());
    const auto* const frameTerminal = std::get_if<application::RequestTerminal>(&recorded[2]);
    ASSERT_NE(frameTerminal, nullptr);
    const auto* const frameSuccess = std::get_if<application::RequestSucceeded>(frameTerminal);
    ASSERT_NE(frameSuccess, nullptr);
    ASSERT_TRUE(std::holds_alternative<application::FrameRequestContext>(frameSuccess->context));
    EXPECT_EQ(std::get<application::FrameRequestContext>(frameSuccess->context), request.context);
    EXPECT_GT(budget.reservedBytes(), 0U);
}

TEST(DirectFrameProviderTests, AcceptsANewerPlaybackGenerationWithoutReopeningSources) {
    platform::FrameBudget budget{4U * 1024U * 1024U};
    DirectFrameProvider provider{budget};
    const auto events = std::make_shared<RecordingEventSink>();
    const application::FrameProviderOpenRequest open = makeOpenRequest(710U);

    ASSERT_EQ(provider.submit(open, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(1U));

    const application::FrameRequest nextGeneration{
        .context = makeFrameContext(711U, 9U),
        .frameId = domain::FrameId{0},
        .priority = application::FrameRequestPriority::Exact,
    };
    ASSERT_EQ(provider.submit(nextGeneration, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(3U));

    const auto recorded = events->events();
    ASSERT_EQ(recorded.size(), 3U);
    const auto* const ready = std::get_if<application::FramePairReady>(&recorded[1]);
    ASSERT_NE(ready, nullptr);
    EXPECT_EQ(ready->context, nextGeneration.context);
    EXPECT_EQ(ready->pair.frameId(), nextGeneration.frameId);
    const auto* const terminal = std::get_if<application::RequestTerminal>(&recorded[2]);
    ASSERT_NE(terminal, nullptr);
    const auto* const success = std::get_if<application::RequestSucceeded>(terminal);
    ASSERT_NE(success, nullptr);
    ASSERT_TRUE(std::holds_alternative<application::FrameRequestContext>(success->context));
    EXPECT_EQ(std::get<application::FrameRequestContext>(success->context), nextGeneration.context);
}

TEST(DirectFrameProviderTests, PublishesForwardSequentialPairsAcrossPlaybackGenerations) {
    platform::FrameBudget budget{8U * 1024U * 1024U};
    DirectFrameProvider provider{budget};
    const auto events = std::make_shared<RecordingEventSink>();
    const application::FrameProviderOpenRequest open = makeOpenRequest(715U);

    ASSERT_EQ(provider.submit(open, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(1U));

    const application::FrameRequest first{
        .context = makeFrameContext(716U, 9U),
        .frameId = domain::FrameId{0},
        .priority = application::FrameRequestPriority::Exact,
    };
    ASSERT_EQ(provider.submit(first, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(3U));

    const application::FrameRequest adjacent{
        .context = makeFrameContext(717U, 10U),
        .frameId = domain::FrameId{1},
        .priority = application::FrameRequestPriority::Sequential,
    };
    ASSERT_EQ(provider.submit(adjacent, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(5U));

    const application::FrameRequest skipped{
        .context = makeFrameContext(718U, 11U),
        .frameId = domain::FrameId{6},
        .priority = application::FrameRequestPriority::Sequential,
    };
    ASSERT_EQ(provider.submit(skipped, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(7U));

    const auto recorded = events->events();
    ASSERT_EQ(recorded.size(), 7U);
    const auto* const adjacentReady = std::get_if<application::FramePairReady>(&recorded[3]);
    const auto* const skippedReady = std::get_if<application::FramePairReady>(&recorded[5]);
    ASSERT_NE(adjacentReady, nullptr);
    ASSERT_NE(skippedReady, nullptr);
    EXPECT_EQ(adjacentReady->pair.frameId(), adjacent.frameId);
    EXPECT_EQ(skippedReady->pair.frameId(), skipped.frameId);
    EXPECT_EQ(adjacentReady->context, adjacent.context);
    EXPECT_EQ(skippedReady->context, skipped.context);
}

TEST(DirectFrameProviderTests, SequentialPlaybackDoesNotRetainHistoricalCpuPairs) {
    // One decoded A/B pair fits, but retaining that pair would leave too little budget to decode
    // the next one. This is the small-fixture equivalent of sustained 4K playback under the
    // application's fixed 256 MiB frame budget.
    platform::FrameBudget budget{128U * 1024U};
    DirectFrameProvider provider{budget};
    const auto events = std::make_shared<RecordingEventSink>();
    const application::FrameProviderOpenRequest open = makeOpenRequest(719U);

    ASSERT_EQ(provider.submit(open, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(1U));
    events->clear();

    for (std::int64_t frame = 0; frame < 12; ++frame) {
        const application::FrameRequest request{
            .context = makeFrameContext(720U + static_cast<std::uint64_t>(frame), 9U),
            .frameId = domain::FrameId{frame},
            .priority = application::FrameRequestPriority::Sequential,
        };
        ASSERT_EQ(provider.submit(request, events), application::PortSubmitResult::Accepted);
        ASSERT_TRUE(events->waitForEventCount(2U));

        {
            const auto recorded = events->events();
            ASSERT_EQ(recorded.size(), 2U);
            const auto* const ready = std::get_if<application::FramePairReady>(&recorded[0]);
            ASSERT_NE(ready, nullptr);
            EXPECT_EQ(ready->pair.frameId(), request.frameId);
            const auto* const terminal = std::get_if<application::RequestTerminal>(&recorded[1]);
            ASSERT_NE(terminal, nullptr);
            EXPECT_NE(std::get_if<application::RequestSucceeded>(terminal), nullptr);
        }

        events->clear();
        EXPECT_EQ(budget.reservedBytes(), 0U);
    }
}

TEST(DirectFrameProviderTests, CancelsAnOlderPlaybackGenerationBeforeItCanDecode) {
    platform::FrameBudget budget{4U * 1024U * 1024U};
    DirectFrameProvider provider{budget};
    const auto events = std::make_shared<RecordingEventSink>();
    const application::FrameProviderOpenRequest open = makeOpenRequest(720U);

    ASSERT_EQ(provider.submit(open, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(1U));

    const application::FrameRequest newest{
        .context = makeFrameContext(721U, 10U),
        .frameId = domain::FrameId{1},
        .priority = application::FrameRequestPriority::Exact,
    };
    ASSERT_EQ(provider.submit(newest, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(3U));

    const application::FrameRequest stale{
        .context = makeFrameContext(722U, 9U),
        .frameId = domain::FrameId{0},
        .priority = application::FrameRequestPriority::Exact,
    };
    ASSERT_EQ(provider.submit(stale, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(4U));

    const auto recorded = events->events();
    ASSERT_EQ(recorded.size(), 4U);
    EXPECT_EQ(std::get_if<application::FramePairReady>(&recorded[3]), nullptr);
    const auto* const terminal = std::get_if<application::RequestTerminal>(&recorded[3]);
    ASSERT_NE(terminal, nullptr);
    const auto* const canceled = std::get_if<application::RequestCanceled>(terminal);
    ASSERT_NE(canceled, nullptr);
    EXPECT_EQ(canceled->reason, application::CancellationReason::Superseded);
    ASSERT_TRUE(std::holds_alternative<application::FrameRequestContext>(canceled->context));
    EXPECT_EQ(std::get<application::FrameRequestContext>(canceled->context), stale.context);
}

TEST(DirectFrameProviderTests, ClosingReleasesTheProviderPairTableBudgetReservations) {
    platform::FrameBudget budget{4U * 1024U * 1024U};
    DirectFrameProvider provider{budget};
    const auto events = std::make_shared<RecordingEventSink>();
    const application::FrameProviderOpenRequest open = makeOpenRequest(730U);

    ASSERT_EQ(provider.submit(open, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(1U));
    const application::FrameRequest frame{
        .context = makeFrameContext(731U),
        .frameId = domain::FrameId{0},
        .priority = application::FrameRequestPriority::Exact,
    };
    ASSERT_EQ(provider.submit(frame, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(3U));
    EXPECT_GT(budget.reservedBytes(), 0U);

    const application::FrameProviderCloseRequest close{.context = open.context};
    ASSERT_EQ(provider.submit(close, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(4U));
    events->clear();
    EXPECT_EQ(budget.reservedBytes(), 0U);
}

TEST(DirectFrameProviderTests, DropsQueuedCompletionSafelyAfterTheEventSinkExpires) {
    platform::FrameBudget budget{4U * 1024U * 1024U};
    auto provider = std::make_unique<DirectFrameProvider>(budget);
    auto events = std::make_shared<RecordingEventSink>();
    const application::FrameProviderOpenRequest open = makeOpenRequest(740U);

    ASSERT_EQ(provider->submit(open, events), application::PortSubmitResult::Accepted);
    events.reset();
    provider.reset();
    SUCCEED();
}

// Combination 1: Source A is VFR, Source B is CFR. The VFR timeline is anchored on Source A, and
// the provider pairs one FrameId with one Source A canonical time and a distinct CFR Source B
// frame. The decoded A presentation time must equal the A timeline entry exactly.
TEST(DirectFrameProviderTests, OpensPairWhereAVfrAndBCfrUsesSourceAVfrTimeline) {
    platform::FrameBudget budget{4U * 1024U * 1024U};
    const auto events = std::make_shared<RecordingEventSink>();

    const std::optional<ProbedSource> sourceA =
        probeSource(fixture("h264_vfr_320x180_12.mp4"), domain::SourceRole::kA, events, 100U);
    ASSERT_TRUE(sourceA.has_value());
    ASSERT_EQ(sourceA->descriptor.timingConfidence, domain::TimingConfidence::kVariableFrameRate);
    ASSERT_FALSE(sourceA->descriptor.frameRate.has_value());
    ASSERT_TRUE(sourceA->timeline);
    const std::optional<ProbedSource> sourceB =
        probeSource(fixture("h264_b_160x90_30fps_12.mp4"), domain::SourceRole::kB, events, 101U);
    ASSERT_TRUE(sourceB.has_value());

    const application::FrameProviderOpenRequest open{
        .context = makePlaybackContext(110U),
        .source = application::ActiveFrameSource::Direct,
        .sourceA = sourceA->descriptor,
        .sourceB = sourceB->descriptor,
        .timeline = domain::CanonicalTimeline{sourceA->timeline},
    };
    DirectFrameProvider provider{budget};
    events->clear();
    ASSERT_EQ(provider.submit(open, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(1U));
    ASSERT_TRUE(findPlaybackSucceeded(events, open.context).has_value());
    EXPECT_EQ(budget.reservedBytes(), 0U);

    events->clear();
    const application::FrameRequestContext frameContext = makeFrameContext(111U, 9U);
    const application::FrameRequest request{
        .context = frameContext,
        .frameId = domain::FrameId{1},
        .priority = application::FrameRequestPriority::Exact,
    };
    ASSERT_EQ(provider.submit(request, events), application::PortSubmitResult::Accepted);
    // The provider posts the FramePairReady payload before the RequestSucceeded terminal, so
    // waiting for the terminal guarantees the pair payload is present without an async race.
    ASSERT_TRUE(events->waitForEventCount(2U));
    EXPECT_GT(budget.reservedBytes(), 0U);

    std::optional<application::FramePairReady> ready = findFramePairReady(events, frameContext);
    ASSERT_TRUE(ready.has_value());
    EXPECT_EQ(ready->context, frameContext);
    EXPECT_EQ(ready->pair.frameId(), domain::FrameId{1});
    EXPECT_EQ(ready->pair.source(), application::ActiveFrameSource::Direct);
    EXPECT_EQ(ready->pair.frameA().geometry().width, 320U);
    EXPECT_EQ(ready->pair.frameA().geometry().height, 180U);
    EXPECT_EQ(ready->pair.frameB().geometry().width, 160U);
    EXPECT_EQ(ready->pair.frameB().geometry().height, 90U);
    EXPECT_TRUE(ready->pair.frameA().isValid());
    EXPECT_TRUE(ready->pair.frameB().isValid());

    const domain::MediaTime canonical =
        domain::canonicalFrameStartTime(open.timeline, domain::FrameId{1}).value();
    EXPECT_EQ(ready->pair.canonicalTime(), canonical);
    EXPECT_EQ(ready->pair.presentationTimeA(), canonical);

    events->clear();
    const application::FrameProviderCloseRequest closeRequest{.context = open.context};
    ASSERT_EQ(provider.submit(closeRequest, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(1U));
    ready.reset();
    EXPECT_EQ(budget.reservedBytes(), 0U);
}

// Combination 2: Source A is CFR, Source B is VFR. The canonical timeline is rational and anchored
// on Source A; the provider still emits one atomic (frameId, canonicalTime) pair with both valid
// sides and the A geometry matching the CFR descriptor.
TEST(DirectFrameProviderTests, OpensPairWhereACfrBVfrUsesRationalTimeline) {
    platform::FrameBudget budget{4U * 1024U * 1024U};
    const auto events = std::make_shared<RecordingEventSink>();

    const std::optional<ProbedSource> sourceA =
        probeSource(fixture("h264_a_320x180_30fps_12.mp4"), domain::SourceRole::kA, events, 120U);
    ASSERT_TRUE(sourceA.has_value());
    ASSERT_EQ(sourceA->descriptor.timingConfidence, domain::TimingConfidence::kVerifiedCfr);
    ASSERT_TRUE(sourceA->descriptor.frameRate.has_value());
    const std::optional<ProbedSource> sourceB = probeSource(
        fixture("h264_middle_pts_gap_64x48_30fps_12.mp4"), domain::SourceRole::kB, events, 121U);
    ASSERT_TRUE(sourceB.has_value());

    const application::FrameProviderOpenRequest open{
        .context = makePlaybackContext(130U),
        .source = application::ActiveFrameSource::Direct,
        .sourceA = sourceA->descriptor,
        .sourceB = sourceB->descriptor,
        .timeline = domain::CanonicalTimeline{makeRate()},
    };
    DirectFrameProvider provider{budget};
    events->clear();
    ASSERT_EQ(provider.submit(open, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(1U));
    ASSERT_TRUE(findPlaybackSucceeded(events, open.context).has_value());

    events->clear();
    const application::FrameRequestContext frameContext = makeFrameContext(131U, 9U);
    const application::FrameRequest request{
        .context = frameContext,
        .frameId = domain::FrameId{6},
        .priority = application::FrameRequestPriority::Exact,
    };
    ASSERT_EQ(provider.submit(request, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(2U));

    std::optional<application::FramePairReady> ready = findFramePairReady(events, frameContext);
    ASSERT_TRUE(ready.has_value());
    EXPECT_EQ(ready->pair.frameId(), domain::FrameId{6});
    EXPECT_EQ(ready->pair.frameA().geometry().width, 320U);
    EXPECT_EQ(ready->pair.frameA().geometry().height, 180U);
    EXPECT_EQ(ready->pair.frameB().geometry().width, 64U);
    EXPECT_EQ(ready->pair.frameB().geometry().height, 48U);
    EXPECT_TRUE(ready->pair.frameA().isValid());
    EXPECT_TRUE(ready->pair.frameB().isValid());

    EXPECT_EQ(ready->pair.canonicalTime(), makeRate().frameStartTime(domain::FrameId{6}).value());

    events->clear();
    const application::FrameProviderCloseRequest closeRequest{.context = open.context};
    ASSERT_EQ(provider.submit(closeRequest, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(1U));
    ready.reset();
    EXPECT_EQ(budget.reservedBytes(), 0U);
}

// Combination 3: both sources VFR. Source A carries the VFR timeline that drives the canonical
// time, and the pair must again be atomic at one FrameId with the A presentation time resolved
// to the Source A timeline entry.
TEST(DirectFrameProviderTests, OpensPairWhereBothVfrRespectsSourceACanonicalTime) {
    platform::FrameBudget budget{4U * 1024U * 1024U};
    const auto events = std::make_shared<RecordingEventSink>();

    const std::optional<ProbedSource> sourceA =
        probeSource(fixture("h264_vfr_320x180_12.mp4"), domain::SourceRole::kA, events, 140U);
    ASSERT_TRUE(sourceA.has_value());
    ASSERT_TRUE(sourceA->timeline);
    const std::optional<ProbedSource> sourceB = probeSource(
        fixture("h264_middle_pts_gap_64x48_30fps_12.mp4"), domain::SourceRole::kB, events, 141U);
    ASSERT_TRUE(sourceB.has_value());

    const application::FrameProviderOpenRequest open{
        .context = makePlaybackContext(150U),
        .source = application::ActiveFrameSource::Direct,
        .sourceA = sourceA->descriptor,
        .sourceB = sourceB->descriptor,
        .timeline = domain::CanonicalTimeline{sourceA->timeline},
    };
    DirectFrameProvider provider{budget};
    events->clear();
    ASSERT_EQ(provider.submit(open, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(1U));
    ASSERT_TRUE(findPlaybackSucceeded(events, open.context).has_value());

    events->clear();
    const application::FrameRequestContext frameContext = makeFrameContext(151U, 9U);
    const application::FrameRequest request{
        .context = frameContext,
        .frameId = domain::FrameId{6},
        .priority = application::FrameRequestPriority::Exact,
    };
    ASSERT_EQ(provider.submit(request, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(2U));

    std::optional<application::FramePairReady> ready = findFramePairReady(events, frameContext);
    ASSERT_TRUE(ready.has_value());
    EXPECT_EQ(ready->pair.frameId(), domain::FrameId{6});
    EXPECT_EQ(ready->pair.frameA().geometry().width, 320U);
    EXPECT_EQ(ready->pair.frameA().geometry().height, 180U);
    EXPECT_EQ(ready->pair.frameB().geometry().width, 64U);
    EXPECT_EQ(ready->pair.frameB().geometry().height, 48U);
    EXPECT_TRUE(ready->pair.frameA().isValid());
    EXPECT_TRUE(ready->pair.frameB().isValid());

    const domain::MediaTime canonical =
        domain::canonicalFrameStartTime(open.timeline, domain::FrameId{6}).value();
    EXPECT_EQ(ready->pair.canonicalTime(), canonical);
    EXPECT_EQ(ready->pair.presentationTimeA(), canonical);

    events->clear();
    const application::FrameProviderCloseRequest closeRequest{.context = open.context};
    ASSERT_EQ(provider.submit(closeRequest, events), application::PortSubmitResult::Accepted);
    ASSERT_TRUE(events->waitForEventCount(1U));
    ready.reset();
    EXPECT_EQ(budget.reservedBytes(), 0U);
}

} // namespace
} // namespace dvs::media
