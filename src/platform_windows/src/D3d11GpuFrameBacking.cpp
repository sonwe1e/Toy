#include "D3d11GpuFrameBacking.h"

#include <utility>

namespace dvs::platform {

D3d11GpuFrameBacking::D3d11GpuFrameBacking(Microsoft::WRL::ComPtr<ID3D11Texture2D> yTexture,
                                           Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> yView,
                                           const D3d11PlaneDimensions yDimensions,
                                           Microsoft::WRL::ComPtr<ID3D11Texture2D> uvTexture,
                                           Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> uvView,
                                           const D3d11PlaneDimensions uvDimensions,
                                           std::shared_ptr<const void> lifetimeAnchor) noexcept
    : yTexture_(std::move(yTexture)), yView_(std::move(yView)), yDimensions_(yDimensions),
      uvTexture_(std::move(uvTexture)), uvView_(std::move(uvView)), uvDimensions_(uvDimensions),
      lifetimeAnchor_(std::move(lifetimeAnchor)) {}

ID3D11Texture2D* D3d11GpuFrameBacking::yTexture() const noexcept {
    return yTexture_.Get();
}

ID3D11ShaderResourceView* D3d11GpuFrameBacking::yView() const noexcept {
    return yView_.Get();
}

const D3d11PlaneDimensions& D3d11GpuFrameBacking::yDimensions() const noexcept {
    return yDimensions_;
}

ID3D11Texture2D* D3d11GpuFrameBacking::uvTexture() const noexcept {
    return uvTexture_.Get();
}

ID3D11ShaderResourceView* D3d11GpuFrameBacking::uvView() const noexcept {
    return uvView_.Get();
}

const D3d11PlaneDimensions& D3d11GpuFrameBacking::uvDimensions() const noexcept {
    return uvDimensions_;
}

} // namespace dvs::platform
