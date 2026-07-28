#include "dvs/platform/FrameResourceFactory.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

namespace dvs::platform {

FrameResourceFactory::FrameResourceFactory(FrameBudget& frameBudget) noexcept
    : frameBudget_(frameBudget) {}

std::optional<application::FrameHandle>
FrameResourceFactory::createCpuNv12(const Nv12FrameLayout layout,
                                    const domain::ColorMetadata colorMetadata,
                                    const std::span<const std::uint8_t> sourceY,
                                    const std::span<const std::uint8_t> sourceUv) const noexcept {
    const std::optional<Nv12PlaneByteCounts> byteCounts = layout.byteCounts();
    if (!byteCounts || !colorMetadata.isValid() || sourceY.size() < byteCounts->y ||
        sourceUv.size() < byteCounts->uv) {
        return std::nullopt;
    }

    const std::size_t storageBytes = byteCounts->total();
    std::optional<FrameBudget::Reservation> reservation = frameBudget_.tryReserve(storageBytes);
    if (!reservation) {
        return std::nullopt;
    }

    try {
        std::vector<std::uint8_t> storage(storageBytes);
        // Finish every fallible allocation before reading decoder-owned bytes. The private
        // constructor and friend access keep the storage unobservable until both planes have been
        // copied and the immutable handle is returned.
        auto resource = std::shared_ptr<CpuNv12FrameResource>(new CpuNv12FrameResource(
            layout, colorMetadata, byteCounts->y, std::move(*reservation), std::move(storage)));
        const auto yEnd = resource->storage_.begin() + static_cast<std::ptrdiff_t>(byteCounts->y);
        std::copy_n(sourceY.begin(), byteCounts->y, resource->storage_.begin());
        std::copy_n(sourceUv.begin(), byteCounts->uv, yEnd);

        return application::FrameHandle::create(
            std::move(resource),
            application::FrameGeometry{.width = layout.width, .height = layout.height},
            storageBytes);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace dvs::platform
