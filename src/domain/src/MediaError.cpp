#include "dvs/domain/MediaError.h"

#include <utility>

namespace dvs::domain {

std::string_view stableId(const SessionState state) noexcept {
    switch (state) {
    case SessionState::kEmpty:
        return "empty";
    case SessionState::kLoading:
        return "loading";
    case SessionState::kReady:
        return "ready";
    case SessionState::kInvalid:
        return "invalid";
    case SessionState::kError:
        return "error";
    }
    return "unknown";
}

std::string_view stableId(const PlaybackState state) noexcept {
    switch (state) {
    case PlaybackState::kPaused:
        return "paused";
    case PlaybackState::kPlaying:
        return "playing";
    case PlaybackState::kSeeking:
        return "seeking";
    case PlaybackState::kBuffering:
        return "buffering";
    }
    return "unknown";
}

std::string_view stableId(const ProxyJobState state) noexcept {
    switch (state) {
    case ProxyJobState::kPending:
        return "pending";
    case ProxyJobState::kRunning:
        return "running";
    case ProxyJobState::kSucceeded:
        return "succeeded";
    case ProxyJobState::kFailed:
        return "failed";
    case ProxyJobState::kCanceled:
        return "canceled";
    }
    return "unknown";
}

std::string_view stableId(const ExportJobState state) noexcept {
    switch (state) {
    case ExportJobState::kPending:
        return "pending";
    case ExportJobState::kRunning:
        return "running";
    case ExportJobState::kSucceeded:
        return "succeeded";
    case ExportJobState::kFailed:
        return "failed";
    case ExportJobState::kCanceled:
        return "canceled";
    case ExportJobState::kInterrupted:
        return "interrupted";
    }
    return "unknown";
}

std::string_view stableId(const MediaErrorCode code) noexcept {
    switch (code) {
    case MediaErrorCode::kInvalidArgument:
        return "invalid-argument";
    case MediaErrorCode::kInvalidRate:
        return "invalid-rate";
    case MediaErrorCode::kInvalidFrameId:
        return "invalid-frame-id";
    case MediaErrorCode::kInvalidFrameRange:
        return "invalid-frame-range";
    case MediaErrorCode::kInvalidFrameCount:
        return "invalid-frame-count";
    case MediaErrorCode::kInvalidDimensions:
        return "invalid-dimensions";
    case MediaErrorCode::kInvalidDuration:
        return "invalid-duration";
    case MediaErrorCode::kInvalidMediaDescriptor:
        return "invalid-media-descriptor";
    case MediaErrorCode::kArithmeticOverflow:
        return "arithmetic-overflow";
    case MediaErrorCode::kSourceFrameRateMismatch:
        return "source-frame-rate-mismatch";
    case MediaErrorCode::kSourceFrameCountMismatch:
        return "source-frame-count-mismatch";
    case MediaErrorCode::kSourceDurationMismatch:
        return "source-duration-mismatch";
    case MediaErrorCode::kDuplicateIdentifier:
        return "duplicate-identifier";
    case MediaErrorCode::kMarksIncomplete:
        return "marks-incomplete";
    case MediaErrorCode::kMarksReversed:
        return "marks-reversed";
    case MediaErrorCode::kClipOutOfRange:
        return "clip-out-of-range";
    case MediaErrorCode::kClipNotFound:
        return "clip-not-found";
    case MediaErrorCode::kExportRecordNotFound:
        return "export-record-not-found";
    case MediaErrorCode::kDuplicateClipSelection:
        return "duplicate-clip-selection";
    case MediaErrorCode::kInvalidExportMode:
        return "invalid-export-mode";
    case MediaErrorCode::kInvalidExportGeometry:
        return "invalid-export-geometry";
    case MediaErrorCode::kUnsupportedProjectSchema:
        return "unsupported-project-schema";
    case MediaErrorCode::kInvalidProjectSchema:
        return "invalid-project-schema";
    case MediaErrorCode::kSourceMissing:
        return "source-missing";
    case MediaErrorCode::kSourceFingerprintMismatch:
        return "source-fingerprint-mismatch";
    case MediaErrorCode::kProjectFileIo:
        return "project-file-io";
    case MediaErrorCode::kMediaOpenFailed:
        return "media-open-failed";
    case MediaErrorCode::kMediaProbeFailed:
        return "media-probe-failed";
    case MediaErrorCode::kInvalidCfrTiming:
        return "invalid-cfr-timing";
    case MediaErrorCode::kUnsupportedCodec:
        return "unsupported-codec";
    case MediaErrorCode::kUnsupportedPixelFormat:
        return "unsupported-pixel-format";
    case MediaErrorCode::kMediaDecodeFailed:
        return "media-decode-failed";
    case MediaErrorCode::kFrameTimelineInvalid:
        return "frame-timeline-invalid";
    case MediaErrorCode::kFrameBudgetExceeded:
        return "frame-budget-exceeded";
    case MediaErrorCode::kGraphicsUnavailable:
        return "graphics-unavailable";
    case MediaErrorCode::kGraphicsDeviceLost:
        return "graphics-device-lost";
    case MediaErrorCode::kFramePresentationTimedOut:
        return "frame-presentation-timed-out";
    }
    return "unknown-error";
}

std::string_view stableId(const MediaOperation operation) noexcept {
    switch (operation) {
    case MediaOperation::kRationalConversion:
        return "rational-conversion";
    case MediaOperation::kMediaDescriptorValidation:
        return "media-descriptor-validation";
    case MediaOperation::kSourcePairValidation:
        return "source-pair-validation";
    case MediaOperation::kProjectMutation:
        return "project-mutation";
    case MediaOperation::kClipMutation:
        return "clip-mutation";
    case MediaOperation::kExportPlanBuild:
        return "export-plan-build";
    case MediaOperation::kProjectPersistence:
        return "project-persistence";
    case MediaOperation::kMediaProbe:
        return "media-probe";
    case MediaOperation::kMediaDecode:
        return "media-decode";
    case MediaOperation::kGraphicsInitialization:
        return "graphics-initialization";
    case MediaOperation::kFramePresentation:
        return "frame-presentation";
    }
    return "unknown-operation";
}

std::string_view stableId(const SourceRole sourceRole) noexcept {
    switch (sourceRole) {
    case SourceRole::kNone:
        return "none";
    case SourceRole::kA:
        return "a";
    case SourceRole::kB:
        return "b";
    case SourceRole::kPair:
        return "pair";
    case SourceRole::kProject:
        return "project";
    case SourceRole::kClip:
        return "clip";
    case SourceRole::kExport:
        return "export";
    }
    return "unknown-source";
}

MediaError makeMediaError(const MediaErrorCode code,
                          const MediaOperation operation,
                          const SourceRole sourceRole,
                          const bool recoverable,
                          std::string technicalDetail,
                          std::optional<RequestId> requestId) {
    return MediaError{
        .code = code,
        .operation = operation,
        .sourceRole = sourceRole,
        .requestId = requestId,
        .recoverable = recoverable,
        .userMessageKey = std::string{stableId(code)},
        .technicalDetail = std::move(technicalDetail),
    };
}

} // namespace dvs::domain
