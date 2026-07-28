#include "dvs/platform/CpuNv12FrameResource.h"

#include <limits>
#include <utility>

namespace dvs::platform {
namespace {

[[nodiscard]] std::optional<std::size_t> checkedProduct(const std::size_t left,
                                                        const std::size_t right) noexcept {
    if (left == 0 || right == 0) {
        return std::size_t{0};
    }
    if (left > std::numeric_limits<std::size_t>::max() / right) {
        return std::nullopt;
    }
    return left * right;
}

[[nodiscard]] std::optional<std::size_t> checkedSum(const std::size_t left,
                                                    const std::size_t right) noexcept {
    if (left > std::numeric_limits<std::size_t>::max() - right) {
        return std::nullopt;
    }
    return left + right;
}

[[nodiscard]] std::optional<std::uint32_t> minimumUvStride(const std::uint32_t width) noexcept {
    if ((width % 2U) == 0U) {
        return width;
    }
    if (width == std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return width + 1U;
}

} // namespace

bool Nv12FrameLayout::isValid() const noexcept {
    return byteCounts().has_value();
}

std::optional<Nv12PlaneByteCounts> Nv12FrameLayout::byteCounts() const noexcept {
    if (width == 0 || height == 0 || yStride < width) {
        return std::nullopt;
    }

    const std::optional<std::uint32_t> minimumUv = minimumUvStride(width);
    if (!minimumUv || uvStride < *minimumUv) {
        return std::nullopt;
    }

    const std::size_t chromaRows = (static_cast<std::size_t>(height) + 1U) / 2U;
    const std::optional<std::size_t> yBytes = checkedProduct(yStride, height);
    const std::optional<std::size_t> uvBytes = checkedProduct(uvStride, chromaRows);
    if (!yBytes || !uvBytes || !checkedSum(*yBytes, *uvBytes)) {
        return std::nullopt;
    }

    return Nv12PlaneByteCounts{.y = *yBytes, .uv = *uvBytes};
}

CpuNv12FrameResource::CpuNv12FrameResource(const Nv12FrameLayout layout,
                                           const domain::ColorMetadata colorMetadata,
                                           const std::size_t yPlaneBytes,
                                           FrameBudget::Reservation reservation,
                                           std::vector<std::uint8_t> storage) noexcept
    : layout_(layout), colorMetadata_(colorMetadata), yPlaneBytes_(yPlaneBytes),
      reservation_(std::move(reservation)), storage_(std::move(storage)) {}

const Nv12FrameLayout& CpuNv12FrameResource::layout() const noexcept {
    return layout_;
}

std::span<const std::uint8_t> CpuNv12FrameResource::yPlane() const noexcept {
    return {storage_.data(), yPlaneBytes_};
}

std::span<const std::uint8_t> CpuNv12FrameResource::uvPlane() const noexcept {
    return {storage_.data() + yPlaneBytes_, storage_.size() - yPlaneBytes_};
}

std::size_t CpuNv12FrameResource::byteCount() const noexcept {
    return storage_.size();
}

const domain::ColorMetadata& CpuNv12FrameResource::colorMetadata() const noexcept {
    return colorMetadata_;
}

} // namespace dvs::platform
