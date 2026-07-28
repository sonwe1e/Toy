#include "dvs/domain/SourcePairValidator.h"

#include <utility>

namespace dvs::domain {

ValidatedSourcePair::ValidatedSourcePair(MediaDescriptor sourceA, MediaDescriptor sourceB)
    : sourceA_(std::move(sourceA)), sourceB_(std::move(sourceB)) {}

const MediaDescriptor& ValidatedSourcePair::sourceA() const noexcept {
    return sourceA_;
}

const MediaDescriptor& ValidatedSourcePair::sourceB() const noexcept {
    return sourceB_;
}

const std::optional<RationalRate>& ValidatedSourcePair::canonicalRate() const noexcept {
    return sourceA_.frameRate;
}

std::int64_t ValidatedSourcePair::canonicalFrameCount() const noexcept {
    return sourceA_.frameCount.value;
}

bool ValidatedSourcePair::hasEstimatedFrameCount() const noexcept {
    return sourceA_.frameCount.origin == FrameCountOrigin::kEstimated ||
           sourceB_.frameCount.origin == FrameCountOrigin::kEstimated;
}

Result<ValidatedSourcePair> SourcePairValidator::validate(const MediaDescriptor& sourceA,
                                                          const MediaDescriptor& sourceB) {
    if (!sourceA.isValid()) {
        return Result<ValidatedSourcePair>::failure(
            makeMediaError(MediaErrorCode::kInvalidMediaDescriptor,
                           MediaOperation::kSourcePairValidation,
                           SourceRole::kA,
                           false,
                           "Source A descriptor is invalid."));
    }
    if (!sourceB.isValid()) {
        return Result<ValidatedSourcePair>::failure(
            makeMediaError(MediaErrorCode::kInvalidMediaDescriptor,
                           MediaOperation::kSourcePairValidation,
                           SourceRole::kB,
                           false,
                           "Source B descriptor is invalid."));
    }
    if (sourceA.frameCount.value != sourceB.frameCount.value) {
        return Result<ValidatedSourcePair>::failure(
            makeMediaError(MediaErrorCode::kSourceFrameCountMismatch,
                           MediaOperation::kSourcePairValidation,
                           SourceRole::kPair,
                           true,
                           "Effective source frame counts differ."));
    }

    return Result<ValidatedSourcePair>::success(ValidatedSourcePair{sourceA, sourceB});
}

} // namespace dvs::domain
