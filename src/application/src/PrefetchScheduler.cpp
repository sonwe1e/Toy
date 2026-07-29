#include "dvs/application/PrefetchScheduler.h"

#include <array>
#include <cstdint>

namespace dvs::application {

std::vector<domain::FrameId> PrefetchScheduler::afterExact(const domain::FrameId frame,
                                                           const std::uint64_t frameCount) {
    if (previousExact_.has_value() && frame.value() != previousExact_->value()) {
        direction_ =
            frame.value() < previousExact_->value() ? Direction::Backward : Direction::Forward;
    }
    previousExact_ = frame;
    return targets(frame, frameCount, direction_, true);
}

std::vector<domain::FrameId>
PrefetchScheduler::duringForwardPlayback(const domain::FrameId frame,
                                         const std::uint64_t frameCount) {
    return targets(frame, frameCount, Direction::Forward, false);
}

void PrefetchScheduler::reset() noexcept {
    previousExact_.reset();
    direction_ = Direction::Forward;
}

std::vector<domain::FrameId> PrefetchScheduler::targets(const domain::FrameId frame,
                                                        const std::uint64_t frameCount,
                                                        const Direction direction,
                                                        const bool includeOpposite) {
    std::vector<domain::FrameId> result;
    if (!frame.isValid() || frameCount == 0U) {
        return result;
    }

    const std::array<std::int64_t, 4U> forward{1, 2, 3, -1};
    const std::array<std::int64_t, 4U> backward{-1, -2, -3, 1};
    const auto& offsets = direction == Direction::Forward ? forward : backward;
    const std::size_t count = includeOpposite ? offsets.size() : offsets.size() - 1U;
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const std::int64_t offset = offsets[index];
        const bool underflow = offset < 0 && frame.value() < -offset;
        const std::int64_t target = underflow ? -1 : frame.value() + offset;
        if (target >= 0 && static_cast<std::uint64_t>(target) < frameCount) {
            result.push_back(domain::FrameId{target});
        }
    }
    return result;
}

} // namespace dvs::application
