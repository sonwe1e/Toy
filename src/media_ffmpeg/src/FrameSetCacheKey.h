#pragma once

#include "dvs/domain/FrameTimeline.h"
#include "dvs/domain/Identifiers.h"

#include <cstdint>

namespace dvs::media::internal {

struct FrameSetCacheKey final {
    domain::SessionEpoch sessionEpoch;
    std::uint64_t alignmentRevision = 0U;
    domain::FrameId canonicalFrame;

    [[nodiscard]] constexpr bool operator==(const FrameSetCacheKey&) const noexcept = default;
};

} // namespace dvs::media::internal
