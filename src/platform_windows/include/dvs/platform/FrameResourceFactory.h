#pragma once

#include "dvs/platform/CpuNv12FrameResource.h"
#include "dvs/platform/CpuP010FrameResource.h"

#include <cstdint>
#include <optional>
#include <span>

namespace dvs::platform {

class WritableCpuNv12Frame final {
public:
    WritableCpuNv12Frame(const WritableCpuNv12Frame&) = delete;
    WritableCpuNv12Frame& operator=(const WritableCpuNv12Frame&) = delete;
    WritableCpuNv12Frame(WritableCpuNv12Frame&&) noexcept = default;
    WritableCpuNv12Frame& operator=(WritableCpuNv12Frame&&) noexcept = default;

    [[nodiscard]] std::span<std::uint8_t> yPlane() noexcept;
    [[nodiscard]] std::span<std::uint8_t> uvPlane() noexcept;
    [[nodiscard]] std::optional<application::FrameHandle> seal() && noexcept;

private:
    friend class FrameResourceFactory;

    WritableCpuNv12Frame(Nv12FrameLayout layout,
                         domain::ColorMetadata colorMetadata,
                         application::FramePresentation presentation,
                         Nv12PlaneByteCounts byteCounts,
                         FrameBudget::Reservation reservation,
                         Nv12BufferPool::Buffer storage) noexcept;

    Nv12FrameLayout layout_;
    domain::ColorMetadata colorMetadata_;
    application::FramePresentation presentation_;
    Nv12PlaneByteCounts byteCounts_;
    FrameBudget::Reservation reservation_;
    Nv12BufferPool::Buffer storage_;
};

class WritableCpuP010Frame final {
public:
    WritableCpuP010Frame(const WritableCpuP010Frame&) = delete;
    WritableCpuP010Frame& operator=(const WritableCpuP010Frame&) = delete;
    WritableCpuP010Frame(WritableCpuP010Frame&&) noexcept = default;
    WritableCpuP010Frame& operator=(WritableCpuP010Frame&&) noexcept = default;

    [[nodiscard]] std::span<std::uint8_t> yPlane() noexcept;
    [[nodiscard]] std::span<std::uint8_t> uvPlane() noexcept;
    [[nodiscard]] std::optional<application::FrameHandle> seal() && noexcept;

private:
    friend class FrameResourceFactory;

    WritableCpuP010Frame(P010FrameLayout layout,
                         domain::ColorMetadata colorMetadata,
                         application::FramePresentation presentation,
                         P010PlaneByteCounts byteCounts,
                         FrameBudget::Reservation reservation,
                         Nv12BufferPool::Buffer storage) noexcept;

    P010FrameLayout layout_;
    domain::ColorMetadata colorMetadata_;
    application::FramePresentation presentation_;
    P010PlaneByteCounts byteCounts_;
    FrameBudget::Reservation reservation_;
    Nv12BufferPool::Buffer storage_;
};

// Copies decoder-owned NV12 planes and normalized color metadata into an immutable platform
// resource. Validation, budget reservation, and all resource allocations finish before the source
// bytes are read. Any failure returns nullopt without leaking a reservation.
class FrameResourceFactory final {
public:
    explicit FrameResourceFactory(
        FrameBudget& frameBudget,
        application::FramePresentation presentation = application::FramePresentation{}) noexcept;

    [[nodiscard]] std::optional<WritableCpuNv12Frame>
    acquireWritableCpuNv12(Nv12FrameLayout layout,
                           domain::ColorMetadata colorMetadata,
                           Nv12BufferPool& bufferPool) const noexcept;

    [[nodiscard]] std::optional<WritableCpuP010Frame>
    acquireWritableCpuP010(P010FrameLayout layout,
                           domain::ColorMetadata colorMetadata,
                           Nv12BufferPool& bufferPool) const noexcept;

    [[nodiscard]] std::optional<application::FrameHandle>
    createCpuNv12(Nv12FrameLayout layout,
                  domain::ColorMetadata colorMetadata,
                  std::span<const std::uint8_t> sourceY,
                  std::span<const std::uint8_t> sourceUv) const noexcept;

    [[nodiscard]] std::optional<application::FrameHandle>
    createCpuP010(P010FrameLayout layout,
                  domain::ColorMetadata colorMetadata,
                  std::span<const std::uint8_t> sourceY,
                  std::span<const std::uint8_t> sourceUv) const noexcept;

private:
    FrameBudget& frameBudget_;
    application::FramePresentation presentation_;
};

} // namespace dvs::platform
