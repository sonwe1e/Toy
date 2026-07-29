#include "dvs/domain/FrameTimeline.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace dvs::domain {
namespace {

TEST(FrameTimelineTests, PreservesVariableCadenceAndFindsTheFrameAtEachBoundary) {
    auto timeline = FrameTimeline::create(std::vector<MediaTime>{
        MediaTime{0}, MediaTime{16'667}, MediaTime{33'334}, MediaTime{83'334}});

    ASSERT_TRUE(timeline);
    EXPECT_EQ(timeline.value().frameCount(), 4);
    EXPECT_EQ(timeline.value().frameStartTime(FrameId{3}).value(), MediaTime{83'334});
    EXPECT_EQ(timeline.value().frameAtOrBefore(MediaTime{0}).value(), FrameId{0});
    EXPECT_EQ(timeline.value().frameAtOrBefore(MediaTime{33'333}).value(), FrameId{1});
    EXPECT_EQ(timeline.value().frameAtOrBefore(MediaTime{33'334}).value(), FrameId{2});
    EXPECT_EQ(timeline.value().frameAtOrBefore(MediaTime{1'000'000}).value(), FrameId{3});
}

TEST(FrameTimelineTests, RejectsMalformedNormalizedDisplayTimes) {
    const auto empty = FrameTimeline::create({});
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.error().code, MediaErrorCode::kFrameTimelineInvalid);

    const auto nonZeroStart =
        FrameTimeline::create(std::vector<MediaTime>{MediaTime{1}, MediaTime{2}});
    ASSERT_FALSE(nonZeroStart);
    EXPECT_EQ(nonZeroStart.error().code, MediaErrorCode::kFrameTimelineInvalid);

    const auto duplicate =
        FrameTimeline::create(std::vector<MediaTime>{MediaTime{0}, MediaTime{1}, MediaTime{1}});
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, MediaErrorCode::kFrameTimelineInvalid);

    const auto reversed =
        FrameTimeline::create(std::vector<MediaTime>{MediaTime{0}, MediaTime{2}, MediaTime{1}});
    ASSERT_FALSE(reversed);
    EXPECT_EQ(reversed.error().code, MediaErrorCode::kFrameTimelineInvalid);
}

TEST(FrameTimelineTests, RejectsInvalidLookupArguments) {
    auto timeline =
        FrameTimeline::create(std::vector<MediaTime>{MediaTime{0}, MediaTime{10}, MediaTime{20}});
    ASSERT_TRUE(timeline);

    const auto invalidFrame = timeline.value().frameStartTime(FrameId{-1});
    ASSERT_FALSE(invalidFrame);
    EXPECT_EQ(invalidFrame.error().code, MediaErrorCode::kInvalidFrameId);

    const auto pastEnd = timeline.value().frameStartTime(FrameId{3});
    ASSERT_FALSE(pastEnd);
    EXPECT_EQ(pastEnd.error().code, MediaErrorCode::kInvalidFrameId);

    const auto negativeTime = timeline.value().frameAtOrBefore(MediaTime{-1});
    ASSERT_FALSE(negativeTime);
    EXPECT_EQ(negativeTime.error().code, MediaErrorCode::kInvalidArgument);
}

TEST(FrameTimelineTests, CanonicalHelpersDispatchToRatesAndSharedVariableTimelines) {
    auto rate = RationalRate::create(25, 1);
    ASSERT_TRUE(rate);
    const CanonicalTimeline cfr{rate.value()};
    EXPECT_EQ(canonicalFrameStartTime(cfr, FrameId{2}).value(), MediaTime{80'000});

    auto variable = FrameTimeline::create(
        std::vector<MediaTime>{MediaTime{0}, MediaTime{40'000}, MediaTime{95'000}});
    ASSERT_TRUE(variable);
    auto shared = std::make_shared<const FrameTimeline>(std::move(variable).value());
    const CanonicalTimeline vfr{shared};
    EXPECT_EQ(canonicalFrameStartTime(vfr, FrameId{2}).value(), MediaTime{95'000});
    EXPECT_EQ(canonicalFrameAtOrBefore(vfr, MediaTime{94'999}).value(), FrameId{1});
}

} // namespace
} // namespace dvs::domain
