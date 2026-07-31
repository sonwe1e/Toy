#pragma once

#include "dvs/domain/FrameTimeline.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dvs::application {

// Pure direction policy for bounded canonical-frame prefetch. The coordinator owns cancellation
// generations and submission; this class only chooses valid targets in priority order.
class PrefetchScheduler final {
public:
    [[nodiscard]] std::vector<domain::FrameId> afterExact(domain::FrameId frame,
                                                          std::uint64_t frameCount,
                                                          std::size_t lookAhead = 3U,
                                                          std::size_t lookBehind = 1U);
    [[nodiscard]] static std::vector<domain::FrameId>
    duringForwardPlayback(domain::FrameId frame, std::uint64_t frameCount);

    void reset() noexcept;

private:
    enum class Direction {
        Forward,
        Backward,
    };

    [[nodiscard]] static std::vector<domain::FrameId> targets(domain::FrameId frame,
                                                              std::uint64_t frameCount,
                                                              Direction direction,
                                                              std::size_t lookAhead,
                                                              std::size_t lookBehind);

    std::optional<domain::FrameId> previousExact_;
    Direction direction_ = Direction::Forward;
};

} // namespace dvs::application
