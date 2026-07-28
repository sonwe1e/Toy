#pragma once

#include "dvs/domain/MediaDescriptor.h"
#include "dvs/domain/MediaError.h"
#include "dvs/domain/Result.h"

#include <filesystem>

namespace dvs::platform {

// Computes and checks durable source identities without exposing Win32 handles. Callers choose
// the operation so errors remain attributable to their adapter boundary.
class SourceIdentityService final {
public:
    [[nodiscard]] static domain::Result<domain::SourceFileIdentity>
    fingerprint(const std::filesystem::path& sourcePath,
                domain::SourceRole sourceRole,
                domain::MediaOperation operation);

    [[nodiscard]] static domain::Status verify(const std::filesystem::path& sourcePath,
                                               const domain::SourceFileIdentity& expected,
                                               domain::SourceRole sourceRole,
                                               domain::MediaOperation operation);
};

} // namespace dvs::platform
