#include "dvs/domain/FrameTimeline.h"
#include "dvs/domain/RationalRate.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace dvs::domain {
namespace {

TEST(FrameTimelineTests, NegativeTimeRefusesToYieldAFrame) {
    auto timeline =
        FrameTimeline::create(std::vector<MediaTime>{MediaTime{0}, MediaTime{10}, MediaTime{20}});
    ASSERT_TRUE(timeline);
    const auto result = timeline.value().frameAtOrBefore(MediaTime{-1});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, MediaErrorCode::kInvalidArgument);
    EXPECT_EQ(result.error().operation, MediaOperation::kMediaDescriptorValidation);
    EXPECT_FALSE(result.error().userMessageKey.empty());
}

TEST(FrameTimelineTests, OvershootTimeClampsToTheFinalFrame) {
    auto timeline = FrameTimeline::create(
        std::vector<MediaTime>{MediaTime{0}, MediaTime{16667}, MediaTime{33334}});
    ASSERT_TRUE(timeline);
    EXPECT_EQ(timeline.value().frameAtOrBefore(MediaTime{33334}).value(), FrameId{2});
    EXPECT_EQ(timeline.value().frameAtOrBefore(MediaTime{1000000}).value(), FrameId{2});
}

TEST(FrameTimelineTests, FrameStartTimeRejectsOutOfRangeFrameIds) {
    auto timeline = FrameTimeline::create(
        std::vector<MediaTime>{MediaTime{0}, MediaTime{16667}, MediaTime{33334}});
    ASSERT_TRUE(timeline);

    EXPECT_FALSE(timeline.value().frameStartTime(FrameId{3}));
    EXPECT_FALSE(timeline.value().frameStartTime(FrameId{-1}));
    EXPECT_EQ(timeline.value().frameStartTime(FrameId{0}).value(), MediaTime{0});
    EXPECT_EQ(timeline.value().frameStartTime(FrameId{2}).value(), MediaTime{33334});
}

TEST(FrameTimelineHelpersTests, StableUserMessageKeyForEveryTimelineOutcome) {
    EXPECT_EQ(stableId(MediaErrorCode::kFrameTimelineInvalid), "frame-timeline-invalid");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidFrameId), "invalid-frame-id");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidArgument), "invalid-argument");
}

TEST(CanonicalTimelineTests, RationalRatePathReturnsCeilingBoundaries) {
    auto rate = RationalRate::create(30, 1);
    ASSERT_TRUE(rate);
    const CanonicalTimeline timeline{rate.value()};
    EXPECT_FALSE(isVariableFrameRate(timeline));
    EXPECT_EQ(canonicalFrameStartTime(timeline, FrameId{0}).value(), MediaTime{0});
    EXPECT_EQ(canonicalFrameStartTime(timeline, FrameId{1}).value(), MediaTime{33334});
    EXPECT_EQ(canonicalFrameAtOrBefore(timeline, MediaTime{33333}).value(), FrameId{0});
    EXPECT_EQ(canonicalFrameAtOrBefore(timeline, MediaTime{33334}).value(), FrameId{1});
}

TEST(CanonicalTimelineTests, VariablePathDispatchesThroughSharedTimeline) {
    auto variable = FrameTimeline::create(
        std::vector<MediaTime>{MediaTime{0}, MediaTime{40000}, MediaTime{95000}});
    ASSERT_TRUE(variable);
    const auto shared = std::make_shared<const FrameTimeline>(std::move(variable).value());
    const CanonicalTimeline timeline{shared};
    EXPECT_TRUE(isVariableFrameRate(timeline));
    EXPECT_EQ(canonicalFrameStartTime(timeline, FrameId{2}).value(), MediaTime{95000});
    EXPECT_EQ(canonicalFrameAtOrBefore(timeline, MediaTime{94999}).value(), FrameId{1});
    EXPECT_EQ(canonicalFrameAtOrBefore(timeline, MediaTime{95000}).value(), FrameId{2});
}

TEST(CanonicalTimelineTests, NullSharedTimelineIsAnErrorNotUndefinedBehavior) {
    const CanonicalTimeline timeline{std::shared_ptr<const FrameTimeline>{}};
    EXPECT_TRUE(isVariableFrameRate(timeline));
    EXPECT_FALSE(canonicalFrameStartTime(timeline, FrameId{0}));
    EXPECT_FALSE(canonicalFrameAtOrBefore(timeline, MediaTime{0}));
}

} // namespace
} // namespace dvs::domain
