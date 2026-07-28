#pragma once

#include "dvs/domain/CompatibilityReport.h"
#include "dvs/domain/ValidatedComparisonSet.h"

#include <vector>

namespace dvs::domain {

struct ComparisonValidation final {
    ValidatedComparisonSet set;
    CompatibilityReport report;
};

// Validates one session's worth of sources. A source is admissible only when its descriptor is
// individually valid; such per-source fatals fail the whole Result. Cross-source differences
// never block opening: they surface as CompatibilityReport findings that the UI and the
// alignment service present explicitly, so videos that differ by one or two frames still enter
// the player with their mismatches named.
class ComparisonValidator final {
public:
    [[nodiscard]] static Result<ComparisonValidation>
    validate(std::vector<ComparisonSource> sources);
};

} // namespace dvs::domain
