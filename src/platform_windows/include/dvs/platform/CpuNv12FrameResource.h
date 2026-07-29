#pragma once

#include "dvs/application/FrameHandle.h"
#include "dvs/domain/MediaDescriptor.h"
#include "dvs/platform/FrameBudget.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace dvs::platform {

class FrameResourceFactory;
class WritableCpuNv12Frame;

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

// Recycles detached NV12 byte buffers after the last immutable frame handle releases them.
// Buffers can return from any thread, so the small retained list is synchronized.
class Nv12BufferPool final {
private:
    struct State;

public:
    class Buffer final {
    public:
        Buffer() noexcept = default;
        ~Buffer();

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;
        Buffer(Buffer&& other) noexcept;
        Buffer& operator=(Buffer&& other) noexcept;

        [[nodiscard]] std::span<std::uint8_t> bytes() noexcept;
        [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;

    private:
        friend class Nv12BufferPool;

        Buffer(std::shared_ptr<State> state, std::vector<std::uint8_t> storage) noexcept;
        void release() noexcept;

        std::shared_ptr<State> state_;
        std::vector<std::uint8_t> storage_;
    };

    explicit Nv12BufferPool(std::size_t maximumRetainedBuffers = 3U);

    Nv12BufferPool(const Nv12BufferPool&) = delete;
    Nv12BufferPool& operator=(const Nv12BufferPool&) = delete;
    Nv12BufferPool(Nv12BufferPool&&) = delete;
    Nv12BufferPool& operator=(Nv12BufferPool&&) = delete;

    [[nodiscard]] std::optional<Buffer> acquire(std::size_t bytes) noexcept;

private:
    std::shared_ptr<State> state_;
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
    friend class WritableCpuNv12Frame;

    CpuNv12FrameResource(Nv12FrameLayout layout,
                         domain::ColorMetadata colorMetadata,
                         std::size_t yPlaneBytes,
                         FrameBudget::Reservation reservation,
                         Nv12BufferPool::Buffer storage) noexcept;

    Nv12FrameLayout layout_;
    domain::ColorMetadata colorMetadata_;
    std::size_t yPlaneBytes_ = 0;
    FrameBudget::Reservation reservation_;
    Nv12BufferPool::Buffer storage_;
};

} // namespace dvs::platform
