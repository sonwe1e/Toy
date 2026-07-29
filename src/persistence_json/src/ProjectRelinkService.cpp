#include "dvs/persistence/ProjectRelinkService.h"

#include "dvs/persistence/FingerprintService.h"
#include "dvs/platform/WindowsPaths.h"

#include <filesystem>
#include <string>
#include <utility>

namespace dvs::persistence {
namespace {

[[nodiscard]] domain::MediaError relinkError(const domain::MediaErrorCode code,
                                             std::optional<domain::SourceId> sourceId,
                                             std::string technicalDetail) {
    return domain::makeMediaError(code,
                                  domain::MediaOperation::kProjectPersistence,
                                  sourceId,
                                  false,
                                  std::move(technicalDetail));
}

[[nodiscard]] domain::Result<std::filesystem::path>
normalizeSourcePath(const std::filesystem::path& sourcePath, domain::SourceId sourceId) {
    const auto normalizedPath = platform::WindowsPaths::absolutePath(sourcePath);
    if (!normalizedPath) {
        return domain::Result<std::filesystem::path>::failure(
            relinkError(domain::MediaErrorCode::kProjectFileIo,
                        sourceId,
                        "Could not normalize the selected source path: " +
                            normalizedPath.error().technicalDetail));
    }
    return domain::Result<std::filesystem::path>::success(normalizedPath.value());
}

} // namespace

domain::Result<domain::SourceRelinkCandidate>
ProjectRelinkService::prepare(const domain::SourceId sourceId,
                              const std::filesystem::path& newSourcePath) {
    const auto normalizedPath = normalizeSourcePath(newSourcePath, sourceId);
    if (!normalizedPath) {
        return domain::Result<domain::SourceRelinkCandidate>::failure(normalizedPath.error());
    }

    const auto identity = FingerprintService::fingerprint(normalizedPath.value(), sourceId);
    if (!identity) {
        return domain::Result<domain::SourceRelinkCandidate>::failure(identity.error());
    }

    const auto candidate =
        domain::SourceRelinkCandidate::create(sourceId, normalizedPath.value(), identity.value());
    if (!candidate) {
        return domain::Result<domain::SourceRelinkCandidate>::failure(candidate.error());
    }
    return candidate;
}

} // namespace dvs::persistence
