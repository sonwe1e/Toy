#include "dvs/domain/SourcePairValidator.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <utility>

namespace dvs::domain {
namespace {

[[nodiscard]] RationalRate makeRate(const std::int64_t numerator = 30,
                                    const std::int64_t denominator = 1) {
    auto result = RationalRate::create(numerator, denominator);
    EXPECT_TRUE(result.hasValue());
    return std::move(result).value();
}

[[nodiscard]] MediaDescriptor
makeDescriptor(const std::filesystem::path& path,
               const RationalRate& rate,
               const std::int64_t frameCount,
               const std::int64_t duration,
               const MediaExtent extent,
               const FrameCountOrigin origin = FrameCountOrigin::kReported) {
    return MediaDescriptor{
        .normalizedPath = path,
        .extent = extent,
        .frameRate = rate,
        .frameCount = FrameCountInfo{.value = frameCount, .origin = origin},
        .duration = MediaTime{duration},
        .codecId = "h264",
        .pixelFormatId = "yuv420p",
        .bitDepth = 8,
        .decodeCapabilities = DecodeCapabilities{.softwareDecode = true, .d3d11VaDecode = false},
        .timingConfidence = TimingConfidence::kDeclaredCfr,
        .sourceIdentity = std::nullopt,
    };
}

} // namespace

TEST(SourcePairValidatorTests, AcceptsDifferentResolutionsAndTracksEstimatedCounts) {
    const RationalRate rate = makeRate();
    const auto sourceA =
        makeDescriptor("a.mp4", rate, 90, 3'000'000, MediaExtent{.width = 1'920, .height = 1'080});
    const auto sourceB = makeDescriptor("b.mp4",
                                        rate,
                                        90,
                                        3'033'333,
                                        MediaExtent{.width = 1'280, .height = 720},
                                        FrameCountOrigin::kEstimated);

    const auto validated = SourcePairValidator::validate(sourceA, sourceB);
    ASSERT_TRUE(validated.hasValue());
    EXPECT_EQ(validated.value().canonicalFrameCount(), 90);
    EXPECT_TRUE(validated.value().hasEstimatedFrameCount());
    EXPECT_EQ(validated.value().sourceB().extent.width, 1'280U);
}

TEST(SourcePairValidatorTests, RejectsRateCountAndDurationMismatches) {
    const RationalRate rate = makeRate();
    const RationalRate fasterRate = makeRate(60);
    const auto sourceA =
        makeDescriptor("a.mp4", rate, 90, 3'000'000, MediaExtent{.width = 1'920, .height = 1'080});
    const auto rateMismatch = makeDescriptor(
        "b.mp4", fasterRate, 90, 3'000'000, MediaExtent{.width = 1'920, .height = 1'080});
    const auto countMismatch =
        makeDescriptor("b.mp4", rate, 89, 3'000'000, MediaExtent{.width = 1'920, .height = 1'080});
    const auto durationMismatch =
        makeDescriptor("b.mp4", rate, 90, 3'033'335, MediaExtent{.width = 1'920, .height = 1'080});

    const auto rateResult = SourcePairValidator::validate(sourceA, rateMismatch);
    ASSERT_TRUE(rateResult.hasValue());

    const auto countResult = SourcePairValidator::validate(sourceA, countMismatch);
    ASSERT_FALSE(countResult.hasValue());
    EXPECT_EQ(countResult.error().code, MediaErrorCode::kSourceFrameCountMismatch);

    const auto durationResult = SourcePairValidator::validate(sourceA, durationMismatch);
    ASSERT_TRUE(durationResult.hasValue());
}

TEST(SourcePairValidatorTests, AcceptsVariableSourcesWithoutNominalRates) {
    const RationalRate rate = makeRate();
    auto sourceA =
        makeDescriptor("a.mp4", rate, 12, 500'000, MediaExtent{.width = 640, .height = 360});
    auto sourceB =
        makeDescriptor("b.mp4", rate, 12, 900'000, MediaExtent{.width = 640, .height = 360});
    sourceA.frameRate = std::nullopt;
    sourceB.frameRate = std::nullopt;
    sourceA.frameCount.origin = FrameCountOrigin::kIndexed;
    sourceB.frameCount.origin = FrameCountOrigin::kIndexed;
    sourceA.timingConfidence = TimingConfidence::kVariableFrameRate;
    sourceB.timingConfidence = TimingConfidence::kVariableFrameRate;

    const auto result = SourcePairValidator::validate(sourceA, sourceB);
    ASSERT_TRUE(result);
    EXPECT_FALSE(result.value().canonicalRate().has_value());
}

TEST(SourcePairValidatorTests, IdentifiesInvalidDescriptorSide) {
    const RationalRate rate = makeRate();
    auto invalidA =
        makeDescriptor("a.mp4", rate, 90, 3'000'000, MediaExtent{.width = 1'920, .height = 1'080});
    const auto validB =
        makeDescriptor("b.mp4", rate, 90, 3'000'000, MediaExtent{.width = 1'920, .height = 1'080});
    invalidA.codecId.clear();

    const auto invalidAResult = SourcePairValidator::validate(invalidA, validB);
    ASSERT_FALSE(invalidAResult.hasValue());
    EXPECT_EQ(invalidAResult.error().sourceRole, SourceRole::kA);

    auto invalidB = validB;
    invalidB.extent.height = 0;
    const auto invalidBResult = SourcePairValidator::validate(validB, invalidB);
    ASSERT_FALSE(invalidBResult.hasValue());
    EXPECT_EQ(invalidBResult.error().sourceRole, SourceRole::kB);
}

} // namespace dvs::domain
