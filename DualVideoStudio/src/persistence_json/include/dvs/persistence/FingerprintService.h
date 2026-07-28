#pragma once

#include "dvs/domain/MediaDescriptor.h"
#include "dvs/domain/Result.h"

#include <filesystem>

namespace dvs::persistence {

// Schema-1 compatibility facade. The platform service owns source I/O and hashing; this facade
// fixes errors to the project-persistence operation.
class FingerprintService final {
public:
    [[nodiscard]] static domain::Result<domain::SourceFileIdentity>
    fingerprint(const std::filesystem::path& sourcePath, domain::SourceRole sourceRole);

    [[nodiscard]] static domain::Status verify(const std::filesystem::path& sourcePath,
                                               const domain::SourceFileIdentity& expected,
                                               domain::SourceRole sourceRole);
};

} // namespace dvs::persistence
