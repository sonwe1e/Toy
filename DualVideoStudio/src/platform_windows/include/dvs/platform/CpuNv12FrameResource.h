#pragma once

#include "dvs/application/FrameHandle.h"
#include "dvs/domain/MediaDescriptor.h"
#include "dvs/platform/FrameBudget.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace dvs::platform {

class FrameResourceFactory;

struct Nv12PlaneByteCounts final {
    std::size_t y = 0;
    std::size_t uv = 0;

    [[nodiscard]] constexpr std::size_t total() const noexcept {
        return y + uv;
    }
};

// The strides are byte strides. Odd image dimensions are supported; the UV stride must still
// contain one interleaved chroma pair for every ceil(width / 2) samples.
struct Nv12FrameLayout final {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t yStride = 0;
    std::uint32_t uvStride = 0;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] std::optional<Nv12PlaneByteCounts> byteCounts() const noexcept;
};

// A packed, immutable CPU copy of a single NV12 frame. Application code only receives the
// IFrameResource base through FrameHandle; adapter code may inspect immutable planes here.
class CpuNv12FrameResource final : public application::IFrameResource {
public:
    [[nodiscard]] const Nv12FrameLayout& layout() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> yPlane() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> uvPlane() const noexcept;
    [[nodiscard]] std::size_t byteCount() const noexcept;
    [[nodiscard]] const domain::ColorMetadata& colorMetadata() const noexcept;

private:
    friend class FrameResourceFactory;

    CpuNv12FrameResource(Nv12FrameLayout layout,
                         domain::ColorMetadata colorMetadata,
                         std::size_t yPlaneBytes,
                         FrameBudget::Reservation reservation,
                         std::vector<std::uint8_t> storage) noexcept;

    Nv12FrameLayout layout_;
    domain::ColorMetadata colorMetadata_;
    std::size_t yPlaneBytes_ = 0;
    FrameBudget::Reservation reservation_;
    std::vector<std::uint8_t> storage_;
};

} // namespace dvs::platform
