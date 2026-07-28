#include "dvs/persistence/FingerprintService.h"

#include "dvs/platform/SourceIdentityService.h"

namespace dvs::persistence {

domain::Result<domain::SourceFileIdentity>
FingerprintService::fingerprint(const std::filesystem::path& sourcePath,
                                const domain::SourceRole sourceRole) {
    return platform::SourceIdentityService::fingerprint(
        sourcePath, sourceRole, domain::MediaOperation::kProjectPersistence);
}

domain::Status FingerprintService::verify(const std::filesystem::path& sourcePath,
                                          const domain::SourceFileIdentity& expected,
                                          const domain::SourceRole sourceRole) {
    return platform::SourceIdentityService::verify(
        sourcePath, expected, sourceRole, domain::MediaOperation::kProjectPersistence);
}

} // namespace dvs::persistence
