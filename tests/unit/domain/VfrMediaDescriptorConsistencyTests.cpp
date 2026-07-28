#include "dvs/domain/MediaDescriptor.h"
#include "dvs/domain/RationalRate.h"

#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <utility>

namespace dvs::domain {
namespace {

[[nodiscard]] MediaDescriptor baseDescriptor() {
    return MediaDescriptor{
        .normalizedPath = std::filesystem::path{"source.mp4"},
        .extent = MediaExtent{.width = 1920, .height = 1080},
        .frameRate = RationalRate::create(30, 1).value(),
        .frameCount = FrameCountInfo{.value = 90, .origin = FrameCountOrigin::kReported},
        .duration = MediaTime{3000000},
        .codecId = "h264",
        .pixelFormatId = "yuv420p",
        .bitDepth = 8,
        .colorMetadata =
            ColorMetadata{.matrix = ColorMatrix::kBt709, .range = ColorRange::kLimited},
        .timingConfidence = TimingConfidence::kDeclaredCfr,
    };
}

TEST(VfrDescriptorConsistencyTests, CfrRequiresANominalRate) {
    auto descriptor = baseDescriptor();
    descriptor.frameRate = std::nullopt;
    descriptor.timingConfidence = TimingConfidence::kDeclaredCfr;

    const auto result = validateMediaDescriptor(std::move(descriptor));
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, MediaErrorCode::kInvalidMediaDescriptor);
    EXPECT_FALSE(result.error().technicalDetail.empty());
}

TEST(VfrDescriptorConsistencyTests, VfrRequiresNoNominalRate) {
    auto descriptor = baseDescriptor();
    descriptor.timingConfidence = TimingConfidence::kVariableFrameRate;

    const auto result = validateMediaDescriptor(std::move(descriptor));
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, MediaErrorCode::kInvalidMediaDescriptor);
    EXPECT_NE(result.error().technicalDetail.find("must not declare a nominal frame rate"),
              std::string::npos);
}

TEST(VfrDescriptorConsistencyTests, VfrRequiresAnIndexedFrameCount) {
    auto descriptor = baseDescriptor();
    descriptor.frameRate = std::nullopt;
    descriptor.timingConfidence = TimingConfidence::kVariableFrameRate;
    descriptor.frameCount.origin = FrameCountOrigin::kReported;

    const auto result = validateMediaDescriptor(std::move(descriptor));
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error().code, MediaErrorCode::kInvalidMediaDescriptor);
    EXPECT_NE(result.error().technicalDetail.find("indexed frame count"), std::string::npos);
}

TEST(VfrDescriptorConsistencyTests, VfrDescriptorWithIndexedCountIsValid) {
    auto descriptor = baseDescriptor();
    descriptor.frameRate = std::nullopt;
    descriptor.timingConfidence = TimingConfidence::kVariableFrameRate;
    descriptor.frameCount = FrameCountInfo{.value = 120, .origin = FrameCountOrigin::kIndexed};

    const auto result = validateMediaDescriptor(std::move(descriptor));
    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.value().frameRate.has_value());
}

TEST(VfrDescriptorConsistencyTests, CfrDescriptorWithRateIsValid) {
    const auto result = validateMediaDescriptor(baseDescriptor());
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result.value().frameRate.has_value());
}

} // namespace
} // namespace dvs::domain
