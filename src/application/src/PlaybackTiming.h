#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace dvs::application::detail {

[[nodiscard]] std::optional<std::int64_t> checkedAdd(std::int64_t left,
                                                     std::int64_t right) noexcept;
[[nodiscard]] std::optional<std::int64_t> checkedSubtract(std::int64_t left,
                                                          std::int64_t right) noexcept;
[[nodiscard]] std::optional<std::chrono::steady_clock::time_point>
addDuration(std::chrono::steady_clock::time_point timePoint,
            std::chrono::microseconds duration) noexcept;

} // namespace dvs::application::detail
