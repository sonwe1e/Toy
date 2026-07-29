#pragma once

#include "dvs/domain/ComparisonSource.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dvs::domain {

// An immutable set of 2-3 validated sources sharing one canonical timeline definition. The
// canonical source defines frame positions through its rational rate (when CFR) and its
// display-order frame count. When a reference role is assigned it is always the canonical
// source; otherwise the first source in load order is canonical.
class ValidatedComparisonSet final {
public:
    [[nodiscard]] std::span<const ComparisonSource> sources() const noexcept;
    [[nodiscard]] std::size_t sourceCount() const noexcept;
    [[nodiscard]] const ComparisonSource* find(SourceId id) const noexcept;
    [[nodiscard]] SourceId canonicalSourceId() const noexcept;
    [[nodiscard]] std::optional<SourceId> referenceSourceId() const noexcept;
    [[nodiscard]] const MediaDescriptor& canonicalDescriptor() const noexcept;
    [[nodiscard]] const std::optional<RationalRate>& canonicalRate() const noexcept;
    [[nodiscard]] std::int64_t canonicalFrameCount() const noexcept;
    [[nodiscard]] bool hasEstimatedFrameCount() const noexcept;

private:
    friend class ComparisonValidator;

    ValidatedComparisonSet(std::vector<ComparisonSource> sources,
                           SourceId canonicalSourceId,
                           std::optional<SourceId> referenceSourceId);

    std::vector<ComparisonSource> sources_;
    SourceId canonicalSourceId_ = 0;
    std::optional<SourceId> referenceSourceId_;
};

} // namespace dvs::domain
