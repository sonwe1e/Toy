#pragma once

#include "dvs/domain/Identifiers.h"

#include <optional>
#include <string>
#include <string_view>

namespace dvs::domain {

enum class SessionState {
    kEmpty,
    kLoading,
    kReady,
    kInvalid,
    kError,
};

enum class PlaybackState {
    kPaused,
    kPlaying,
    kSeeking,
    kBuffering,
};

enum class MediaErrorCode {
    kInvalidArgument,
    kInvalidRate,
    kInvalidFrameId,
    kInvalidFrameCount,
    kInvalidDimensions,
    kInvalidDuration,
    kInvalidMediaDescriptor,
    kArithmeticOverflow,
    kSourceFrameRateMismatch,
    kSourceFrameCountMismatch,
    kSourceDurationMismatch,
    kSourceResolutionMismatch,
    kSourceColorMetadataMismatch,
    kFrameOutOfRange,
    kUnsupportedProjectSchema,
    kInvalidProjectSchema,
    kSourceMissing,
    kSourceFingerprintMismatch,
    kProjectFileIo,
    kMediaOpenFailed,
    kMediaProbeFailed,
    kInvalidCfrTiming,
    kUnsupportedCodec,
    kUnsupportedPixelFormat,
    kMediaDecodeFailed,
    kFrameTimelineInvalid,
    kFrameBudgetExceeded,
    kGraphicsUnavailable,
    kGraphicsDeviceLost,
    kFramePresentationTimedOut,
};

enum class MediaOperation {
    kRationalConversion,
    kMediaDescriptorValidation,
    kSourcePairValidation,
    kProjectMutation,
    kProjectPersistence,
    kMediaProbe,
    kMediaDecode,
    kGraphicsInitialization,
    kFramePresentation,
};

enum class SourceRole {
    kNone,
    kA,
    kB,
    kPair,
    kProject,
};

[[nodiscard]] std::string_view stableId(SessionState state) noexcept;
[[nodiscard]] std::string_view stableId(PlaybackState state) noexcept;
[[nodiscard]] std::string_view stableId(MediaErrorCode code) noexcept;
[[nodiscard]] std::string_view stableId(MediaOperation operation) noexcept;
[[nodiscard]] std::string_view stableId(SourceRole sourceRole) noexcept;

// The UI maps userMessageKey to localized presentation text. technicalDetail stays diagnostic and
// must never be used as a user-visible label.
struct MediaError final {
    MediaErrorCode code = MediaErrorCode::kInvalidArgument;
    MediaOperation operation = MediaOperation::kProjectMutation;
    SourceRole sourceRole = SourceRole::kNone;
    std::optional<RequestId> requestId;
    bool recoverable = false;
    std::string userMessageKey;
    std::string technicalDetail;
};

[[nodiscard]] MediaError makeMediaError(MediaErrorCode code,
                                        MediaOperation operation,
                                        SourceRole sourceRole,
                                        bool recoverable,
                                        std::string technicalDetail = {},
                                        std::optional<RequestId> requestId = std::nullopt);

} // namespace dvs::domain
