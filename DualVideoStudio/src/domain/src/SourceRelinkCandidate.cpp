#include "dvs/domain/SourceRelinkCandidate.h"

#include <string>
#include <utility>

namespace dvs::domain {
namespace {

[[nodiscard]] MediaError invalidCandidate(const SourceRole sourceRole,
                                          std::string technicalDetail) {
    return makeMediaError(MediaErrorCode::kInvalidArgument,
                          MediaOperation::kProjectMutation,
                          sourceRole,
                          false,
                          std::move(technicalDetail));
}

} // namespace

SourceRelinkCandidate::SourceRelinkCandidate(const SourceRole sourceRole,
                                             std::filesystem::path normalizedPath,
                                             SourceFileIdentity sourceIdentity)
    : sourceRole_(sourceRole), normalizedPath_(std::move(normalizedPath)),
      sourceIdentity_(std::move(sourceIdentity)) {}

Result<SourceRelinkCandidate> SourceRelinkCandidate::create(const SourceRole sourceRole,
                                                            std::filesystem::path normalizedPath,
                                                            SourceFileIdentity sourceIdentity) {
    if (sourceRole != SourceRole::kA && sourceRole != SourceRole::kB) {
        return Result<SourceRelinkCandidate>::failure(
            invalidCandidate(sourceRole, "A relink candidate must target source A or source B."));
    }

    normalizedPath = normalizedPath.lexically_normal();
    if (!normalizedPath.is_absolute()) {
        return Result<SourceRelinkCandidate>::failure(
            invalidCandidate(sourceRole, "A relink candidate requires an absolute source path."));
    }
    if (!sourceIdentity.isComplete()) {
        return Result<SourceRelinkCandidate>::failure(invalidCandidate(
            sourceRole, "A relink candidate requires a complete source file identity."));
    }

    return Result<SourceRelinkCandidate>::success(SourceRelinkCandidate{
        sourceRole,
        std::move(normalizedPath),
        std::move(sourceIdentity),
    });
}

SourceRole SourceRelinkCandidate::sourceRole() const noexcept {
    return sourceRole_;
}

const std::filesystem::path& SourceRelinkCandidate::normalizedPath() const noexcept {
    return normalizedPath_;
}

const SourceFileIdentity& SourceRelinkCandidate::sourceIdentity() const noexcept {
    return sourceIdentity_;
}

} // namespace dvs::domain
