#include "dvs/persistence/ProjectRelinkService.h"

#include "dvs/persistence/FingerprintService.h"
#include "dvs/platform/WindowsPaths.h"

#include <filesystem>
#include <string>
#include <utility>

namespace dvs::persistence {
namespace {

[[nodiscard]] domain::MediaError relinkError(const domain::MediaErrorCode code,
                                             const domain::SourceRole sourceRole,
                                             std::string technicalDetail) {
    return domain::makeMediaError(code,
                                  domain::MediaOperation::kProjectPersistence,
                                  sourceRole,
                                  false,
                                  std::move(technicalDetail));
}

[[nodiscard]] domain::Result<std::filesystem::path>
normalizeSourcePath(const std::filesystem::path& sourcePath, const domain::SourceRole sourceRole) {
    const auto normalizedPath = platform::WindowsPaths::absolutePath(sourcePath);
    if (!normalizedPath) {
        return domain::Result<std::filesystem::path>::failure(
            relinkError(domain::MediaErrorCode::kProjectFileIo,
                        sourceRole,
                        "Could not normalize the selected source path: " +
                            normalizedPath.error().technicalDetail));
    }
    return domain::Result<std::filesystem::path>::success(normalizedPath.value());
}

} // namespace

domain::Result<domain::SourceRelinkCandidate>
ProjectRelinkService::prepare(const domain::SourceRole sourceRole,
                              const std::filesystem::path& newSourcePath) {
    if (sourceRole != domain::SourceRole::kA && sourceRole != domain::SourceRole::kB) {
        return domain::Result<domain::SourceRelinkCandidate>::failure(
            relinkError(domain::MediaErrorCode::kInvalidArgument,
                        sourceRole,
                        "Only source A or source B can be relinked."));
    }

    const auto normalizedPath = normalizeSourcePath(newSourcePath, sourceRole);
    if (!normalizedPath) {
        return domain::Result<domain::SourceRelinkCandidate>::failure(normalizedPath.error());
    }

    const auto identity = FingerprintService::fingerprint(normalizedPath.value(), sourceRole);
    if (!identity) {
        return domain::Result<domain::SourceRelinkCandidate>::failure(identity.error());
    }

    const auto candidate =
        domain::SourceRelinkCandidate::create(sourceRole, normalizedPath.value(), identity.value());
    if (!candidate) {
        return domain::Result<domain::SourceRelinkCandidate>::failure(candidate.error());
    }
    return candidate;
}

} // namespace dvs::persistence
