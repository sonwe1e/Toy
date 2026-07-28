#pragma once

#include "GpuFrameResource.h"

#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>

namespace dvs::platform {

struct D3d11PlaneDimensions final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;

    [[nodiscard]] constexpr bool operator==(const D3d11PlaneDimensions&) const = default;
};

// Immutable shader-visible plane resources. Upload staging resources stay actor-local and are
// released after the event fences complete; only the default textures and SRVs reach rendering.
class D3d11GpuFrameBacking final : public IGpuFrameBacking {
public:
    D3d11GpuFrameBacking(Microsoft::WRL::ComPtr<ID3D11Texture2D> yTexture,
                         Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> yView,
                         D3d11PlaneDimensions yDimensions,
                         Microsoft::WRL::ComPtr<ID3D11Texture2D> uvTexture,
                         Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> uvView,
                         D3d11PlaneDimensions uvDimensions) noexcept;
    ~D3d11GpuFrameBacking() override = default;

    [[nodiscard]] ID3D11Texture2D* yTexture() const noexcept;
    [[nodiscard]] ID3D11ShaderResourceView* yView() const noexcept;
    [[nodiscard]] const D3d11PlaneDimensions& yDimensions() const noexcept;
    [[nodiscard]] ID3D11Texture2D* uvTexture() const noexcept;
    [[nodiscard]] ID3D11ShaderResourceView* uvView() const noexcept;
    [[nodiscard]] const D3d11PlaneDimensions& uvDimensions() const noexcept;

private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> yTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> yView_;
    D3d11PlaneDimensions yDimensions_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> uvTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> uvView_;
    D3d11PlaneDimensions uvDimensions_;
};

} // namespace dvs::platform
