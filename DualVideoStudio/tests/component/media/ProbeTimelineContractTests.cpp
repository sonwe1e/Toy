#include "dvs/application/Events.h"
#include "dvs/application/Ports.h"
#include "dvs/domain/FrameTimeline.h"
#include "dvs/domain/RationalRate.h"
#include "dvs/media/MediaProbe.h"

#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dvs::media {
namespace {

[[nodiscard]] application::RequestContext makeContext() {
    return application::RequestContext{
        .sessionId = domain::SessionId{1},
        .sessionEpoch = domain::SessionEpoch{1},
        .requestId = domain::RequestId{1},
    };
}

[[nodiscard]] application::PlaybackRequestContext makePlaybackContext() {
    return application::PlaybackRequestContext{
        .request = makeContext(),
        .playbackGeneration = domain::PlaybackGeneration{1},
    };
}

[[nodiscard]] domain::MediaDescriptor cfrDescriptor() {
    return domain::MediaDescriptor{
        .normalizedPath = std::filesystem::path{"cfr.mp4"},
        .extent = domain::MediaExtent{.width = 320, .height = 180},
        .frameRate = domain::RationalRate::create(30, 1).value(),
        .frameCount =
            domain::FrameCountInfo{.value = 12, .origin = domain::FrameCountOrigin::kReported},
        .duration = domain::MediaTime{400000},
        .codecId = "h264",
        .pixelFormatId = "nv12",
        .bitDepth = 8U,
        .colorMetadata = {domain::ColorMatrix::kBt709, domain::ColorRange::kLimited, false},
        .decodeCapabilities = {true, false},
        .timingConfidence = domain::TimingConfidence::kDeclaredCfr,
        .sourceIdentity = std::nullopt,
    };
}

[[nodiscard]] domain::MediaDescriptor vfrDescriptor() {
    return domain::MediaDescriptor{
        .normalizedPath = std::filesystem::path{"vfr.mp4"},
        .extent = domain::MediaExtent{.width = 320, .height = 180},
        .frameRate = std::nullopt,
        .frameCount =
            domain::FrameCountInfo{.value = 3, .origin = domain::FrameCountOrigin::kIndexed},
        .duration = domain::MediaTime{33334},
        .codecId = "h264",
        .pixelFormatId = "nv12",
        .bitDepth = 8U,
        .colorMetadata = {domain::ColorMatrix::kBt709, domain::ColorRange::kLimited, false},
        .decodeCapabilities = {true, false},
        .timingConfidence = domain::TimingConfidence::kVariableFrameRate,
        .sourceIdentity = std::nullopt,
    };
}

[[nodiscard]] application::ProbeCompleted makeCfrCompleted() {
    return application::ProbeCompleted{
        .context = makeContext(),
        .sourceRole = domain::SourceRole::kA,
        .descriptor = cfrDescriptor(),
        .timeline = std::nullopt,
    };
}

[[nodiscard]] std::shared_ptr<const domain::FrameTimeline> makeVfrTimeline() {
    auto variable = domain::FrameTimeline::create(std::vector<domain::MediaTime>{
        domain::MediaTime{0}, domain::MediaTime{16667}, domain::MediaTime{33334}});
    return std::make_shared<const domain::FrameTimeline>(std::move(variable).value());
}

TEST(ProbeTimelineContractTests, CfrProbeCompletedCarriesNoTimeline) {
    application::ProbeCompleted completed = makeCfrCompleted();
    ASSERT_FALSE(completed.timeline.has_value());
}

TEST(ProbeTimelineContractTests, VfrProbeCompletedCarriesASharedTimeline) {
    auto timeline = makeVfrTimeline();
    ASSERT_TRUE(timeline);
    application::ProbeCompleted completed{
        .context = makeContext(),
        .sourceRole = domain::SourceRole::kA,
        .descriptor = vfrDescriptor(),
        .timeline = timeline,
    };

    ASSERT_TRUE(completed.timeline.has_value());
    EXPECT_EQ(completed.timeline.value()->frameCount(), 3);
    EXPECT_EQ(domain::canonicalFrameStartTime(domain::CanonicalTimeline{completed.timeline.value()},
                                              domain::FrameId{2})
                  .value(),
              domain::MediaTime{33334});
}

TEST(ProbeTimelineContractTests, OpenRequestCanonicalTimelineAcceptsBothTimingPaths) {
    application::FrameProviderOpenRequest cfrRequest{
        .context = makePlaybackContext(),
        .source = application::ActiveFrameSource::Direct,
        .sourceA = cfrDescriptor(),
        .sourceB = cfrDescriptor(),
        .timeline = domain::CanonicalTimeline{domain::RationalRate::create(30, 1).value()},
    };
    auto vfrTimeline = makeVfrTimeline();
    ASSERT_TRUE(vfrTimeline);
    application::FrameProviderOpenRequest vfrRequest{
        .context = makePlaybackContext(),
        .source = application::ActiveFrameSource::Direct,
        .sourceA = vfrDescriptor(),
        .sourceB = vfrDescriptor(),
        .timeline = domain::CanonicalTimeline{vfrTimeline},
    };

    EXPECT_FALSE(domain::isVariableFrameRate(cfrRequest.timeline));
    EXPECT_TRUE(domain::isVariableFrameRate(vfrRequest.timeline));
    ASSERT_TRUE(domain::canonicalFrameAtOrBefore(vfrRequest.timeline, domain::MediaTime{16667}));
    EXPECT_EQ(
        domain::canonicalFrameAtOrBefore(vfrRequest.timeline, domain::MediaTime{16667}).value(),
        domain::FrameId{1});
}

} // namespace
} // namespace dvs::media
