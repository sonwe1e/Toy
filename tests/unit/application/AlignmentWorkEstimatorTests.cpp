#include "dvs/application/AlignmentWorkEstimator.h"

#include <gtest/gtest.h>
#include <vector>

namespace dvs::application {
namespace {

[[nodiscard]] domain::ComparisonSource source(const domain::SourceId id,
                                              const std::int64_t frameCount) {
    return domain::ComparisonSource{
        .id = id,
        .descriptor =
            domain::MediaDescriptor{
                .frameCount =
                    domain::FrameCountInfo{
                        .value = frameCount,
                        .origin = domain::FrameCountOrigin::kIndexed,
                    },
                .duration = domain::MediaTime{frameCount},
            },
    };
}

[[nodiscard]] PlaybackRequestContext context() {
    return PlaybackRequestContext{
        .request =
            RequestContext{
                .sessionId = domain::SessionId{1U},
                .sessionEpoch = domain::SessionEpoch{1U},
                .requestId = domain::RequestId{1U},
            },
        .playbackGeneration = domain::PlaybackGeneration{1U},
    };
}

TEST(AlignmentWorkEstimatorTests, GlobalCountsCanonicalAndUniqueTargetSamples) {
    const AlignmentEstimateRequest request{
        .context = context(),
        .canonicalSourceId = 0U,
        .options =
            GlobalOffsetEstimationOptions{
                .minimumOffset = -2,
                .maximumOffset = 2,
                .selectedSampleCount = 5U,
                .minimumEvidence = 3U,
            },
        .candidateSampleCount = 5U,
        .sources = {source(0U, 100), source(1U, 100)},
    };

    const AlignmentWorkEstimate work = estimateAlignmentWork(request);

    EXPECT_EQ(work.totalUnits, 30U);
    EXPECT_EQ(work.unitName, "samples");
}

TEST(AlignmentWorkEstimatorTests, SequenceAddsEverySourceFrameAndEveryBandedDpCell) {
    const SequenceAlignmentRequest request{
        .context = context(),
        .canonicalSourceId = 0U,
        .options =
            SequenceAlignmentOptions{
                .bandWidth = 2U,
            },
        .sources = {source(0U, 10), source(1U, 12)},
    };

    const AlignmentWorkEstimate work = estimateAlignmentWork(request);

    EXPECT_EQ(work.totalUnits, 74U);
    EXPECT_EQ(work.unitName, "work units");
}

TEST(AlignmentWorkEstimatorTests, InvalidRequestsPublishNoInventedWork) {
    const AlignmentEstimateRequest global{
        .context = context(),
        .canonicalSourceId = 9U,
        .sources = {source(0U, 10), source(1U, 10)},
    };
    const SequenceAlignmentRequest sequence{
        .context = context(),
        .canonicalSourceId = 0U,
        .options =
            SequenceAlignmentOptions{
                .bandWidth = 0U,
            },
        .sources = {source(0U, 10), source(1U, 10)},
    };

    EXPECT_EQ(estimateAlignmentWork(global).totalUnits, 0U);
    EXPECT_EQ(estimateAlignmentWork(sequence).totalUnits, 0U);
}

} // namespace
} // namespace dvs::application
