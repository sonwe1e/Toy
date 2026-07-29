#pragma once

#include "dvs/domain/RationalRate.h"

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace dvs::domain {

// Runtime-only display-order timing. Values are normalized to frame zero and expressed in
// microseconds so application timing never depends on an FFmpeg stream time base.
class FrameTimeline final {
public:
    [[nodiscard]] static Result<FrameTimeline> create(std::vector<MediaTime> displayTimes);

    [[nodiscard]] std::int64_t frameCount() const noexcept;
    [[nodiscard]] Result<MediaTime> frameStartTime(FrameId frameId) const;
    [[nodiscard]] Result<FrameId> frameAtOrBefore(MediaTime time) const;

private:
    explicit FrameTimeline(std::vector<MediaTime> displayTimes);

    std::vector<MediaTime> displayTimes_;
};

// CFR sessions retain the O(1) rational path. VFR sessions share one immutable Source A timeline
// between the coordinator and frame provider without persisting derived per-frame data.
using CanonicalTimeline = std::variant<RationalRate, std::shared_ptr<const FrameTimeline>>;

[[nodiscard]] Result<MediaTime> canonicalFrameStartTime(const CanonicalTimeline& timeline,
                                                        FrameId frameId);
[[nodiscard]] Result<FrameId> canonicalFrameAtOrBefore(const CanonicalTimeline& timeline,
                                                       MediaTime time);
[[nodiscard]] bool isVariableFrameRate(const CanonicalTimeline& timeline) noexcept;

} // namespace dvs::domain
