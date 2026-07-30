#include "dvs/application/PrefetchScheduler.h"

#include <algorithm>
#include <cstdint>

namespace dvs::application {

std::vector<domain::FrameId> PrefetchScheduler::afterExact(const domain::FrameId frame,
                                                           const std::uint64_t frameCount,
                                                           const std::size_t lookAhead,
                                                           const std::size_t lookBehind) {
    if (previousExact_.has_value() && frame.value() != previousExact_->value()) {
        direction_ =
            frame.value() < previousExact_->value() ? Direction::Backward : Direction::Forward;
    }
    previousExact_ = frame;
    return targets(frame, frameCount, direction_, lookAhead, lookBehind);
}

std::vector<domain::FrameId>
PrefetchScheduler::duringForwardPlayback(const domain::FrameId frame,
                                         const std::uint64_t frameCount) {
    return targets(frame, frameCount, Direction::Forward, 3U, 0U);
}

void PrefetchScheduler::reset() noexcept {
    previousExact_.reset();
    direction_ = Direction::Forward;
}

std::vector<domain::FrameId> PrefetchScheduler::targets(const domain::FrameId frame,
                                                        const std::uint64_t frameCount,
                                                        const Direction direction,
                                                        const std::size_t lookAhead,
                                                        const std::size_t lookBehind) {
    std::vector<domain::FrameId> result;
    if (!frame.isValid() || frameCount == 0U || lookAhead == 0U) {
        return result;
    }

    const std::size_t boundedLookAhead = std::clamp<std::size_t>(lookAhead, 1U, 12U);
    const std::size_t boundedLookBehind = std::min<std::size_t>(lookBehind, 3U);
    result.reserve(boundedLookAhead + boundedLookBehind);
    const std::int64_t sign = direction == Direction::Forward ? 1 : -1;
    const auto appendOffset = [&](const std::int64_t offset) {
        const bool underflow = offset < 0 && frame.value() < -offset;
        const std::int64_t target = underflow ? -1 : frame.value() + offset;
        if (target >= 0 && static_cast<std::uint64_t>(target) < frameCount) {
            result.push_back(domain::FrameId{target});
        }
    };
    for (std::size_t index = 1U; index <= boundedLookAhead; ++index) {
        appendOffset(sign * static_cast<std::int64_t>(index));
    }
    for (std::size_t index = 1U; index <= boundedLookBehind; ++index) {
        appendOffset(-sign * static_cast<std::int64_t>(index));
    }
    return result;
}

} // namespace dvs::application
