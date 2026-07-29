#include "dvs/domain/SourceRelinkCandidate.h"

#include <string>
#include <utility>

namespace dvs::domain {
namespace {

[[nodiscard]] MediaError invalidCandidate(const SourceId sourceId, std::string technicalDetail) {
    return makeMediaError(MediaErrorCode::kInvalidArgument,
                          MediaOperation::kProjectMutation,
                          sourceId,
                          false,
                          std::move(technicalDetail));
}

} // namespace

SourceRelinkCandidate::SourceRelinkCandidate(const SourceId sourceId,
                                             std::filesystem::path normalizedPath,
                                             SourceFileIdentity sourceIdentity)
    : sourceId_(sourceId), normalizedPath_(std::move(normalizedPath)),
      sourceIdentity_(std::move(sourceIdentity)) {}

Result<SourceRelinkCandidate> SourceRelinkCandidate::create(const SourceId sourceId,
                                                            std::filesystem::path normalizedPath,
                                                            SourceFileIdentity sourceIdentity) {
    normalizedPath = normalizedPath.lexically_normal();
    if (!normalizedPath.is_absolute()) {
        return Result<SourceRelinkCandidate>::failure(
            invalidCandidate(sourceId, "A relink candidate requires an absolute source path."));
    }
    if (!sourceIdentity.isComplete()) {
        return Result<SourceRelinkCandidate>::failure(invalidCandidate(
            sourceId, "A relink candidate requires a complete source file identity."));
    }

    return Result<SourceRelinkCandidate>::success(SourceRelinkCandidate{
        sourceId,
        std::move(normalizedPath),
        std::move(sourceIdentity),
    });
}

SourceId SourceRelinkCandidate::sourceId() const noexcept {
    return sourceId_;
}

const std::filesystem::path& SourceRelinkCandidate::normalizedPath() const noexcept {
    return normalizedPath_;
}

const SourceFileIdentity& SourceRelinkCandidate::sourceIdentity() const noexcept {
    return sourceIdentity_;
}

} // namespace dvs::domain
