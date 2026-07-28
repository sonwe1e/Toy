#include "GpuFramePair.h"

#include "GpuFrameResource.h"

#include <limits>
#include <utility>

namespace dvs::platform {

std::shared_ptr<const GpuFramePair>
GpuFramePair::create(std::shared_ptr<const GpuFrameResource> frameA,
                     std::shared_ptr<const GpuFrameResource> frameB) noexcept {
    if (!frameA || !frameB || frameA == frameB || frameA->sourceRole() != domain::SourceRole::kA ||
        frameB->sourceRole() != domain::SourceRole::kB || frameA->context() != frameB->context() ||
        frameA->frameId() != frameB->frameId() ||
        frameA->deviceGeneration() != frameB->deviceGeneration() ||
        frameA->accountedBytes() >
            std::numeric_limits<std::size_t>::max() - frameB->accountedBytes()) {
        return {};
    }

    const std::size_t accountedBytes = frameA->accountedBytes() + frameB->accountedBytes();
    try {
        return std::shared_ptr<const GpuFramePair>{
            new GpuFramePair(std::move(frameA), std::move(frameB), accountedBytes)};
    } catch (...) {
        return {};
    }
}

GpuFramePair::GpuFramePair(std::shared_ptr<const GpuFrameResource> frameA,
                           std::shared_ptr<const GpuFrameResource> frameB,
                           const std::size_t accountedBytes) noexcept
    : frameA_(std::move(frameA)), frameB_(std::move(frameB)), accountedBytes_(accountedBytes) {}

const application::FrameRequestContext& GpuFramePair::context() const noexcept {
    return frameA_->context();
}

const domain::FrameId& GpuFramePair::frameId() const noexcept {
    return frameA_->frameId();
}

const std::shared_ptr<const GpuFrameResource>& GpuFramePair::frameA() const noexcept {
    return frameA_;
}

const std::shared_ptr<const GpuFrameResource>& GpuFramePair::frameB() const noexcept {
    return frameB_;
}

std::size_t GpuFramePair::accountedBytes() const noexcept {
    return accountedBytes_;
}

} // namespace dvs::platform
