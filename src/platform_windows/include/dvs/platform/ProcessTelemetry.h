#pragma once

#include <cstddef>

namespace dvs::platform {

struct ProcessTelemetry final {
    std::size_t threadCount = 0U;
    std::size_t workingSetBytes = 0U;
};

[[nodiscard]] ProcessTelemetry sampleCurrentProcessTelemetry() noexcept;

} // namespace dvs::platform
