#include "dvs/domain/MediaDescriptor.h"
#include "dvs/domain/Result.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <utility>

namespace dvs::domain {
namespace {

[[nodiscard]] RationalRate makeRate() {
    auto result = RationalRate::create(30, 1);
    EXPECT_TRUE(result.hasValue());
    return std::move(result).value();
}

[[nodiscard]] MediaDescriptor validDescriptor() {
    return MediaDescriptor{
        .normalizedPath = std::filesystem::path{"source.mp4"},
        .extent = MediaExtent{.width = 1'920, .height = 1'080},
        .frameRate = makeRate(),
        .frameCount = FrameCountInfo{.value = 90, .origin = FrameCountOrigin::kReported},
        .duration = MediaTime{3'000'000},
        .codecId = "h264",
        .pixelFormatId = "yuv420p",
        .bitDepth = 8,
        .decodeCapabilities = DecodeCapabilities{.softwareDecode = true, .d3d11VaDecode = false},
        .timingConfidence = TimingConfidence::kDeclaredCfr,
        .sourceIdentity = std::nullopt,
    };
}

} // namespace

TEST(MediaDescriptorTests, ValidatesEveryRequiredNormalizedField) {
    const auto valid = validateMediaDescriptor(validDescriptor());
    ASSERT_TRUE(valid.hasValue());
    EXPECT_TRUE(valid.value().isValid());

    auto emptyPath = validDescriptor();
    emptyPath.normalizedPath.clear();
    EXPECT_EQ(validateMediaDescriptor(std::move(emptyPath)).error().code,
              MediaErrorCode::kInvalidMediaDescriptor);

    auto invalidExtent = validDescriptor();
    invalidExtent.extent.width = 0;
    EXPECT_EQ(validateMediaDescriptor(std::move(invalidExtent)).error().code,
              MediaErrorCode::kInvalidDimensions);

    auto invalidCount = validDescriptor();
    invalidCount.frameCount.value = 0;
    EXPECT_EQ(validateMediaDescriptor(std::move(invalidCount)).error().code,
              MediaErrorCode::kInvalidFrameCount);

    auto invalidDuration = validDescriptor();
    invalidDuration.duration = MediaTime{-1};
    EXPECT_EQ(validateMediaDescriptor(std::move(invalidDuration)).error().code,
              MediaErrorCode::kInvalidDuration);

    auto invalidFormat = validDescriptor();
    invalidFormat.pixelFormatId.clear();
    EXPECT_EQ(validateMediaDescriptor(std::move(invalidFormat)).error().code,
              MediaErrorCode::kInvalidMediaDescriptor);
}

TEST(MediaDescriptorTests, RecognizesCompleteSourceFileIdentities) {
    SourceFileIdentity identity{
        .byteSize = 1,
        .modifiedUtcMilliseconds = 0,
        .fingerprintSha256 = std::string(64, 'a'),
    };
    EXPECT_TRUE(identity.isComplete());

    identity.fingerprintSha256 = "not-a-sha256";
    EXPECT_FALSE(identity.isComplete());
    identity.fingerprintSha256 = std::string(64, 'g');
    EXPECT_FALSE(identity.isComplete());
    identity.byteSize = 0;
    EXPECT_FALSE(identity.isComplete());
}

TEST(MediaDescriptorTests, AcceptsVariableFrameRateWithoutANominalRate) {
    auto descriptor = validDescriptor();
    descriptor.frameRate = std::nullopt;
    descriptor.frameCount.origin = FrameCountOrigin::kIndexed;
    descriptor.timingConfidence = TimingConfidence::kVariableFrameRate;

    const auto result = validateMediaDescriptor(std::move(descriptor));
    ASSERT_TRUE(result);
    EXPECT_FALSE(result.value().frameRate.has_value());
}

TEST(MediaErrorTests, ExposesStableEnglishIdentifiersForPersistedStatesAndErrors) {
    EXPECT_EQ(stableId(SessionState::kEmpty), "empty");
    EXPECT_EQ(stableId(SessionState::kLoading), "loading");
    EXPECT_EQ(stableId(SessionState::kReady), "ready");
    EXPECT_EQ(stableId(SessionState::kInvalid), "invalid");
    EXPECT_EQ(stableId(SessionState::kError), "error");
    EXPECT_EQ(stableId(PlaybackState::kPaused), "paused");
    EXPECT_EQ(stableId(PlaybackState::kPlaying), "playing");
    EXPECT_EQ(stableId(PlaybackState::kSeeking), "seeking");
    EXPECT_EQ(stableId(PlaybackState::kBuffering), "buffering");
    EXPECT_EQ(stableId(ProxyJobState::kPending), "pending");
    EXPECT_EQ(stableId(ProxyJobState::kRunning), "running");
    EXPECT_EQ(stableId(ProxyJobState::kSucceeded), "succeeded");
    EXPECT_EQ(stableId(ProxyJobState::kFailed), "failed");
    EXPECT_EQ(stableId(ProxyJobState::kCanceled), "canceled");
    EXPECT_EQ(stableId(ExportJobState::kPending), "pending");
    EXPECT_EQ(stableId(ExportJobState::kRunning), "running");
    EXPECT_EQ(stableId(ExportJobState::kSucceeded), "succeeded");
    EXPECT_EQ(stableId(ExportJobState::kFailed), "failed");
    EXPECT_EQ(stableId(ExportJobState::kCanceled), "canceled");
    EXPECT_EQ(stableId(ExportJobState::kInterrupted), "interrupted");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidArgument), "invalid-argument");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidRate), "invalid-rate");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidFrameId), "invalid-frame-id");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidFrameRange), "invalid-frame-range");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidFrameCount), "invalid-frame-count");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidDimensions), "invalid-dimensions");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidDuration), "invalid-duration");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidMediaDescriptor), "invalid-media-descriptor");
    EXPECT_EQ(stableId(MediaErrorCode::kArithmeticOverflow), "arithmetic-overflow");
    EXPECT_EQ(stableId(MediaErrorCode::kSourceFrameRateMismatch), "source-frame-rate-mismatch");
    EXPECT_EQ(stableId(MediaErrorCode::kSourceFrameCountMismatch), "source-frame-count-mismatch");
    EXPECT_EQ(stableId(MediaErrorCode::kSourceDurationMismatch), "source-duration-mismatch");
    EXPECT_EQ(stableId(MediaErrorCode::kDuplicateIdentifier), "duplicate-identifier");
    EXPECT_EQ(stableId(MediaErrorCode::kMarksIncomplete), "marks-incomplete");
    EXPECT_EQ(stableId(MediaErrorCode::kMarksReversed), "marks-reversed");
    EXPECT_EQ(stableId(MediaErrorCode::kClipOutOfRange), "clip-out-of-range");
    EXPECT_EQ(stableId(MediaErrorCode::kClipNotFound), "clip-not-found");
    EXPECT_EQ(stableId(MediaErrorCode::kExportRecordNotFound), "export-record-not-found");
    EXPECT_EQ(stableId(MediaErrorCode::kDuplicateClipSelection), "duplicate-clip-selection");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidExportMode), "invalid-export-mode");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidExportGeometry), "invalid-export-geometry");
    EXPECT_EQ(stableId(MediaErrorCode::kUnsupportedProjectSchema), "unsupported-project-schema");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidProjectSchema), "invalid-project-schema");
    EXPECT_EQ(stableId(MediaErrorCode::kSourceMissing), "source-missing");
    EXPECT_EQ(stableId(MediaErrorCode::kSourceFingerprintMismatch), "source-fingerprint-mismatch");
    EXPECT_EQ(stableId(MediaErrorCode::kProjectFileIo), "project-file-io");
    EXPECT_EQ(stableId(MediaErrorCode::kMediaOpenFailed), "media-open-failed");
    EXPECT_EQ(stableId(MediaErrorCode::kMediaProbeFailed), "media-probe-failed");
    EXPECT_EQ(stableId(MediaErrorCode::kInvalidCfrTiming), "invalid-cfr-timing");
    EXPECT_EQ(stableId(MediaErrorCode::kUnsupportedCodec), "unsupported-codec");
    EXPECT_EQ(stableId(MediaErrorCode::kUnsupportedPixelFormat), "unsupported-pixel-format");
    EXPECT_EQ(stableId(MediaErrorCode::kMediaDecodeFailed), "media-decode-failed");
    EXPECT_EQ(stableId(MediaErrorCode::kFrameTimelineInvalid), "frame-timeline-invalid");
    EXPECT_EQ(stableId(MediaErrorCode::kFrameBudgetExceeded), "frame-budget-exceeded");
    EXPECT_EQ(stableId(MediaErrorCode::kGraphicsUnavailable), "graphics-unavailable");
    EXPECT_EQ(stableId(MediaErrorCode::kGraphicsDeviceLost), "graphics-device-lost");
    EXPECT_EQ(stableId(MediaErrorCode::kFramePresentationTimedOut), "frame-presentation-timed-out");
    EXPECT_EQ(stableId(MediaOperation::kRationalConversion), "rational-conversion");
    EXPECT_EQ(stableId(MediaOperation::kMediaDescriptorValidation), "media-descriptor-validation");
    EXPECT_EQ(stableId(MediaOperation::kSourcePairValidation), "source-pair-validation");
    EXPECT_EQ(stableId(MediaOperation::kProjectMutation), "project-mutation");
    EXPECT_EQ(stableId(MediaOperation::kClipMutation), "clip-mutation");
    EXPECT_EQ(stableId(MediaOperation::kExportPlanBuild), "export-plan-build");
    EXPECT_EQ(stableId(MediaOperation::kProjectPersistence), "project-persistence");
    EXPECT_EQ(stableId(MediaOperation::kMediaProbe), "media-probe");
    EXPECT_EQ(stableId(MediaOperation::kMediaDecode), "media-decode");
    EXPECT_EQ(stableId(MediaOperation::kGraphicsInitialization), "graphics-initialization");
    EXPECT_EQ(stableId(MediaOperation::kFramePresentation), "frame-presentation");
    EXPECT_EQ(stableId(SourceRole::kNone), "none");
    EXPECT_EQ(stableId(SourceRole::kA), "a");
    EXPECT_EQ(stableId(SourceRole::kB), "b");
    EXPECT_EQ(stableId(SourceRole::kPair), "pair");
    EXPECT_EQ(stableId(SourceRole::kProject), "project");
    EXPECT_EQ(stableId(SourceRole::kClip), "clip");
    EXPECT_EQ(stableId(SourceRole::kExport), "export");

    const MediaError error = makeMediaError(MediaErrorCode::kInvalidFrameCount,
                                            MediaOperation::kMediaDescriptorValidation,
                                            SourceRole::kA,
                                            true,
                                            "count was zero",
                                            RequestId{42});
    EXPECT_EQ(error.userMessageKey, "invalid-frame-count");
    EXPECT_EQ(error.requestId, RequestId{42});
    EXPECT_TRUE(error.recoverable);
}

TEST(MediaErrorTests, KeepsNativeGraphicsDiagnosticsOutOfTheUserMessageKey) {
    const MediaError error =
        makeMediaError(MediaErrorCode::kGraphicsUnavailable,
                       MediaOperation::kGraphicsInitialization,
                       SourceRole::kNone,
                       true,
                       "D3D11 device creation failed with HRESULT 0x887A0005.");
    EXPECT_EQ(error.userMessageKey, "graphics-unavailable");
    EXPECT_EQ(error.userMessageKey.find("0x"), std::string::npos);
    EXPECT_EQ(error.technicalDetail, "D3D11 device creation failed with HRESULT 0x887A0005.");
}

TEST(MediaErrorTests, ResultCanCarryMediaErrorAsEitherValueOrFailure) {
    const MediaError value = makeMediaError(MediaErrorCode::kInvalidArgument,
                                            MediaOperation::kProjectPersistence,
                                            SourceRole::kProject,
                                            false,
                                            "decoded persisted error");
    const auto success = Result<MediaError>::success(value);
    ASSERT_TRUE(success);
    EXPECT_EQ(success.value().technicalDetail, "decoded persisted error");

    const auto failure =
        Result<MediaError>::failure(makeMediaError(MediaErrorCode::kInvalidProjectSchema,
                                                   MediaOperation::kProjectPersistence,
                                                   SourceRole::kProject,
                                                   false,
                                                   "schema rejected"));
    ASSERT_FALSE(failure);
    EXPECT_EQ(failure.error().code, MediaErrorCode::kInvalidProjectSchema);
}

} // namespace dvs::domain
