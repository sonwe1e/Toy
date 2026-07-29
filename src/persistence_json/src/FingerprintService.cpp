#include "dvs/persistence/FingerprintService.h"

#include "dvs/platform/SourceIdentityService.h"

namespace dvs::persistence {

domain::Result<domain::SourceFileIdentity>
FingerprintService::fingerprint(const std::filesystem::path& sourcePath,
                                const domain::SourceId sourceId) {
    return platform::SourceIdentityService::fingerprint(
        sourcePath, sourceId, domain::MediaOperation::kProjectPersistence);
}

domain::Status FingerprintService::verify(const std::filesystem::path& sourcePath,
                                          const domain::SourceFileIdentity& expected,
                                          const domain::SourceId sourceId) {
    return platform::SourceIdentityService::verify(
        sourcePath, expected, sourceId, domain::MediaOperation::kProjectPersistence);
}

} // namespace dvs::persistence
