#include "dvs/domain/MediaDescriptor.h"

#include <cctype>
#include <utility>

namespace dvs::domain {

bool SourceFileIdentity::isComplete() const noexcept {
    if (byteSize == 0 || fingerprintSha256.size() != 64) {
        return false;
    }

    for (const unsigned char character : fingerprintSha256) {
        if (std::isxdigit(character) == 0) {
            return false;
        }
    }
    return true;
}

bool FrameCountInfo::isValid() const noexcept {
    return value > 0;
}

bool MediaDescriptor::isValid() const noexcept {
    return !normalizedPath.empty() && extent.isValid() && frameCount.isValid() &&
           duration.microseconds() >= 0 && !codecId.empty() && !pixelFormatId.empty() &&
           bitDepth != 0 &&
           (rotationDegrees == 0U || rotationDegrees == 90U || rotationDegrees == 180U ||
            rotationDegrees == 270U) &&
           sampleAspectRatio.isValid() && colorMetadata.isValid();
}

Result<MediaDescriptor> validateMediaDescriptor(MediaDescriptor descriptor) {
    if (descriptor.normalizedPath.empty()) {
        return Result<MediaDescriptor>::failure(
            makeMediaError(MediaErrorCode::kInvalidMediaDescriptor,
                           MediaOperation::kMediaDescriptorValidation,
                           std::nullopt,
                           false,
                           "Media descriptor path is empty."));
    }
    if (!descriptor.extent.isValid()) {
        return Result<MediaDescriptor>::failure(
            makeMediaError(MediaErrorCode::kInvalidDimensions,
                           MediaOperation::kMediaDescriptorValidation,
                           std::nullopt,
                           false,
                           "Media descriptor dimensions must be non-zero."));
    }
    if (!descriptor.frameCount.isValid()) {
        return Result<MediaDescriptor>::failure(
            makeMediaError(MediaErrorCode::kInvalidFrameCount,
                           MediaOperation::kMediaDescriptorValidation,
                           std::nullopt,
                           false,
                           "Media descriptor frame count must be positive."));
    }
    if (descriptor.duration.microseconds() < 0) {
        return Result<MediaDescriptor>::failure(
            makeMediaError(MediaErrorCode::kInvalidDuration,
                           MediaOperation::kMediaDescriptorValidation,
                           std::nullopt,
                           false,
                           "Media descriptor duration cannot be negative."));
    }
    if (descriptor.codecId.empty() || descriptor.pixelFormatId.empty() ||
        descriptor.bitDepth == 0) {
        return Result<MediaDescriptor>::failure(
            makeMediaError(MediaErrorCode::kInvalidMediaDescriptor,
                           MediaOperation::kMediaDescriptorValidation,
                           std::nullopt,
                           false,
                           "Media descriptor codec, pixel format, and bit depth are required."));
    }
    if (!descriptor.colorMetadata.isValid()) {
        return Result<MediaDescriptor>::failure(
            makeMediaError(MediaErrorCode::kInvalidMediaDescriptor,
                           MediaOperation::kMediaDescriptorValidation,
                           std::nullopt,
                           false,
                           "Media descriptor color metadata is invalid."));
    }
    if ((descriptor.rotationDegrees != 0U && descriptor.rotationDegrees != 90U &&
         descriptor.rotationDegrees != 180U && descriptor.rotationDegrees != 270U) ||
        !descriptor.sampleAspectRatio.isValid()) {
        return Result<MediaDescriptor>::failure(makeMediaError(
            MediaErrorCode::kInvalidMediaDescriptor,
            MediaOperation::kMediaDescriptorValidation,
            std::nullopt,
            false,
            "Media rotation must be right-angled and sample aspect ratio positive."));
    }

    // Constant-frame-rate sources must declare a rational rate; variable-frame-rate
    // sources carry no nominal rate and must have an indexed frame count. The media
    // adapter fills `timingConfidence`, so the domain has the final word on whether
    // that declaration is internally consistent before any pair is built.
    if (descriptor.timingConfidence == TimingConfidence::kVariableFrameRate) {
        if (descriptor.frameRate.has_value()) {
            return Result<MediaDescriptor>::failure(makeMediaError(
                MediaErrorCode::kInvalidMediaDescriptor,
                MediaOperation::kMediaDescriptorValidation,
                std::nullopt,
                false,
                "A variable-frame-rate source must not declare a nominal frame rate."));
        }
        if (descriptor.frameCount.origin != FrameCountOrigin::kIndexed) {
            return Result<MediaDescriptor>::failure(
                makeMediaError(MediaErrorCode::kInvalidMediaDescriptor,
                               MediaOperation::kMediaDescriptorValidation,
                               std::nullopt,
                               false,
                               "A variable-frame-rate source must have an indexed frame count."));
        }
    } else if (!descriptor.frameRate.has_value()) {
        return Result<MediaDescriptor>::failure(
            makeMediaError(MediaErrorCode::kInvalidMediaDescriptor,
                           MediaOperation::kMediaDescriptorValidation,
                           std::nullopt,
                           false,
                           "A constant-frame-rate source must declare a nominal frame rate."));
    }

    return Result<MediaDescriptor>::success(std::move(descriptor));
}

} // namespace dvs::domain
