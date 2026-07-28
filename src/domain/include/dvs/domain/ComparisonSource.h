#pragma once

#include "dvs/domain/MediaDescriptor.h"

#include <cstdint>
#include <string>

namespace dvs::domain {

// SourceId identifies one loaded input within a comparison session. Ids are assigned by the
// session builder (normally 0, 1, 2 in load order) and stay stable for the lifetime of the
// session; they are array positions, not the old positional A/B roles.
using SourceId = std::uint32_t;

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
