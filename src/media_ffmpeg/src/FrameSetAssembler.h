#pragma once

#include "dvs/application/FrameSet.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace dvs::media::internal {

// Collects one terminal outcome per configured source and releases a FrameSet only when every
// slot is complete. Results are restored to session source order regardless of worker completion
// order.
class FrameSetAssembler final {
public:
    FrameSetAssembler(domain::FrameId canonicalFrameId,
                      domain::MediaTime canonicalTime,
                      std::vector<domain::SourceId> sourceOrder);

    [[nodiscard]] bool complete(application::MappedSourceFrame entry);
    [[nodiscard]] std::size_t completedSlotCount() const noexcept;
    [[nodiscard]] std::optional<application::FrameSet> finish();

private:
    domain::FrameId canonicalFrameId_;
    domain::MediaTime canonicalTime_;
    std::vector<domain::SourceId> sourceOrder_;
    std::vector<std::optional<application::MappedSourceFrame>> slots_;
};

} // namespace dvs::media::internal
