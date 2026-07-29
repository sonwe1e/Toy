#pragma once

#include "dvs/application/FrameHandle.h"
#include "dvs/domain/MediaDescriptor.h"
#include "dvs/platform/CpuNv12FrameResource.h"
#include "dvs/platform/FrameBudget.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace dvs::platform {

class FrameResourceFactory;
class WritableCpuP010Frame;

struct P010PlaneByteCounts final {
    std::size_t y = 0U;
    std::size_t uv = 0U;

    [[nodiscard]] constexpr std::size_t total() const noexcept {
        return y + uv;
    }
};

// P010 stores each 10-bit sample in the most significant bits of a 16-bit little-endian word.
// Strides are byte strides and therefore must be even.
struct P010FrameLayout final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t yStride = 0U;
    std::uint32_t uvStride = 0U;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] std::optional<P010PlaneByteCounts> byteCounts() const noexcept;
};

class CpuP010FrameResource final : public application::IFrameResource {
public:
    [[nodiscard]] const P010FrameLayout& layout() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> yPlane() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> uvPlane() const noexcept;
    [[nodiscard]] std::size_t byteCount() const noexcept;
    [[nodiscard]] const domain::ColorMetadata& colorMetadata() const noexcept;

private:
    friend class FrameResourceFactory;
    friend class WritableCpuP010Frame;

    CpuP010FrameResource(P010FrameLayout layout,
                         domain::ColorMetadata colorMetadata,
                         std::size_t yPlaneBytes,
                         FrameBudget::Reservation reservation,
                         Nv12BufferPool::Buffer storage) noexcept;

    P010FrameLayout layout_;
    domain::ColorMetadata colorMetadata_;
    std::size_t yPlaneBytes_ = 0U;
    FrameBudget::Reservation reservation_;
    Nv12BufferPool::Buffer storage_;
};

} // namespace dvs::platform
