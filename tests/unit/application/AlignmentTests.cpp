#include "dvs/application/Alignment.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

namespace dvs::application {
namespace {

[[nodiscard]] FrameLumaSignature syntheticSignature(const std::int64_t frame,
                                                    const std::int64_t contentFrame) {
    std::array<std::uint8_t, kAlignmentSignaturePixels> pixels{};
    for (std::size_t y = 0U; y < kAlignmentSignatureHeight; ++y) {
        for (std::size_t x = 0U; x < kAlignmentSignatureWidth; ++x) {
            const std::int64_t value = (contentFrame * 29) + (static_cast<std::int64_t>(x) * 17) +
                                       (static_cast<std::int64_t>(y) * 31) +
                                       ((contentFrame + static_cast<std::int64_t>(x * y)) % 7) * 19;
            pixels[(y * kAlignmentSignatureWidth) + x] = static_cast<std::uint8_t>(value & 0xFF);
        }
    }
    return makeFrameLumaSignature(domain::FrameId{frame}, pixels);
}

[[nodiscard]] std::vector<FrameLumaSignature> sequence(const std::int64_t count,
                                                       const std::int64_t contentOffset = 0) {
    std::vector<FrameLumaSignature> result;
    result.reserve(static_cast<std::size_t>(count));
    for (std::int64_t frame = 0; frame < count; ++frame) {
        result.push_back(syntheticSignature(frame, frame - contentOffset));
    }
    return result;
}

[[nodiscard]] std::vector<FrameLumaSignature>
sequenceFromContent(const std::vector<std::int64_t>& contentFrames) {
    std::vector<FrameLumaSignature> result;
    result.reserve(contentFrames.size());
    for (std::size_t frame = 0U; frame < contentFrames.size(); ++frame) {
        result.push_back(
            syntheticSignature(static_cast<std::int64_t>(frame), contentFrames[frame]));
    }
    return result;
}

} // namespace

TEST(AlignmentTests, FindsPositiveTargetFrameOffsetWithStrongEvidence) {
    const std::vector<FrameLumaSignature> reference = sequence(24);
    const std::vector<FrameLumaSignature> target = sequence(28, 2);

    const std::optional<GlobalOffsetEstimate> estimate =
        estimateGlobalOffset(1U,
                             reference,
                             target,
                             GlobalOffsetEstimationOptions{
                                 .minimumOffset = -4,
                                 .maximumOffset = 4,
                                 .selectedSampleCount = 12U,
                                 .minimumEvidence = 6U,
                                 .minimumConfidence = 0.10F,
                                 .maximumBestCost = 0.20F,
                             });

    ASSERT_TRUE(estimate.has_value());
    EXPECT_EQ(estimate->sourceId, 1U);
    EXPECT_EQ(estimate->bestOffset, 2);
    EXPECT_FLOAT_EQ(estimate->bestCost, 0.0F);
    EXPECT_GT(estimate->confidence, 0.9F);
    EXPECT_TRUE(estimate->autoApplicable);
}

TEST(AlignmentTests, FindsNegativeTargetFrameOffsetAtSequenceBoundary) {
    const std::vector<FrameLumaSignature> reference = sequence(28);
    const std::vector<FrameLumaSignature> target = sequence(24, -3);

    const std::optional<GlobalOffsetEstimate> estimate =
        estimateGlobalOffset(2U,
                             reference,
                             target,
                             GlobalOffsetEstimationOptions{
                                 .minimumOffset = -5,
                                 .maximumOffset = 5,
                                 .selectedSampleCount = 16U,
                                 .minimumEvidence = 6U,
                             });

    ASSERT_TRUE(estimate.has_value());
    EXPECT_EQ(estimate->bestOffset, -3);
    EXPECT_TRUE(estimate->autoApplicable);
}

TEST(AlignmentTests, KeepsAmbiguousConstantEvidenceAsSuggestionOnly) {
    std::array<std::uint8_t, kAlignmentSignaturePixels> pixels{};
    pixels.fill(96U);
    std::vector<FrameLumaSignature> reference;
    std::vector<FrameLumaSignature> target;
    for (std::int64_t frame = 0; frame < 12; ++frame) {
        reference.push_back(makeFrameLumaSignature(domain::FrameId{frame}, pixels));
        target.push_back(makeFrameLumaSignature(domain::FrameId{frame}, pixels));
    }

    const std::optional<GlobalOffsetEstimate> estimate =
        estimateGlobalOffset(1U,
                             reference,
                             target,
                             GlobalOffsetEstimationOptions{
                                 .minimumOffset = -2,
                                 .maximumOffset = 2,
                                 .selectedSampleCount = 9U,
                                 .minimumEvidence = 5U,
                             });

    ASSERT_TRUE(estimate.has_value());
    EXPECT_FLOAT_EQ(estimate->bestCost, 0.0F);
    EXPECT_FLOAT_EQ(estimate->runnerUpCost, 0.0F);
    EXPECT_FLOAT_EQ(estimate->confidence, 0.0F);
    EXPECT_FALSE(estimate->autoApplicable);
}

TEST(AlignmentTests, RejectsMalformedOptionsAndInsufficientEvidence) {
    const std::vector<FrameLumaSignature> shortSequence = sequence(2);
    EXPECT_FALSE(estimateGlobalOffset(1U, shortSequence, shortSequence).has_value());

    GlobalOffsetEstimationOptions invalid;
    invalid.minimumOffset = 4;
    invalid.maximumOffset = -4;
    EXPECT_FALSE(estimateGlobalOffset(1U, sequence(12), sequence(12), invalid).has_value());
}

TEST(AlignmentTests, BandedSequenceAlignmentMapsAnExactSequenceWithoutAnomalies) {
    const auto reference = sequence(16);
    const auto target = sequence(16);
    const auto result = alignFrameSequences(1U, reference, target);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->entries.size(), reference.size());
    EXPECT_TRUE(result->anomalies.empty());
    EXPECT_FLOAT_EQ(result->meanMatchCost, 0.0F);
    EXPECT_TRUE(result->autoApplicable);
    for (std::size_t index = 0U; index < result->entries.size(); ++index) {
        EXPECT_EQ(result->entries[index].canonicalFrameId,
                  domain::FrameId{static_cast<std::int64_t>(index)});
        EXPECT_EQ(result->entries[index].sourceFrameId,
                  domain::FrameId{static_cast<std::int64_t>(index)});
    }
}

TEST(AlignmentTests, BandedSequenceAlignmentMarksAMissingTargetFrame) {
    const auto reference = sequence(10);
    const auto target = sequenceFromContent({0, 1, 2, 3, 5, 6, 7, 8, 9});
    const auto result = alignFrameSequences(1U, reference, target);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->entries.size(), reference.size());
    EXPECT_FALSE(result->entries[4U].sourceFrameId.has_value());
    EXPECT_EQ(result->entries[4U].matchKind, FrameMatchKind::Missing);
    ASSERT_EQ(result->anomalies.size(), 1U);
    EXPECT_EQ(result->anomalies.front().kind, SequenceAlignmentAnomalyKind::TargetFrameMissing);
    EXPECT_EQ(result->anomalies.front().canonicalFrameId, domain::FrameId{4});
}

TEST(AlignmentTests, BandedSequenceAlignmentDistinguishesDuplicateAndExtraTargetFrames) {
    const auto reference = sequence(10);
    const auto duplicate = sequenceFromContent({0, 1, 2, 3, 4, 4, 5, 6, 7, 8, 9});
    const auto duplicateResult = alignFrameSequences(1U, reference, duplicate);
    ASSERT_TRUE(duplicateResult.has_value());
    ASSERT_EQ(duplicateResult->anomalies.size(), 1U);
    EXPECT_EQ(duplicateResult->anomalies.front().kind,
              SequenceAlignmentAnomalyKind::TargetFrameDuplicate);
    ASSERT_TRUE(duplicateResult->anomalies.front().sourceFrameId.has_value());
    EXPECT_TRUE(duplicateResult->anomalies.front().sourceFrameId == domain::FrameId{4} ||
                duplicateResult->anomalies.front().sourceFrameId == domain::FrameId{5});

    const auto extra = sequenceFromContent({0, 1, 2, 3, 77, 4, 5, 6, 7, 8, 9});
    const auto extraResult = alignFrameSequences(1U, reference, extra);
    ASSERT_TRUE(extraResult.has_value());
    ASSERT_EQ(extraResult->anomalies.size(), 1U);
    EXPECT_EQ(extraResult->anomalies.front().kind, SequenceAlignmentAnomalyKind::TargetFrameExtra);
    EXPECT_EQ(extraResult->anomalies.front().sourceFrameId, domain::FrameId{4});
}

TEST(AlignmentTests, BandedSequenceAlignmentKeepsConstantEvidenceSuggestionOnly) {
    std::array<std::uint8_t, kAlignmentSignaturePixels> pixels{};
    pixels.fill(96U);
    std::vector<FrameLumaSignature> reference;
    std::vector<FrameLumaSignature> target;
    for (std::int64_t frame = 0; frame < 12; ++frame) {
        reference.push_back(makeFrameLumaSignature(domain::FrameId{frame}, pixels));
        target.push_back(makeFrameLumaSignature(domain::FrameId{frame}, pixels));
    }

    const auto result = alignFrameSequences(1U, reference, target);
    ASSERT_TRUE(result.has_value());
    EXPECT_FLOAT_EQ(result->confidence, 0.0F);
    EXPECT_FALSE(result->autoApplicable);
}

TEST(AlignmentTests, ManualAnchorsInterpolateMonotonicallyAndExtendBoundaryOffsets) {
    const SourceAlignmentAnchors anchors{
        .sourceId = 2U,
        .anchors =
            {
                ManualAlignmentAnchor{
                    .canonicalFrameId = domain::FrameId{2},
                    .sourceFrameId = domain::FrameId{3},
                },
                ManualAlignmentAnchor{
                    .canonicalFrameId = domain::FrameId{6},
                    .sourceFrameId = domain::FrameId{9},
                },
            },
    };
    ASSERT_TRUE(anchors.isValid());

    const auto before = mapFrameWithAnchors(anchors, domain::FrameId{0}, 12);
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(before->frames, 1);
    EXPECT_EQ(before->matchKind, FrameMatchKind::ManualAnchor);

    const auto between = mapFrameWithAnchors(anchors, domain::FrameId{4}, 12);
    ASSERT_TRUE(between.has_value());
    EXPECT_EQ(between->frames, 2);
    EXPECT_EQ(between->matchKind, FrameMatchKind::ManualAnchor);

    const auto beyond = mapFrameWithAnchors(anchors, domain::FrameId{9}, 12);
    ASSERT_TRUE(beyond.has_value());
    EXPECT_EQ(beyond->matchKind, FrameMatchKind::Missing);
}

TEST(AlignmentTests, ManualAnchorsRejectCrossingAndMalformedInput) {
    const SourceAlignmentAnchors crossing{
        .sourceId = 1U,
        .anchors =
            {
                ManualAlignmentAnchor{
                    .canonicalFrameId = domain::FrameId{2},
                    .sourceFrameId = domain::FrameId{5},
                },
                ManualAlignmentAnchor{
                    .canonicalFrameId = domain::FrameId{6},
                    .sourceFrameId = domain::FrameId{4},
                },
            },
    };
    EXPECT_FALSE(crossing.isValid());
    EXPECT_FALSE(mapFrameWithAnchors(crossing, domain::FrameId{3}, 12).has_value());
}

} // namespace dvs::application
