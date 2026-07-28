#include "dvs/domain/Clip.h"

#include <gtest/gtest.h>

namespace dvs::domain {

TEST(ClipTests, ValidatesInclusiveRangesAndConvertsToHalfOpenSpans) {
    const auto negative = FrameRange::inclusive(FrameId{-1}, FrameId{0});
    ASSERT_FALSE(negative.hasValue());
    EXPECT_EQ(negative.error().code, MediaErrorCode::kInvalidFrameRange);

    const auto reversed = FrameRange::inclusive(FrameId{4}, FrameId{2});
    ASSERT_FALSE(reversed.hasValue());
    EXPECT_EQ(reversed.error().code, MediaErrorCode::kInvalidFrameRange);

    const auto range = FrameRange::inclusive(FrameId{2}, FrameId{4});
    ASSERT_TRUE(range.hasValue());
    EXPECT_TRUE(range.value().isWithin(5));
    EXPECT_FALSE(range.value().isWithin(4));

    const auto span = range.value().toHalfOpen();
    ASSERT_TRUE(span.hasValue());
    EXPECT_TRUE(span.value().isValid());
    EXPECT_EQ(span.value().first, FrameId{2});
    EXPECT_EQ(span.value().endExclusive, 5);
    EXPECT_EQ(span.value().frameCount(), 3);
}

TEST(ClipTests, RejectsInvalidFrameSpans) {
    const FrameSpan empty{.first = FrameId{0}, .endExclusive = 0};
    EXPECT_FALSE(empty.isValid());
    EXPECT_EQ(empty.frameCount(), 0);
}

} // namespace dvs::domain
