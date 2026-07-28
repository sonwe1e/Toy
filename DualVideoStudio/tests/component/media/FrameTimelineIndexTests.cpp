#include "FrameTimelineIndex.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <vector>

namespace dvs::media::internal {
namespace {

[[nodiscard]] std::filesystem::path fixture(const char* const name) {
    return std::filesystem::path{DVS_MEDIA_FIXTURE_DIR} / name;
}

struct CancelAfterChecks final {
    mutable std::atomic<int> checks = 0;
    int limit = 0;
};

[[nodiscard]] bool cancelAfterChecks(const void* const context) noexcept {
    const auto* const state = static_cast<const CancelAfterChecks*>(context);
    return state != nullptr &&
           state->checks.fetch_add(1, std::memory_order_acq_rel) >= state->limit;
}

TEST(FrameTimelineIndexTests, SortsDecodeOrderPacketPtsIntoDisplayOrder) {
    const auto result = validatePresentationTimestamps({0, 1'536, 512, 1'024},
                                                       false,
                                                       std::int64_t{4},
                                                       domain::SourceRole::kA,
                                                       domain::MediaOperation::kMediaDecode);

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value(), (std::vector<std::int64_t>{0, 512, 1'024, 1'536}));
}

TEST(FrameTimelineIndexTests, RejectsDuplicateMissingAndContradictoryPacketPts) {
    const auto duplicate = validatePresentationTimestamps({0, 512, 512},
                                                          false,
                                                          std::nullopt,
                                                          domain::SourceRole::kA,
                                                          domain::MediaOperation::kMediaDecode);
    ASSERT_FALSE(duplicate);
    EXPECT_EQ(duplicate.error().code, domain::MediaErrorCode::kFrameTimelineInvalid);

    const auto missing = validatePresentationTimestamps(
        {0, 512}, true, std::nullopt, domain::SourceRole::kB, domain::MediaOperation::kMediaProbe);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, domain::MediaErrorCode::kFrameTimelineInvalid);

    const auto contradictory = validatePresentationTimestamps({0, 512, 1'024},
                                                              false,
                                                              std::int64_t{4},
                                                              domain::SourceRole::kA,
                                                              domain::MediaOperation::kMediaDecode);
    ASSERT_FALSE(contradictory);
    EXPECT_EQ(contradictory.error().code, domain::MediaErrorCode::kFrameTimelineInvalid);
}

TEST(FrameTimelineIndexTests, HonorsCancellationWhileIndexIoIsInProgress) {
    CancelAfterChecks cancellation{.checks = 0, .limit = 5};
    const auto result = buildPresentationTimestampIndex(TimestampIndexRequest{
        .sourcePath = fixture("h264_no_count_64x48_30fps_12.mkv"),
        .sourceRole = domain::SourceRole::kB,
        .operation = domain::MediaOperation::kMediaProbe,
        .expectedFrameCount = std::nullopt,
        .cancellation =
            TimelineCancellation{.isRequested = cancelAfterChecks, .context = &cancellation},
    });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, domain::MediaErrorCode::kMediaProbeFailed);
    EXPECT_TRUE(result.error().recoverable);
}

TEST(FrameTimelineIndexTests, VerifiesQuantizedCadenceFromANonZeroPresentationAnchor) {
    const std::vector<std::int64_t> timestamps{900, 933, 967, 1'000, 1'033, 1'067};
    const auto result = verifyConstantFrameRate(CfrVerificationRequest{
        .presentationTimestamps = timestamps,
        .timeBase = TimelineRational{.numerator = 1, .denominator = 1'000},
        .candidates = {{.rate = {.numerator = 30, .denominator = 1}}},
        .sourceRole = domain::SourceRole::kA,
    });

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().frameRate, (TimelineRational{.numerator = 30, .denominator = 1}));
}

TEST(FrameTimelineIndexTests, VerifiesFractionalNtscCadenceAtIntegerTickBoundaries) {
    const std::vector<std::int64_t> timestamps{123, 3'126, 6'129, 9'132, 12'135};
    const auto result = verifyConstantFrameRate(CfrVerificationRequest{
        .presentationTimestamps = timestamps,
        .timeBase = TimelineRational{.numerator = 1, .denominator = 90'000},
        .candidates = {{.rate = {.numerator = 30'000, .denominator = 1'001}}},
        .sourceRole = domain::SourceRole::kB,
    });

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().frameRate,
              (TimelineRational{.numerator = 30'000, .denominator = 1'001}));
}

TEST(FrameTimelineIndexTests, PrefersAPassingGuessedRateAndNormalizesDuplicateCandidates) {
    const std::vector<std::int64_t> timestamps{0, 33, 67, 100};
    const auto result = verifyConstantFrameRate(CfrVerificationRequest{
        .presentationTimestamps = timestamps,
        .timeBase = TimelineRational{.numerator = 1, .denominator = 1'000},
        .candidates =
            {
                {.rate = {.numerator = 60, .denominator = 2}},
                {.rate = {.numerator = 30'000, .denominator = 1'001}, .guessed = true},
                {.rate = {.numerator = 30, .denominator = 1}},
                {.rate = {.numerator = 0, .denominator = 1}, .guessed = true},
            },
        .sourceRole = domain::SourceRole::kA,
    });

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().frameRate,
              (TimelineRational{.numerator = 30'000, .denominator = 1'001}));
}

TEST(FrameTimelineIndexTests, UsesTheFfmpegGuessWhenRateDeclarationsAreMissing) {
    const std::vector<std::int64_t> timestamps{0, 512, 1'024, 1'536};
    const auto result = verifyConstantFrameRate(CfrVerificationRequest{
        .presentationTimestamps = timestamps,
        .timeBase = TimelineRational{.numerator = 1, .denominator = 15'360},
        .candidates =
            {
                {.rate = {.numerator = 30, .denominator = 1}, .guessed = true},
                {.rate = {.numerator = 0, .denominator = 0}},
                {.rate = {.numerator = 0, .denominator = 1}},
            },
        .sourceRole = domain::SourceRole::kB,
    });

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().frameRate, (TimelineRational{.numerator = 30, .denominator = 1}));
}

TEST(FrameTimelineIndexTests, SelectsTheSmallestResidualWhenNoPassingCandidateIsGuessed) {
    const std::vector<std::int64_t> timestamps{0, 33};
    const auto result = verifyConstantFrameRate(CfrVerificationRequest{
        .presentationTimestamps = timestamps,
        .timeBase = TimelineRational{.numerator = 1, .denominator = 1'000},
        .candidates =
            {
                {.rate = {.numerator = 30'000, .denominator = 1'001}},
                {.rate = {.numerator = 30, .denominator = 1}},
            },
        .sourceRole = domain::SourceRole::kA,
    });

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().frameRate, (TimelineRational{.numerator = 30, .denominator = 1}));
}

TEST(FrameTimelineIndexTests, AcceptsASingleFrameWhenAnyPositiveCandidateRemains) {
    const std::vector<std::int64_t> timestamps{42};
    const auto result = verifyConstantFrameRate(CfrVerificationRequest{
        .presentationTimestamps = timestamps,
        .timeBase = TimelineRational{.numerator = 1, .denominator = 1'000},
        .candidates =
            {
                {.rate = {.numerator = 0, .denominator = 1}},
                {.rate = {.numerator = 60'000, .denominator = 2'002}},
            },
        .sourceRole = domain::SourceRole::kB,
    });

    ASSERT_TRUE(result);
    EXPECT_EQ(result.value().frameRate,
              (TimelineRational{.numerator = 30'000, .denominator = 1'001}));
}

TEST(FrameTimelineIndexTests, RejectsTrueVfrAndDoesNotGrantABroadOneTickTolerance) {
    const std::vector<std::int64_t> variableTimestamps{0, 512, 1'024, 2'048};
    const auto variable = verifyConstantFrameRate(CfrVerificationRequest{
        .presentationTimestamps = variableTimestamps,
        .timeBase = TimelineRational{.numerator = 1, .denominator = 15'360},
        .candidates =
            {
                {.rate = {.numerator = 30, .denominator = 1}, .guessed = true},
                {.rate = {.numerator = 24, .denominator = 1}},
            },
        .sourceRole = domain::SourceRole::kA,
    });
    ASSERT_FALSE(variable);
    EXPECT_EQ(variable.error().code, domain::MediaErrorCode::kInvalidCfrTiming);

    const std::vector<std::int64_t> oneTickOutside{0, 513};
    const auto outside = verifyConstantFrameRate(CfrVerificationRequest{
        .presentationTimestamps = oneTickOutside,
        .timeBase = TimelineRational{.numerator = 1, .denominator = 15'360},
        .candidates = {{.rate = {.numerator = 30, .denominator = 1}}},
        .sourceRole = domain::SourceRole::kA,
    });
    ASSERT_FALSE(outside);
    EXPECT_EQ(outside.error().code, domain::MediaErrorCode::kInvalidCfrTiming);
}

TEST(FrameTimelineIndexTests, RejectsTimestampDeltaOverflowWithoutUndefinedArithmetic) {
    const std::vector<std::int64_t> timestamps{std::numeric_limits<std::int64_t>::min(),
                                               std::numeric_limits<std::int64_t>::max()};
    const auto result = verifyConstantFrameRate(CfrVerificationRequest{
        .presentationTimestamps = timestamps,
        .timeBase = TimelineRational{.numerator = 1, .denominator = 1'000},
        .candidates = {{.rate = {.numerator = 30, .denominator = 1}}},
        .sourceRole = domain::SourceRole::kA,
    });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, domain::MediaErrorCode::kInvalidCfrTiming);
}

TEST(FrameTimelineIndexTests, HonorsCancellationDuringCadenceVerification) {
    CancelAfterChecks cancellation{.checks = 0, .limit = 2};
    const std::vector<std::int64_t> timestamps{0, 512, 1'024, 1'536, 2'048};
    const auto result = verifyConstantFrameRate(CfrVerificationRequest{
        .presentationTimestamps = timestamps,
        .timeBase = TimelineRational{.numerator = 1, .denominator = 15'360},
        .candidates = {{.rate = {.numerator = 30, .denominator = 1}}},
        .sourceRole = domain::SourceRole::kB,
        .cancellation =
            TimelineCancellation{.isRequested = cancelAfterChecks, .context = &cancellation},
    });

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, domain::MediaErrorCode::kMediaProbeFailed);
    EXPECT_TRUE(result.error().recoverable);
}

} // namespace
} // namespace dvs::media::internal
