#pragma once

#include "dvs/domain/ComparisonSource.h"
#include "dvs/domain/MediaDescriptor.h"
#include "dvs/domain/Result.h"

#include <filesystem>

namespace dvs::persistence {

// Schema-2 compatibility facade. The platform service owns source I/O and hashing; this facade
// fixes errors to the project-persistence operation.
class FingerprintService final {
public:
    [[nodiscard]] static domain::Result<domain::SourceFileIdentity>
    fingerprint(const std::filesystem::path& sourcePath, domain::SourceId sourceId);

    [[nodiscard]] static domain::Status verify(const std::filesystem::path& sourcePath,
                                               const domain::SourceFileIdentity& expected,
                                               domain::SourceId sourceId);
};

} // namespace dvs::persistence
