#pragma once

#include "dvs/domain/ComparisonSource.h"

#include <cstdint>

namespace dvs::application {

struct SessionSnapshot;

enum class ComparisonExactness : std::uint8_t {
    ExactCodeValue,
    DisplaySpaceConverted,
    SpatiallyResampled,
    TemporallyAligned,
    Unavailable,
};

[[nodiscard]] ComparisonExactness comparisonExactness(const SessionSnapshot& snapshot,
                                                      domain::SourceId first,
                                                      domain::SourceId second) noexcept;

} // namespace dvs::application
