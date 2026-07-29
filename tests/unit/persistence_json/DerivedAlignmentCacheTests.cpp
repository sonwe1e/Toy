#include "dvs/application/AlignmentCacheIdentity.h"
#include "dvs/domain/ComparisonValidator.h"
#include "dvs/persistence/DerivedAlignmentCache.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

namespace dvs::persistence {
namespace {

std::atomic<std::uint64_t> nextCacheDirectory{0U};

class ScopedCacheDirectory final {
public:
    ScopedCacheDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                ("dvs-derived-cache-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                 std::to_string(nextCacheDirectory.fetch_add(1U)));
    }

    ~ScopedCacheDirectory() {
        std::error_code errorCode;
        std::filesystem::remove_all(path_, errorCode);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] domain::ValidatedComparisonSet makeSet(const char secondFingerprint = 'b') {
    auto rate = domain::RationalRate::create(30, 1);
    EXPECT_TRUE(rate);
    const auto descriptor = [&rate](const std::filesystem::path& path, const char fingerprint) {
        return domain::MediaDescriptor{
            .normalizedPath = path,
            .extent = domain::MediaExtent{.width = 320U, .height = 180U},
            .frameRate = rate.value(),
            .frameCount =
                domain::FrameCountInfo{
                    .value = 3,
                    .origin = domain::FrameCountOrigin::kIndexed,
                },
            .duration = domain::MediaTime{100'000},
            .codecId = "h264",
            .pixelFormatId = "nv12",
            .bitDepth = 8U,
            .decodeCapabilities =
                domain::DecodeCapabilities{
                    .softwareDecode = true,
                    .d3d11VaDecode = true,
                },
            .timingConfidence = domain::TimingConfidence::kVerifiedCfr,
            .sourceIdentity =
                domain::SourceFileIdentity{
                    .byteSize = 1'024U,
                    .modifiedUtcMilliseconds = 2'048,
                    .fingerprintSha256 = std::string(64U, fingerprint),
                },
        };
    };
    auto validated = domain::ComparisonValidator::validate({
        domain::ComparisonSource{
            .id = 0U,
            .role = domain::ComparisonRole::kReference,
            .descriptor = descriptor("C:/media/a.mp4", 'a'),
            .displayName = "A",
        },
        domain::ComparisonSource{
            .id = 1U,
            .role = domain::ComparisonRole::kPrediction,
            .descriptor = descriptor("C:/media/b.mp4", secondFingerprint),
            .displayName = "B",
        },
    });
    EXPECT_TRUE(validated);
    return std::move(validated).value().set;
}

[[nodiscard]] std::vector<application::SequenceAlignmentResult> makeResults() {
    return {
        application::SequenceAlignmentResult{
            .sourceId = 1U,
            .entries =
                {
                    application::SequenceAlignmentEntry{
                        .canonicalFrameId = domain::FrameId{0},
                        .sourceFrameId = domain::FrameId{0},
                        .matchKind = application::FrameMatchKind::AutoAligned,
                        .confidence = 0.9F,
                    },
                    application::SequenceAlignmentEntry{
                        .canonicalFrameId = domain::FrameId{1},
                        .sourceFrameId = std::nullopt,
                        .matchKind = application::FrameMatchKind::Missing,
                        .confidence = 0.2F,
                    },
                    application::SequenceAlignmentEntry{
                        .canonicalFrameId = domain::FrameId{2},
                        .sourceFrameId = domain::FrameId{2},
                        .matchKind = application::FrameMatchKind::AutoAligned,
                        .confidence = 0.8F,
                    },
                },
            .anomalies =
                {
                    application::SequenceAlignmentAnomaly{
                        .kind = application::SequenceAlignmentAnomalyKind::TargetFrameMissing,
                        .canonicalFrameId = domain::FrameId{1},
                    },
                },
            .segments =
                {
                    application::SequenceAlignmentSegment{
                        .firstCanonicalFrame = domain::FrameId{0},
                        .lastCanonicalFrame = domain::FrameId{2},
                        .state = application::AlignmentSegmentState::Accepted,
                        .meanConfidence = 0.7F,
                        .p10Confidence = 0.2F,
                        .maximumLowConfidenceRun = 1U,
                        .anomalyDensity = 0.34F,
                        .mappingSlope = 1.0F,
                    },
                },
            .totalCost = 0.3F,
            .meanMatchCost = 0.1F,
            .confidence = 0.7F,
            .autoApplicable = true,
        },
    };
}

TEST(DerivedAlignmentCacheTests, RoundTripsAnOpaqueFingerprintBoundCache) {
    ScopedCacheDirectory directory;
    const domain::ValidatedComparisonSet sources = makeSet();
    const std::vector<application::SequenceAlignmentResult> results = makeResults();
    const std::string key = application::makeDerivedAlignmentCacheKey(sources, results);
    const DerivedAlignmentCache cache{directory.path()};

    ASSERT_TRUE(cache.store(key, sources, results, 1U));
    auto loaded = cache.load(key, sources);
    ASSERT_TRUE(loaded) << loaded.error().technicalDetail;
    EXPECT_EQ(loaded.value(), results);
}

TEST(DerivedAlignmentCacheTests, RejectsTheSameKeyAfterASourceFingerprintChanges) {
    ScopedCacheDirectory directory;
    const domain::ValidatedComparisonSet sources = makeSet();
    const std::vector<application::SequenceAlignmentResult> results = makeResults();
    const std::string key = application::makeDerivedAlignmentCacheKey(sources, results);
    const DerivedAlignmentCache cache{directory.path()};

    ASSERT_TRUE(cache.store(key, sources, results, 1U));
    auto loaded = cache.load(key, makeSet('c'));
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error().code, domain::MediaErrorCode::kInvalidProjectSchema);
}

TEST(DerivedAlignmentCacheTests, RejectsAKeyThatDoesNotCoverThePayload) {
    ScopedCacheDirectory directory;
    const domain::ValidatedComparisonSet sources = makeSet();
    std::vector<application::SequenceAlignmentResult> results = makeResults();
    const std::string key = application::makeDerivedAlignmentCacheKey(sources, results);
    results.front().entries.front().confidence = 0.1F;
    const DerivedAlignmentCache cache{directory.path()};

    auto status = cache.store(key, sources, results, 1U);
    ASSERT_FALSE(status);
    EXPECT_EQ(status.error().code, domain::MediaErrorCode::kInvalidProjectSchema);
}

} // namespace
} // namespace dvs::persistence
