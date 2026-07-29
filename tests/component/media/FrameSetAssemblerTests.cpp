#include "FrameSetAssembler.h"

#include <gtest/gtest.h>
#include <optional>
#include <vector>

namespace dvs::media::internal {
namespace {

[[nodiscard]] application::MappedSourceFrame missing(const domain::SourceId sourceId) {
    return application::MappedSourceFrame{
        .sourceId = sourceId,
        .matchKind = application::FrameMatchKind::Missing,
        .missingReason = application::MissingReason::AlignmentGap,
    };
}

TEST(FrameSetAssemblerTests, RefusesPartialSetsAndPublishesInSessionSourceOrder) {
    FrameSetAssembler assembler{
        domain::FrameId{7},
        domain::MediaTime{233'333},
        std::vector<domain::SourceId>{10U, 20U, 30U},
    };

    EXPECT_TRUE(assembler.complete(missing(30U)));
    EXPECT_TRUE(assembler.complete(missing(10U)));
    EXPECT_EQ(assembler.completedSlotCount(), 2U);
    EXPECT_FALSE(assembler.finish().has_value());

    EXPECT_TRUE(assembler.complete(missing(20U)));
    const std::optional<application::FrameSet> set = assembler.finish();
    ASSERT_TRUE(set.has_value());
    ASSERT_EQ(set->sources().size(), 3U);
    EXPECT_EQ(set->sources()[0U].sourceId, 10U);
    EXPECT_EQ(set->sources()[1U].sourceId, 20U);
    EXPECT_EQ(set->sources()[2U].sourceId, 30U);
}

TEST(FrameSetAssemblerTests, RejectsUnknownAndDuplicateSourceCompletions) {
    FrameSetAssembler assembler{
        domain::FrameId{0},
        domain::MediaTime{0},
        std::vector<domain::SourceId>{1U, 2U},
    };

    EXPECT_FALSE(assembler.complete(missing(3U)));
    EXPECT_TRUE(assembler.complete(missing(1U)));
    EXPECT_FALSE(assembler.complete(missing(1U)));
    EXPECT_FALSE(assembler.finish().has_value());
}

} // namespace
} // namespace dvs::media::internal
