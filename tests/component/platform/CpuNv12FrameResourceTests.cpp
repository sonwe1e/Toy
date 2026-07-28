#include "dvs/platform/FrameResourceFactory.h"

#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace dvs::platform {

TEST(CpuNv12FrameResourceTests, ComputesOddDimensionLayoutAndCopiesImmutablePlanes) {
    const Nv12FrameLayout layout{
        .width = 3,
        .height = 3,
        .yStride = 3,
        .uvStride = 4,
    };
    const auto byteCounts = layout.byteCounts();
    ASSERT_TRUE(byteCounts.has_value());
    EXPECT_EQ(byteCounts->y, 9U);
    EXPECT_EQ(byteCounts->uv, 8U);

    FrameBudget budget{byteCounts->total()};
    FrameResourceFactory factory{budget};
    std::vector<std::uint8_t> y(byteCounts->y, 7);
    std::vector<std::uint8_t> uv(byteCounts->uv, 9);
    domain::ColorMetadata colorMetadata{
        .matrix = domain::ColorMatrix::kBt709,
        .range = domain::ColorRange::kFull,
        .matrixInferred = true,
    };
    auto handle = factory.createCpuNv12(layout, colorMetadata, y, uv);
    ASSERT_TRUE(handle.has_value());
    EXPECT_EQ(handle->accountedBytes(), byteCounts->total());
    EXPECT_EQ(budget.reservedBytes(), byteCounts->total());

    auto resource = std::dynamic_pointer_cast<const CpuNv12FrameResource>(handle->resource());
    ASSERT_NE(resource, nullptr);
    y.front() = 1;
    uv.front() = 2;
    colorMetadata = domain::ColorMetadata{};
    EXPECT_EQ(resource->yPlane().front(), 7);
    EXPECT_EQ(resource->uvPlane().front(), 9);
    EXPECT_EQ(resource->byteCount(), byteCounts->total());
    EXPECT_EQ(resource->colorMetadata().matrix, domain::ColorMatrix::kBt709);
    EXPECT_EQ(resource->colorMetadata().range, domain::ColorRange::kFull);
    EXPECT_TRUE(resource->colorMetadata().matrixInferred);

    resource.reset();
    handle.reset();
    EXPECT_EQ(budget.reservedBytes(), 0U);
}

TEST(CpuNv12FrameResourceTests, RejectsInvalidLayoutsAndDoesNotLeakBudgetOnFailure) {
    const Nv12FrameLayout invalid{
        .width = 3,
        .height = 2,
        .yStride = 3,
        .uvStride = 3,
    };
    EXPECT_FALSE(invalid.isValid());

    const Nv12FrameLayout valid{
        .width = 2,
        .height = 2,
        .yStride = 2,
        .uvStride = 2,
    };
    const auto byteCounts = valid.byteCounts();
    ASSERT_TRUE(byteCounts.has_value());
    FrameBudget budget{byteCounts->total() - 1U};
    FrameResourceFactory factory{budget};
    const std::vector<std::uint8_t> y(byteCounts->y, 0);
    const std::vector<std::uint8_t> uv(byteCounts->uv, 0);
    const domain::ColorMetadata colorMetadata{};

    EXPECT_FALSE(factory.createCpuNv12(valid, colorMetadata, y, uv).has_value());
    EXPECT_EQ(budget.reservedBytes(), 0U);
}

} // namespace dvs::platform
