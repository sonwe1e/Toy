#include "dvs/domain/Clip.h"

namespace dvs::domain {

bool FrameSpan::isValid() const noexcept {
    return first.isValid() && endExclusive > first.value();
}

std::int64_t FrameSpan::frameCount() const noexcept {
    return isValid() ? endExclusive - first.value() : 0;
}

Result<FrameRange> FrameRange::inclusive(const FrameId first, const FrameId last) {
    if (!first.isValid() || !last.isValid() || last < first) {
        return Result<FrameRange>::failure(
            makeMediaError(MediaErrorCode::kInvalidFrameRange,
                           MediaOperation::kClipMutation,
                           SourceRole::kClip,
                           false,
                           "Inclusive frame range must contain ordered canonical frame IDs."));
    }
    return Result<FrameRange>::success(FrameRange{first, last});
}

Result<FrameSpan> FrameRange::toHalfOpen() const {
    // FrameId reserves INT64_MAX as this end boundary, so the addition is always representable.
    return Result<FrameSpan>::success(
        FrameSpan{.first = first_, .endExclusive = last_.value() + 1});
}

bool FrameRange::isWithin(const std::int64_t frameCount) const noexcept {
    return frameCount > 0 && first_.isValid() && last_.isValid() && last_.value() < frameCount;
}

} // namespace dvs::domain
