#pragma once

#include "dvs/domain/MediaDescriptor.h"

#include <filesystem>

namespace dvs::domain {

// A filesystem-only result of an explicit relink selection. It deliberately carries no media
// metadata: a fresh probe must produce a descriptor before Project::replaceSources can commit
// the replacement to the editable aggregate.
class SourceRelinkCandidate final {
public:
    [[nodiscard]] static Result<SourceRelinkCandidate> create(SourceRole sourceRole,
                                                              std::filesystem::path normalizedPath,
                                                              SourceFileIdentity sourceIdentity);

    [[nodiscard]] SourceRole sourceRole() const noexcept;
    [[nodiscard]] const std::filesystem::path& normalizedPath() const noexcept;
    [[nodiscard]] const SourceFileIdentity& sourceIdentity() const noexcept;

private:
    SourceRelinkCandidate(SourceRole sourceRole,
                          std::filesystem::path normalizedPath,
                          SourceFileIdentity sourceIdentity);

    SourceRole sourceRole_;
    std::filesystem::path normalizedPath_;
    SourceFileIdentity sourceIdentity_;
};

} // namespace dvs::domain
