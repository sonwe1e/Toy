#pragma once

#include "dvs/platform/CpuNv12FrameResource.h"

#include <cstdint>
#include <optional>
#include <span>

namespace dvs::platform {

// Copies decoder-owned NV12 planes and normalized color metadata into an immutable platform
// resource. Validation, budget reservation, and all resource allocations finish before the source
// bytes are read. Any failure returns nullopt without leaking a reservation.
class FrameResourceFactory final {
public:
    explicit FrameResourceFactory(FrameBudget& frameBudget) noexcept;

    [[nodiscard]] std::optional<application::FrameHandle>
    createCpuNv12(Nv12FrameLayout layout,
                  domain::ColorMetadata colorMetadata,
                  std::span<const std::uint8_t> sourceY,
                  std::span<const std::uint8_t> sourceUv) const noexcept;

private:
    FrameBudget& frameBudget_;
};

} // namespace dvs::platform
