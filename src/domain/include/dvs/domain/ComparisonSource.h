#pragma once

#include "dvs/domain/Identifiers.h"
#include "dvs/domain/MediaDescriptor.h"

#include <string>

namespace dvs::domain {

// Role is user-assigned meaning, not position. A session may run with zero references (the first
// input becomes the canonical timeline) or exactly one; two references are rejected.
enum class ComparisonRole {
    kReference,
    kPrediction,
};

struct ComparisonSource final {
    SourceId id = 0;
    ComparisonRole role = ComparisonRole::kPrediction;
    MediaDescriptor descriptor;
    std::string displayName;
};

} // namespace dvs::domain
