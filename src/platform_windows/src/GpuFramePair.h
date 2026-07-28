#pragma once

#include "dvs/application/RequestContext.h"
#include "dvs/domain/Identifiers.h"

#include <cstddef>
#include <memory>

namespace dvs::platform {

class GpuFrameResource;

// Construction is all-or-nothing: a GPU pair always owns both source resources under one exact
// request context and one canonical frame identity.
class GpuFramePair final {
public:
    [[nodiscard]] static std::shared_ptr<const GpuFramePair>
    create(std::shared_ptr<const GpuFrameResource> frameA,
           std::shared_ptr<const GpuFrameResource> frameB) noexcept;

    [[nodiscard]] const application::FrameRequestContext& context() const noexcept;
    [[nodiscard]] const domain::FrameId& frameId() const noexcept;
    [[nodiscard]] const std::shared_ptr<const GpuFrameResource>& frameA() const noexcept;
    [[nodiscard]] const std::shared_ptr<const GpuFrameResource>& frameB() const noexcept;
    [[nodiscard]] std::size_t accountedBytes() const noexcept;

private:
    GpuFramePair(std::shared_ptr<const GpuFrameResource> frameA,
                 std::shared_ptr<const GpuFrameResource> frameB,
                 std::size_t accountedBytes) noexcept;

    std::shared_ptr<const GpuFrameResource> frameA_;
    std::shared_ptr<const GpuFrameResource> frameB_;
    std::size_t accountedBytes_ = 0U;
};

} // namespace dvs::platform
