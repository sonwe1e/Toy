#include "dvs/domain/FrameTimeline.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace dvs::domain {
namespace {

[[nodiscard]] MediaError timelineError(const MediaErrorCode code, std::string detail) {
    return makeMediaError(code,
                          MediaOperation::kMediaDescriptorValidation,
                          std::nullopt,
                          false,
                          std::move(detail));
}

[[nodiscard]] MediaError missingTimelineError() {
    return timelineError(MediaErrorCode::kFrameTimelineInvalid,
                         "The variable-frame-rate timeline is unavailable.");
}

} // namespace

FrameTimeline::FrameTimeline(std::vector<MediaTime> displayTimes)
    : displayTimes_(std::move(displayTimes)) {}

Result<FrameTimeline> FrameTimeline::create(std::vector<MediaTime> displayTimes) {
    if (displayTimes.empty() ||
        displayTimes.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max())) {
        return Result<FrameTimeline>::failure(
            timelineError(MediaErrorCode::kFrameTimelineInvalid,
                          "A frame timeline must contain a representable number of frames."));
    }
    if (displayTimes.front().microseconds() != 0) {
        return Result<FrameTimeline>::failure(
            timelineError(MediaErrorCode::kFrameTimelineInvalid,
                          "A frame timeline must be normalized to frame zero."));
    }
    for (std::size_t index = 1U; index < displayTimes.size(); ++index) {
        if (displayTimes[index].microseconds() <= displayTimes[index - 1U].microseconds()) {
            return Result<FrameTimeline>::failure(
                timelineError(MediaErrorCode::kFrameTimelineInvalid,
                              "Frame display times must be strictly increasing."));
        }
    }
    return Result<FrameTimeline>::success(FrameTimeline{std::move(displayTimes)});
}

std::int64_t FrameTimeline::frameCount() const noexcept {
    return static_cast<std::int64_t>(displayTimes_.size());
}

Result<MediaTime> FrameTimeline::frameStartTime(const FrameId frameId) const {
    if (!frameId.isValid() || frameId.value() >= frameCount()) {
        return Result<MediaTime>::failure(timelineError(
            MediaErrorCode::kInvalidFrameId, "Frame ID is outside the variable-rate timeline."));
    }
    return Result<MediaTime>::success(displayTimes_[static_cast<std::size_t>(frameId.value())]);
}

Result<FrameId> FrameTimeline::frameAtOrBefore(const MediaTime time) const {
    if (time.microseconds() < 0) {
        return Result<FrameId>::failure(
            timelineError(MediaErrorCode::kInvalidArgument,
                          "A canonical frame cannot be derived from negative time."));
    }

    const auto after = std::upper_bound(
        displayTimes_.begin(),
        displayTimes_.end(),
        time,
        [](const MediaTime value, const MediaTime candidate) { return value < candidate; });
    if (after == displayTimes_.begin()) {
        return Result<FrameId>::success(FrameId{0});
    }
    const auto index = static_cast<std::int64_t>(std::distance(displayTimes_.begin(), after) - 1);
    return Result<FrameId>::success(FrameId{index});
}

Result<MediaTime> canonicalFrameStartTime(const CanonicalTimeline& timeline,
                                          const FrameId frameId) {
    if (const auto* const rate = std::get_if<RationalRate>(&timeline); rate != nullptr) {
        return rate->frameStartTime(frameId);
    }
    const auto& variable = std::get<std::shared_ptr<const FrameTimeline>>(timeline);
    if (!variable) {
        return Result<MediaTime>::failure(missingTimelineError());
    }
    return variable->frameStartTime(frameId);
}

Result<FrameId> canonicalFrameAtOrBefore(const CanonicalTimeline& timeline, const MediaTime time) {
    if (const auto* const rate = std::get_if<RationalRate>(&timeline); rate != nullptr) {
        return rate->frameAtOrBefore(time);
    }
    const auto& variable = std::get<std::shared_ptr<const FrameTimeline>>(timeline);
    if (!variable) {
        return Result<FrameId>::failure(missingTimelineError());
    }
    return variable->frameAtOrBefore(time);
}

bool isVariableFrameRate(const CanonicalTimeline& timeline) noexcept {
    return std::holds_alternative<std::shared_ptr<const FrameTimeline>>(timeline);
}

} // namespace dvs::domain
