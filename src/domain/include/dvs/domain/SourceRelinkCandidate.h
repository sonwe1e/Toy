#pragma once

#include "dvs/domain/MediaDescriptor.h"

#include <filesystem>

namespace dvs::domain {

// A filesystem-only result of an explicit relink selection. It deliberately carries no media
// metadata: a fresh probe must produce a descriptor before the comparison set can be rebuilt and
// the replacement committed to the editable aggregate.
class SourceRelinkCandidate final {
public:
    [[nodiscard]] static Result<SourceRelinkCandidate> create(SourceId sourceId,
                                                              std::filesystem::path normalizedPath,
                                                              SourceFileIdentity sourceIdentity);

    [[nodiscard]] SourceId sourceId() const noexcept;
    [[nodiscard]] const std::filesystem::path& normalizedPath() const noexcept;
    [[nodiscard]] const SourceFileIdentity& sourceIdentity() const noexcept;

private:
    SourceRelinkCandidate(SourceId sourceId,
                          std::filesystem::path normalizedPath,
                          SourceFileIdentity sourceIdentity);

    SourceId sourceId_;
    std::filesystem::path normalizedPath_;
    SourceFileIdentity sourceIdentity_;
};

} // namespace dvs::domain
