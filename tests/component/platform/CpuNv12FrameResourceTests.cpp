#include "dvs/platform/FrameResourceFactory.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <utility>
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

TEST(CpuNv12FrameResourceTests, WritableAcquisitionSealsWithoutAPlaneCopy) {
    const Nv12FrameLayout layout{
        .width = 4,
        .height = 2,
        .yStride = 4,
        .uvStride = 4,
    };
    FrameBudget budget{12U};
    FrameResourceFactory factory{budget};
    Nv12BufferPool pool;

    std::optional<WritableCpuNv12Frame> writable =
        factory.acquireWritableCpuNv12(layout, domain::ColorMetadata{}, pool);
    ASSERT_TRUE(writable.has_value());
    std::fill(writable->yPlane().begin(), writable->yPlane().end(), 17U);
    std::fill(writable->uvPlane().begin(), writable->uvPlane().end(), 29U);
    const std::uint8_t* const writableAddress = writable->yPlane().data();

    std::optional<application::FrameHandle> handle = std::move(*writable).seal();
    ASSERT_TRUE(handle.has_value());
    const auto resource = std::dynamic_pointer_cast<const CpuNv12FrameResource>(handle->resource());
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(resource->yPlane().data(), writableAddress);
    EXPECT_EQ(resource->yPlane().front(), 17U);
    EXPECT_EQ(resource->uvPlane().front(), 29U);
}

TEST(CpuNv12FrameResourceTests, BufferPoolReusesReleasedStorage) {
    Nv12BufferPool pool{1U};
    const std::uint8_t* firstAddress = nullptr;
    {
        std::optional<Nv12BufferPool::Buffer> first = pool.acquire(4'096U);
        ASSERT_TRUE(first.has_value());
        firstAddress = first->bytes().data();
    }
    std::optional<Nv12BufferPool::Buffer> second = pool.acquire(4'096U);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->bytes().data(), firstAddress);
}

TEST(CpuP010FrameResourceTests, PreservesSixteenBitPlanesAndAccountsTheirFullBudget) {
    const P010FrameLayout layout{
        .width = 3U,
        .height = 3U,
        .yStride = 8U,
        .uvStride = 8U,
    };
    const std::optional<P010PlaneByteCounts> byteCounts = layout.byteCounts();
    ASSERT_TRUE(byteCounts.has_value());
    EXPECT_EQ(byteCounts->y, 24U);
    EXPECT_EQ(byteCounts->uv, 16U);

    FrameBudget budget{byteCounts->total()};
    FrameResourceFactory factory{budget};
    std::vector<std::uint8_t> y(byteCounts->y, 0U);
    std::vector<std::uint8_t> uv(byteCounts->uv, 0U);
    y[0U] = 0x00U;
    y[1U] = 0x40U;
    uv[0U] = 0x00U;
    uv[1U] = 0x80U;
    auto handle = factory.createCpuP010(layout, domain::ColorMetadata{}, y, uv);
    ASSERT_TRUE(handle.has_value());

    const auto resource = std::dynamic_pointer_cast<const CpuP010FrameResource>(handle->resource());
    ASSERT_NE(resource, nullptr);
    EXPECT_EQ(resource->yPlane()[1U], 0x40U);
    EXPECT_EQ(resource->uvPlane()[1U], 0x80U);
    EXPECT_EQ(resource->byteCount(), 40U);
    EXPECT_EQ(handle->accountedBytes(), 40U);
}

TEST(CpuP010FrameResourceTests, RejectsOddOrUndersizedByteStrides) {
    EXPECT_FALSE((P010FrameLayout{
                      .width = 3U,
                      .height = 2U,
                      .yStride = 6U,
                      .uvStride = 7U,
                  })
                     .isValid());
    EXPECT_FALSE((P010FrameLayout{
                      .width = 3U,
                      .height = 2U,
                      .yStride = 5U,
                      .uvStride = 8U,
                  })
                     .isValid());
}

} // namespace dvs::platform
