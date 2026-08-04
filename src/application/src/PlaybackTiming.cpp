#include "PlaybackTiming.h"

#include <cstdint>

namespace dvs::application::detail {

std::optional<std::int64_t> checkedAdd(const std::int64_t left, const std::int64_t right) noexcept {
    if (right > 0 && left > INT64_MAX - right) {
        return std::nullopt;
    }
    if (right < 0 && left < INT64_MIN - right) {
        return std::nullopt;
    }
    return left + right;
}

std::optional<std::int64_t> checkedSubtract(const std::int64_t left,
                                            const std::int64_t right) noexcept {
    if (right > 0 && left < INT64_MIN + right) {
        return std::nullopt;
    }
    if (right < 0 && left > INT64_MAX + right) {
        return std::nullopt;
    }
    return left - right;
}

std::optional<std::chrono::steady_clock::time_point>
addDuration(const std::chrono::steady_clock::time_point timePoint,
            const std::chrono::microseconds duration) noexcept {
    const auto ticks = std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration);
    if (std::chrono::duration_cast<std::chrono::microseconds>(ticks) != duration) {
        return std::nullopt;
    }
    const std::chrono::steady_clock::duration epoch = timePoint.time_since_epoch();
    if (ticks.count() > 0 && epoch > std::chrono::steady_clock::duration::max() - ticks) {
        return std::nullopt;
    }
    if (ticks.count() < 0 && epoch < std::chrono::steady_clock::duration::min() - ticks) {
        return std::nullopt;
    }
    return timePoint + ticks;
}

} // namespace dvs::application::detail
