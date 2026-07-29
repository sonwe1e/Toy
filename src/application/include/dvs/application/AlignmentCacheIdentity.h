#pragma once

#include "dvs/application/Alignment.h"
#include "dvs/domain/ValidatedComparisonSet.h"

#include <span>
#include <string>
#include <string_view>

namespace dvs::application {

inline constexpr std::string_view kSequenceAlignmentAlgorithmVersion = "dvs-sequence-alignment-v3";

// Validates immutable derived mappings before they cross a persistence or restore boundary.
// Accepted caches may contain a subset of non-canonical sources because rejected segments keep
// their strict/global fallback instead of being silently accepted.
[[nodiscard]] bool
validateDerivedSequenceAlignments(const domain::ValidatedComparisonSet& sources,
                                  std::span<const SequenceAlignmentResult> results) noexcept;

// The key covers source identities, timeline sizes, the algorithm version, and the accepted map
// contents. It is an opaque cache locator, not a security boundary.
[[nodiscard]] std::string
makeDerivedAlignmentCacheKey(const domain::ValidatedComparisonSet& sources,
                             std::span<const SequenceAlignmentResult> results);

} // namespace dvs::application
