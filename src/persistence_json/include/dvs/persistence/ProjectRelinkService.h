#pragma once

#include "dvs/domain/ComparisonSource.h"
#include "dvs/domain/Result.h"
#include "dvs/domain/SourceRelinkCandidate.h"

#include <filesystem>

namespace dvs::persistence {

// Produces a filesystem-validated relink candidate after a user explicitly chooses a file. This
// worker never rebuilds a Project from stale descriptors: a fresh media probe must validate the
// candidate before the coordinator calls Project::replaceSources.
class ProjectRelinkService final {
public:
    [[nodiscard]] static domain::Result<domain::SourceRelinkCandidate>
    prepare(domain::SourceId sourceId, const std::filesystem::path& newSourcePath);
};

} // namespace dvs::persistence
