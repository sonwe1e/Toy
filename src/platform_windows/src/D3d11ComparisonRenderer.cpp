#include "dvs/platform/D3d11ComparisonRenderer.h"

#include "dvs/platform/FrameMailbox.h"
#include "dvs/platform/GraphicsDeviceBroker.h"
#include "dvs/platform/PresentationAckMailbox.h"
#include "dvs/platform/RenderActivitySink.h"

#include "D3d11GpuFrameBacking.h"
#include "GpuFrameResource.h"
#include "GpuFrameSet.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <d3d11.h>
#include <dvs/platform/shaders/DvsNv12Shaders.generated.h>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <wrl/client.h>

namespace dvs::platform {
namespace {

using Microsoft::WRL::ComPtr;

struct alignas(16) ComposeConstants final {
    std::array<float, 16U> clipFromItem{};
    std::array<float, 4U> destinationRect{};
    std::array<float, 4U> sourceUvRect{};
    float opacity = 1.0F;
    std::array<float, 3U> padding{};
};

struct alignas(16) ColorConstants final {
    std::array<float, 12U> yuvToRgb{};
};

struct alignas(16) DifferenceConstants final {
    std::array<float, 4U> sourceUvRectA{};
    std::array<float, 4U> sourceUvRectB{};
    std::array<float, 4U> planeDimensionsA{};
    std::array<float, 4U> planeDimensionsB{};
    std::uint32_t metric = 0U;
    float gain = 1.0F;
    std::uint32_t filter = 0U;
    float padding = 0.0F;
};

static_assert(sizeof(ComposeConstants) == 112U);
static_assert(sizeof(ColorConstants) == 48U);
static_assert(sizeof(DifferenceConstants) == 80U);

struct VideoDraw final {
    ComposeConstants compose;
    ColorConstants color;
    ID3D11ShaderResourceView* yView = nullptr;
    ID3D11ShaderResourceView* uvView = nullptr;
};

struct DifferenceDraw final {
    ComposeConstants compose;
    ColorConstants colorA;
    ColorConstants colorB;
    DifferenceConstants options;
    ID3D11ShaderResourceView* yViewA = nullptr;
    ID3D11ShaderResourceView* uvViewA = nullptr;
    ID3D11ShaderResourceView* yViewB = nullptr;
    ID3D11ShaderResourceView* uvViewB = nullptr;
    SurfaceDifferenceFilter filter = SurfaceDifferenceFilter::Bilinear;
};

struct PreparedSetDraw final {
    FrameMailboxPublication publication;
    bool difference = false;
    VideoDraw drawA;
    VideoDraw drawB;
    DifferenceDraw differenceDraw;
    std::array<ComposeConstants, 8U> letterboxBars{};
    std::size_t letterboxBarCount = 0U;
};

[[nodiscard]] bool allFinite(const std::array<float, 16U>& values) noexcept {
    return std::all_of(
        values.cbegin(), values.cend(), [](const float value) { return std::isfinite(value); });
}

[[nodiscard]] bool isValid(const SurfaceViewMode value) noexcept {
    return value == SurfaceViewMode::SideBySide || value == SurfaceViewMode::Difference;
}

[[nodiscard]] bool isValid(const SurfaceDifferenceMetric value) noexcept {
    return value == SurfaceDifferenceMetric::RgbAbsolute ||
           value == SurfaceDifferenceMetric::Luma || value == SurfaceDifferenceMetric::Chroma ||
           value == SurfaceDifferenceMetric::Heatmap;
}

[[nodiscard]] bool isValid(const SurfaceDifferenceGain value) noexcept {
    return value == SurfaceDifferenceGain::Gain1x || value == SurfaceDifferenceGain::Gain2x ||
           value == SurfaceDifferenceGain::Gain4x || value == SurfaceDifferenceGain::Gain8x ||
           value == SurfaceDifferenceGain::Gain16x;
}

[[nodiscard]] bool isValid(const SurfaceDifferenceReference value) noexcept {
    return value == SurfaceDifferenceReference::SourceA ||
           value == SurfaceDifferenceReference::SourceB;
}

[[nodiscard]] bool isValid(const SurfaceDifferenceFilter value) noexcept {
    return value == SurfaceDifferenceFilter::Nearest ||
           value == SurfaceDifferenceFilter::Bilinear || value == SurfaceDifferenceFilter::Bicubic;
}

[[nodiscard]] float differenceGain(const SurfaceDifferenceGain value) noexcept {
    switch (value) {
    case SurfaceDifferenceGain::Gain1x:
        return 1.0F;
    case SurfaceDifferenceGain::Gain2x:
        return 2.0F;
    case SurfaceDifferenceGain::Gain4x:
        return 4.0F;
    case SurfaceDifferenceGain::Gain8x:
        return 8.0F;
    case SurfaceDifferenceGain::Gain16x:
        return 16.0F;
    }
    return 1.0F;
}

[[nodiscard]] std::array<float, 4U>
textureRegionValues(const application::TextureRegion& region) noexcept {
    return {
        region.left,
        region.top,
        region.right - region.left,
        region.bottom - region.top,
    };
}

[[nodiscard]] std::array<float, 4U>
sourcePlaneDimensions(const application::FrameGeometry& geometry) noexcept {
    const std::uint32_t uvWidth = (geometry.width / 2U) + (geometry.width % 2U);
    const std::uint32_t uvHeight = (geometry.height / 2U) + (geometry.height % 2U);
    return {
        static_cast<float>(geometry.width),
        static_cast<float>(geometry.height),
        static_cast<float>(uvWidth),
        static_cast<float>(uvHeight),
    };
}

[[nodiscard]] ComposeConstants makeComposeConstants(const SurfaceRenderState& state,
                                                    const SurfaceRect& destination,
                                                    const application::TextureRegion& region) {
    return ComposeConstants{
        .clipFromItem = state.clipFromItem,
        .destinationRect = {destination.x, destination.y, destination.width, destination.height},
        .sourceUvRect =
            {
                region.left,
                region.top,
                region.right - region.left,
                region.bottom - region.top,
            },
        .opacity = std::clamp(state.opacity, 0.0F, 1.0F),
    };
}

[[nodiscard]] ComposeConstants makeBlackConstants(const SurfaceRenderState& state) {
    return makeComposeConstants(state,
                                SurfaceRect{
                                    .x = 0.0F,
                                    .y = 0.0F,
                                    .width = state.logicalWidth,
                                    .height = state.logicalHeight,
                                },
                                application::TextureRegion{});
}

[[nodiscard]] D3D11_BUFFER_DESC dynamicConstantBufferDescription(const std::size_t byteWidth) {
    return D3D11_BUFFER_DESC{
        .ByteWidth = static_cast<UINT>(byteWidth),
        .Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        .MiscFlags = 0U,
        .StructureByteStride = 0U,
    };
}

[[nodiscard]] HRESULT createDynamicConstantBuffer(ID3D11Device& device,
                                                  const std::size_t byteWidth,
                                                  ComPtr<ID3D11Buffer>& result) noexcept {
    const D3D11_BUFFER_DESC description = dynamicConstantBufferDescription(byteWidth);
    return device.CreateBuffer(&description, nullptr, result.ReleaseAndGetAddressOf());
}

template <typename Value>
[[nodiscard]] HRESULT writeConstantBuffer(ID3D11DeviceContext& context,
                                          ID3D11Buffer& buffer,
                                          const Value& value) noexcept {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result = context.Map(&buffer, 0U, D3D11_MAP_WRITE_DISCARD, 0U, &mapped);
    if (FAILED(result)) {
        return result;
    }
    std::memcpy(mapped.pData, &value, sizeof(value));
    context.Unmap(&buffer, 0U);
    return S_OK;
}

[[nodiscard]] const D3d11GpuFrameBacking*
d3dBacking(const std::shared_ptr<const GpuFrameResource>& frame) noexcept {
    if (!frame) {
        return nullptr;
    }
    return dynamic_cast<const D3d11GpuFrameBacking*>(&frame->backing());
}

[[nodiscard]] VideoDraw makeVideoDraw(const SurfaceRenderState& state,
                                      const SurfaceRect& destination,
                                      const GpuFrameResource& frame,
                                      const D3d11GpuFrameBacking& backing) {
    return VideoDraw{
        .compose = makeComposeConstants(state, destination, frame.geometry().textureRegion),
        .color = ColorConstants{.yuvToRgb = nv12ColorTransform(frame.colorMetadata()).yuvToRgb},
        .yView = backing.yView(),
        .uvView = backing.uvView(),
    };
}

[[nodiscard]] DifferenceDraw makeDifferenceDraw(const SurfaceRenderState& state,
                                                const SurfaceRect& destination,
                                                const GpuFrameResource& frameA,
                                                const D3d11GpuFrameBacking& backingA,
                                                const GpuFrameResource& frameB,
                                                const D3d11GpuFrameBacking& backingB) {
    return DifferenceDraw{
        .compose = makeComposeConstants(
            state,
            destination,
            application::TextureRegion{.left = 0.0F, .top = 0.0F, .right = 1.0F, .bottom = 1.0F}),
        .colorA = ColorConstants{.yuvToRgb = nv12ColorTransform(frameA.colorMetadata()).yuvToRgb},
        .colorB = ColorConstants{.yuvToRgb = nv12ColorTransform(frameB.colorMetadata()).yuvToRgb},
        .options =
            DifferenceConstants{
                .sourceUvRectA = textureRegionValues(frameA.geometry().textureRegion),
                .sourceUvRectB = textureRegionValues(frameB.geometry().textureRegion),
                .planeDimensionsA = sourcePlaneDimensions(frameA.geometry()),
                .planeDimensionsB = sourcePlaneDimensions(frameB.geometry()),
                .metric = static_cast<std::uint32_t>(state.differenceMetric),
                .gain = differenceGain(state.differenceGain),
                .filter = static_cast<std::uint32_t>(state.differenceFilter),
            },
        .yViewA = backingA.yView(),
        .uvViewA = backingA.uvView(),
        .yViewB = backingB.yView(),
        .uvViewB = backingB.uvView(),
        .filter = state.differenceFilter,
    };
}

void appendLetterboxBars(const SurfaceRenderState& state,
                         const SurfaceRect& bounds,
                         const SurfaceRect& content,
                         PreparedSetDraw& prepared) {
    const auto append = [&](const SurfaceRect& bar) {
        if (bar.isValid() && prepared.letterboxBarCount < prepared.letterboxBars.size()) {
            prepared.letterboxBars[prepared.letterboxBarCount] =
                makeComposeConstants(state, bar, application::TextureRegion{});
            ++prepared.letterboxBarCount;
        }
    };

    append(SurfaceRect{
        .x = bounds.x, .y = bounds.y, .width = bounds.width, .height = content.y - bounds.y});
    append(SurfaceRect{.x = bounds.x,
                       .y = content.y + content.height,
                       .width = bounds.width,
                       .height = (bounds.y + bounds.height) - (content.y + content.height)});
    append(SurfaceRect{
        .x = bounds.x, .y = content.y, .width = content.x - bounds.x, .height = content.height});
    append(SurfaceRect{.x = content.x + content.width,
                       .y = content.y,
                       .width = (bounds.x + bounds.width) - (content.x + content.width),
                       .height = content.height});
}

[[nodiscard]] bool isRemovedResult(const HRESULT result) noexcept {
    return result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DEVICE_REMOVED ||
           result == DXGI_ERROR_DEVICE_RESET || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

} // namespace

bool SurfaceRenderState::isValid() const noexcept {
    if (!allFinite(clipFromItem) || !std::isfinite(logicalWidth) || !std::isfinite(logicalHeight) ||
        !std::isfinite(opacity) || logicalWidth <= 0.0F || logicalHeight <= 0.0F ||
        pixelWidth == 0U || pixelHeight == 0U) {
        return false;
    }
    return (!scissorEnabled || scissor.isValid()) && dvs::platform::isValid(viewMode) &&
           dvs::platform::isValid(differenceMetric) && dvs::platform::isValid(differenceGain) &&
           dvs::platform::isValid(differenceReference) && dvs::platform::isValid(differenceFilter);
}

SurfaceSplitLayout computeSurfaceSplit(const float logicalWidth,
                                       const float logicalHeight,
                                       const std::uint32_t pixelWidth,
                                       const std::uint32_t pixelHeight) noexcept {
    if (!std::isfinite(logicalWidth) || !std::isfinite(logicalHeight) || logicalWidth <= 0.0F ||
        logicalHeight <= 0.0F || pixelWidth < 2U || pixelHeight == 0U) {
        return {};
    }

    const std::uint32_t leftPixels = pixelWidth / 2U;
    const std::uint32_t rightPixels = pixelWidth - leftPixels;
    const float split =
        logicalWidth * static_cast<float>(leftPixels) / static_cast<float>(pixelWidth);
    return SurfaceSplitLayout{
        .leftPixelWidth = leftPixels,
        .rightPixelWidth = rightPixels,
        .left = SurfaceRect{.x = 0.0F, .y = 0.0F, .width = split, .height = logicalHeight},
        .right =
            SurfaceRect{
                .x = split,
                .y = 0.0F,
                .width = logicalWidth - split,
                .height = logicalHeight,
            },
    };
}

SurfaceRect aspectFitRect(const SurfaceRect& bounds,
                          const std::uint32_t sourceWidth,
                          const std::uint32_t sourceHeight) noexcept {
    if (!bounds.isValid() || !std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
        !std::isfinite(bounds.width) || !std::isfinite(bounds.height) || sourceWidth == 0U ||
        sourceHeight == 0U) {
        return {};
    }

    const float sourceAspect = static_cast<float>(sourceWidth) / static_cast<float>(sourceHeight);
    const float boundsAspect = bounds.width / bounds.height;
    float fittedWidth = bounds.width;
    float fittedHeight = bounds.height;
    if (sourceAspect > boundsAspect) {
        fittedHeight = fittedWidth / sourceAspect;
    } else {
        fittedWidth = fittedHeight * sourceAspect;
    }

    return SurfaceRect{
        .x = bounds.x + ((bounds.width - fittedWidth) * 0.5F),
        .y = bounds.y + ((bounds.height - fittedHeight) * 0.5F),
        .width = fittedWidth,
        .height = fittedHeight,
    };
}

std::optional<D3dScissorRect>
d3dScissorFromBottomLeft(const SurfaceScissorRect& scissor,
                         const SurfaceViewport& viewport,
                         const std::uint32_t renderTargetHeight) noexcept {
    if (!scissor.isValid() || !std::isfinite(viewport.topLeftX) ||
        !std::isfinite(viewport.topLeftY) || !std::isfinite(viewport.width) ||
        !std::isfinite(viewport.height) || viewport.width <= 0.0F || viewport.height <= 0.0F ||
        renderTargetHeight == 0U) {
        return std::nullopt;
    }

    const double viewportLeft = std::floor(static_cast<double>(viewport.topLeftX));
    const double viewportTop = std::floor(static_cast<double>(viewport.topLeftY));
    const double viewportRight = std::ceil(static_cast<double>(viewport.topLeftX) + viewport.width);
    const double viewportBottom =
        std::ceil(static_cast<double>(viewport.topLeftY) + viewport.height);
    if (viewportLeft >= viewportRight || viewportTop >= viewportBottom) {
        return std::nullopt;
    }

    const double left = std::clamp(static_cast<double>(scissor.left), viewportLeft, viewportRight);
    const double right =
        std::clamp(static_cast<double>(scissor.right), viewportLeft, viewportRight);
    const double flippedTop = std::clamp(
        static_cast<double>(renderTargetHeight) - scissor.top, viewportTop, viewportBottom);
    const double flippedBottom = std::clamp(
        static_cast<double>(renderTargetHeight) - scissor.bottom, viewportTop, viewportBottom);
    const auto toLong = [](const double value) {
        constexpr double minimum = static_cast<double>((std::numeric_limits<std::int32_t>::min)());
        constexpr double maximum = static_cast<double>((std::numeric_limits<std::int32_t>::max)());
        return static_cast<std::int32_t>(std::clamp(value, minimum, maximum));
    };

    const D3dScissorRect result{
        .left = toLong(std::floor(left)),
        .top = toLong(std::floor(flippedTop)),
        .right = toLong(std::ceil(right)),
        .bottom = toLong(std::ceil(flippedBottom)),
    };
    if (!result.isValid()) {
        return std::nullopt;
    }
    return result;
}

Nv12ColorTransform nv12ColorTransform(const domain::ColorMetadata& metadata) noexcept {
    const bool bt709 = metadata.matrix == domain::ColorMatrix::kBt709;
    const float redLuma = bt709 ? 0.2126F : 0.299F;
    const float blueLuma = bt709 ? 0.0722F : 0.114F;
    const float greenLuma = 1.0F - redLuma - blueLuma;

    const float redFromCr = 2.0F * (1.0F - redLuma);
    const float blueFromCb = 2.0F * (1.0F - blueLuma);
    const float greenFromCb = -2.0F * blueLuma * (1.0F - blueLuma) / greenLuma;
    const float greenFromCr = -2.0F * redLuma * (1.0F - redLuma) / greenLuma;

    const bool limited = metadata.range == domain::ColorRange::kLimited;
    const float yScale = limited ? (255.0F / 219.0F) : 1.0F;
    const float yOffset = limited ? (-16.0F / 219.0F) : 0.0F;
    const float chromaScale = limited ? (255.0F / 224.0F) : 1.0F;
    const float chromaOffset = limited ? (-128.0F / 224.0F) : -0.5F;

    return Nv12ColorTransform{
        .yuvToRgb =
            {
                yScale,
                0.0F,
                redFromCr * chromaScale,
                yOffset + (redFromCr * chromaOffset),
                yScale,
                greenFromCb * chromaScale,
                greenFromCr * chromaScale,
                yOffset + ((greenFromCb + greenFromCr) * chromaOffset),
                yScale,
                blueFromCb * chromaScale,
                0.0F,
                yOffset + (blueFromCb * chromaOffset),
            },
    };
}

class D3d11ComparisonRenderer::Impl final {
public:
    Impl(std::shared_ptr<GraphicsDeviceBroker> deviceBroker,
         std::shared_ptr<FrameMailbox> frameMailbox,
         std::shared_ptr<PresentationAckMailbox> acknowledgementMailbox,
         std::weak_ptr<IRenderActivitySink> activitySink) noexcept
        : deviceBroker_(std::move(deviceBroker)), frameMailbox_(std::move(frameMailbox)),
          acknowledgementMailbox_(std::move(acknowledgementMailbox)),
          activitySink_(std::move(activitySink)) {}

    [[nodiscard]] ComparisonRenderResult render(const SurfaceRenderState& state) noexcept {
        if (!state.isValid() || !deviceBroker_ || !frameMailbox_ || !acknowledgementMailbox_) {
            return ComparisonRenderResult::InvalidState;
        }

        retryPendingAcknowledgement();
        if (acknowledgementClosed_) {
            return ComparisonRenderResult::Closed;
        }

        const GraphicsDeviceLeaseResult leaseResult = deviceBroker_->tryLease();
        if (leaseResult.status == GraphicsDeviceLeaseStatus::Busy) {
            return ComparisonRenderResult::Contended;
        }
        if (leaseResult.status == GraphicsDeviceLeaseStatus::Closed) {
            return ComparisonRenderResult::Closed;
        }
        if (leaseResult.status != GraphicsDeviceLeaseStatus::Available ||
            !leaseResult.lease.has_value()) {
            return ComparisonRenderResult::DeviceUnavailable;
        }
        const GraphicsDeviceLease& lease = *leaseResult.lease;
        if (!ensureDeviceResources(lease)) {
            return ComparisonRenderResult::ResourceFailure;
        }
        if (!applyRenderState(state, *lease.immediateContext.Get())) {
            return ComparisonRenderResult::InvalidState;
        }

        // A full lossless ACK queue is presentation backpressure. Keep repainting the retained
        // front pair, but do not expose a later publication until the pending event is admitted.
        if (pendingAcknowledgement_.has_value()) {
            return drawFrontOrBackground(state, lease) ? ComparisonRenderResult::PresentedAckPending
                                                       : ComparisonRenderResult::ResourceFailure;
        }

        const FrameMailboxReadResult read = frameMailbox_->tryLatest(lease.deviceGeneration);
        switch (read.status) {
        case FrameMailboxReadStatus::Contended:
            return drawFrontOrBackground(state, lease) ? ComparisonRenderResult::Contended
                                                       : ComparisonRenderResult::ResourceFailure;
        case FrameMailboxReadStatus::Closed:
            return ComparisonRenderResult::Closed;
        case FrameMailboxReadStatus::DeviceGenerationMismatch:
            frontPublication_.reset();
            return ComparisonRenderResult::DeviceUnavailable;
        case FrameMailboxReadStatus::Empty:
            return drawFrontOrBackground(state, lease) ? ComparisonRenderResult::BackgroundOnly
                                                       : ComparisonRenderResult::ResourceFailure;
        case FrameMailboxReadStatus::Available:
            break;
        }
        if (!read.publication.has_value() || !read.publication->set) {
            return drawFrontOrBackground(state, lease) ? ComparisonRenderResult::BackgroundOnly
                                                       : ComparisonRenderResult::ResourceFailure;
        }

        const FrameMailboxPublication publication = *read.publication;
        if (frontPublication_.has_value() &&
            frontPublication_->publicationSerial == publication.publicationSerial &&
            frontPublication_->set == publication.set) {
            return drawFrontOrBackground(state, lease) ? ComparisonRenderResult::Presented
                                                       : ComparisonRenderResult::ResourceFailure;
        }

        PreparedSetDraw prepared;
        if (!prepareSetDraw(state, publication, lease, prepared)) {
            return frontPublication_.has_value() && drawFrontOrBackground(state, lease)
                       ? ComparisonRenderResult::BackgroundOnly
                       : ComparisonRenderResult::ResourceFailure;
        }

        // Replacement is checked after every fallible preparation and immediately before the
        // first command. The new pair replaces the pinned front only after both sides are issued.
        const FrameMailboxDrawStatus drawStatus =
            frameMailbox_->validateForDraw(publication, lease.deviceGeneration);
        if (drawStatus == FrameMailboxDrawStatus::Contended) {
            return drawFrontOrBackground(state, lease) ? ComparisonRenderResult::Contended
                                                       : ComparisonRenderResult::ResourceFailure;
        }
        if (drawStatus == FrameMailboxDrawStatus::Closed) {
            return ComparisonRenderResult::Closed;
        }
        if (drawStatus == FrameMailboxDrawStatus::DeviceGenerationMismatch) {
            frontPublication_.reset();
            return ComparisonRenderResult::DeviceUnavailable;
        }
        if (drawStatus == FrameMailboxDrawStatus::Superseded) {
            return drawFrontOrBackground(state, lease) ? ComparisonRenderResult::BackgroundOnly
                                                       : ComparisonRenderResult::ResourceFailure;
        }

        drawPreparedSet(prepared, *lease.immediateContext.Get());
        frontPublication_ = publication;
        return acknowledge(publication, *publication.set);
    }

    void releaseResources() noexcept {
        // Drop the front publication before the device objects so its deferred-retirement
        // deleters can observe a still-valid broker/device generation during teardown.
        frontPublication_.reset();
        colorBufferB_.Reset();
        colorBufferA_.Reset();
        differenceBuffer_.Reset();
        composeBufferB_.Reset();
        composeBufferA_.Reset();
        for (ComPtr<ID3D11Buffer>& buffer : blackComposeBuffers_) {
            buffer.Reset();
        }
        linearSampler_.Reset();
        nearestSampler_.Reset();
        scissorRasterizerState_.Reset();
        rasterizerState_.Reset();
        stencilState_.Reset();
        depthState_.Reset();
        blendState_.Reset();
        differencePixelShader_.Reset();
        nv12PixelShader_.Reset();
        blackPixelShader_.Reset();
        vertexShader_.Reset();
        device_.Reset();
        deviceGeneration_ = domain::DeviceGeneration{0U};
    }

private:
    [[nodiscard]] bool ensureDeviceResources(const GraphicsDeviceLease& lease) noexcept {
        if (device_ && device_.Get() == lease.device.Get() &&
            deviceGeneration_ == lease.deviceGeneration) {
            return true;
        }
        releaseResources();
        if (!lease.device || !lease.immediateContext) {
            return false;
        }

        device_ = lease.device;
        deviceGeneration_ = lease.deviceGeneration;
        HRESULT result = device_->CreateVertexShader(shaders::kComposeVertexShader.data(),
                                                     shaders::kComposeVertexShader.size(),
                                                     nullptr,
                                                     vertexShader_.GetAddressOf());
        if (FAILED(result)) {
            return failDevice(result, lease);
        }
        result = device_->CreatePixelShader(shaders::kBlackPixelShader.data(),
                                            shaders::kBlackPixelShader.size(),
                                            nullptr,
                                            blackPixelShader_.GetAddressOf());
        if (FAILED(result)) {
            return failDevice(result, lease);
        }
        result = device_->CreatePixelShader(shaders::kNv12PixelShader.data(),
                                            shaders::kNv12PixelShader.size(),
                                            nullptr,
                                            nv12PixelShader_.GetAddressOf());
        if (FAILED(result)) {
            return failDevice(result, lease);
        }
        result = device_->CreatePixelShader(shaders::kDifferencePixelShader.data(),
                                            shaders::kDifferencePixelShader.size(),
                                            nullptr,
                                            differencePixelShader_.GetAddressOf());
        if (FAILED(result)) {
            return failDevice(result, lease);
        }

        for (ComPtr<ID3D11Buffer>& buffer : blackComposeBuffers_) {
            result = createDynamicConstantBuffer(*device_.Get(), sizeof(ComposeConstants), buffer);
            if (FAILED(result)) {
                return failDevice(result, lease);
            }
        }
        if (FAILED(result = createDynamicConstantBuffer(
                       *device_.Get(), sizeof(ComposeConstants), composeBufferA_)) ||
            FAILED(result = createDynamicConstantBuffer(
                       *device_.Get(), sizeof(ComposeConstants), composeBufferB_)) ||
            FAILED(result = createDynamicConstantBuffer(
                       *device_.Get(), sizeof(ColorConstants), colorBufferA_)) ||
            FAILED(result = createDynamicConstantBuffer(
                       *device_.Get(), sizeof(ColorConstants), colorBufferB_)) ||
            FAILED(result = createDynamicConstantBuffer(
                       *device_.Get(), sizeof(DifferenceConstants), differenceBuffer_))) {
            return failDevice(result, lease);
        }

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.ComparisonFunc = D3D11_COMPARISON_NEVER;
        samplerDescription.MinLOD = 0.0F;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        result = device_->CreateSamplerState(&samplerDescription, nearestSampler_.GetAddressOf());
        if (FAILED(result)) {
            return failDevice(result, lease);
        }
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        result = device_->CreateSamplerState(&samplerDescription, linearSampler_.GetAddressOf());
        if (FAILED(result)) {
            return failDevice(result, lease);
        }

        D3D11_BLEND_DESC blendDescription{};
        D3D11_RENDER_TARGET_BLEND_DESC& renderTarget = blendDescription.RenderTarget[0U];
        renderTarget.BlendEnable = TRUE;
        renderTarget.SrcBlend = D3D11_BLEND_ONE;
        renderTarget.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        renderTarget.BlendOp = D3D11_BLEND_OP_ADD;
        renderTarget.SrcBlendAlpha = D3D11_BLEND_ONE;
        renderTarget.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        renderTarget.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        renderTarget.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        result = device_->CreateBlendState(&blendDescription, blendState_.GetAddressOf());
        if (FAILED(result)) {
            return failDevice(result, lease);
        }

        D3D11_DEPTH_STENCIL_DESC depthDescription{};
        depthDescription.DepthEnable = FALSE;
        depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDescription.DepthFunc = D3D11_COMPARISON_ALWAYS;
        result = device_->CreateDepthStencilState(&depthDescription, depthState_.GetAddressOf());
        if (FAILED(result)) {
            return failDevice(result, lease);
        }
        depthDescription.StencilEnable = TRUE;
        depthDescription.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
        depthDescription.StencilWriteMask = 0U;
        depthDescription.FrontFace = D3D11_DEPTH_STENCILOP_DESC{
            .StencilFailOp = D3D11_STENCIL_OP_KEEP,
            .StencilDepthFailOp = D3D11_STENCIL_OP_KEEP,
            .StencilPassOp = D3D11_STENCIL_OP_KEEP,
            .StencilFunc = D3D11_COMPARISON_EQUAL,
        };
        depthDescription.BackFace = depthDescription.FrontFace;
        result = device_->CreateDepthStencilState(&depthDescription, stencilState_.GetAddressOf());
        if (FAILED(result)) {
            return failDevice(result, lease);
        }

        D3D11_RASTERIZER_DESC rasterizerDescription{};
        rasterizerDescription.FillMode = D3D11_FILL_SOLID;
        rasterizerDescription.CullMode = D3D11_CULL_NONE;
        rasterizerDescription.DepthClipEnable = TRUE;
        result =
            device_->CreateRasterizerState(&rasterizerDescription, rasterizerState_.GetAddressOf());
        if (FAILED(result)) {
            return failDevice(result, lease);
        }
        rasterizerDescription.ScissorEnable = TRUE;
        result = device_->CreateRasterizerState(&rasterizerDescription,
                                                scissorRasterizerState_.GetAddressOf());
        if (FAILED(result)) {
            return failDevice(result, lease);
        }
        return true;
    }

    [[nodiscard]] bool failDevice(const HRESULT result, const GraphicsDeviceLease& lease) noexcept {
        HRESULT reason = lease.device ? lease.device->GetDeviceRemovedReason() : result;
        if (SUCCEEDED(reason) && isRemovedResult(result)) {
            reason = result;
        }
        if (FAILED(reason)) {
            static_cast<void>(deviceBroker_->reportDeviceLost(lease.deviceGeneration, reason));
        }
        releaseResources();
        return false;
    }

    [[nodiscard]] bool applyRenderState(const SurfaceRenderState& state,
                                        ID3D11DeviceContext& context) const noexcept {
        std::optional<D3dScissorRect> convertedScissor;
        if (state.scissorEnabled) {
            UINT viewportCount = 1U;
            D3D11_VIEWPORT viewport{};
            context.RSGetViewports(&viewportCount, &viewport);
            if (viewportCount == 0U) {
                return false;
            }

            ComPtr<ID3D11RenderTargetView> renderTarget;
            context.OMGetRenderTargets(1U, renderTarget.GetAddressOf(), nullptr);
            ComPtr<ID3D11Resource> renderTargetResource;
            if (renderTarget) {
                renderTarget->GetResource(renderTargetResource.GetAddressOf());
            }
            ComPtr<ID3D11Texture2D> renderTargetTexture;
            if (!renderTargetResource || FAILED(renderTargetResource.As(&renderTargetTexture)) ||
                !renderTargetTexture) {
                return false;
            }
            D3D11_TEXTURE2D_DESC renderTargetDescription{};
            renderTargetTexture->GetDesc(&renderTargetDescription);
            convertedScissor = d3dScissorFromBottomLeft(state.scissor,
                                                        SurfaceViewport{
                                                            .topLeftX = viewport.TopLeftX,
                                                            .topLeftY = viewport.TopLeftY,
                                                            .width = viewport.Width,
                                                            .height = viewport.Height,
                                                        },
                                                        renderTargetDescription.Height);
            if (!convertedScissor.has_value()) {
                return false;
            }
        }

        const float blendFactor[4U] = {0.0F, 0.0F, 0.0F, 0.0F};
        context.OMSetBlendState(blendState_.Get(), blendFactor, 0xFFFFFFFFU);
        context.OMSetDepthStencilState(
            state.stencilEnabled ? stencilState_.Get() : depthState_.Get(), state.stencilReference);
        context.RSSetState(state.scissorEnabled ? scissorRasterizerState_.Get()
                                                : rasterizerState_.Get());
        if (convertedScissor.has_value()) {
            const D3D11_RECT rectangle{
                .left = convertedScissor->left,
                .top = convertedScissor->top,
                .right = convertedScissor->right,
                .bottom = convertedScissor->bottom,
            };
            context.RSSetScissorRects(1U, &rectangle);
        }
        context.IASetInputLayout(nullptr);
        context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context.VSSetShader(vertexShader_.Get(), nullptr, 0U);
        context.HSSetShader(nullptr, nullptr, 0U);
        context.DSSetShader(nullptr, nullptr, 0U);
        context.GSSetShader(nullptr, nullptr, 0U);
        return true;
    }

    [[nodiscard]] bool drawBackground(const SurfaceRenderState& state,
                                      const GraphicsDeviceLease& lease) noexcept {
        const ComposeConstants constants = makeBlackConstants(state);
        const HRESULT result = writeConstantBuffer(
            *lease.immediateContext.Get(), *blackComposeBuffers_[0U].Get(), constants);
        if (FAILED(result)) {
            return failDevice(result, lease);
        }
        ID3D11Buffer* const composeBuffer = blackComposeBuffers_[0U].Get();
        lease.immediateContext->VSSetConstantBuffers(0U, 1U, &composeBuffer);
        lease.immediateContext->PSSetShader(blackPixelShader_.Get(), nullptr, 0U);
        lease.immediateContext->Draw(4U, 0U);
        return true;
    }

    [[nodiscard]] bool prepareSetDraw(const SurfaceRenderState& state,
                                      const FrameMailboxPublication& publication,
                                      const GraphicsDeviceLease& lease,
                                      PreparedSetDraw& prepared) noexcept {
        if (!publication.set) {
            return false;
        }
        const GpuFrameSet& set = *publication.set;
        const std::span<const GpuFrameSlot> slots = set.slots();

        // Extract first two slots; missing slots result in black panels.
        const GpuFrameSlot* const slotA = (slots.size() > 0U) ? &slots[0U] : nullptr;
        const GpuFrameSlot* const slotB = (slots.size() > 1U) ? &slots[1U] : nullptr;
        const std::shared_ptr<const GpuFrameResource> frameA = slotA ? slotA->frame : nullptr;
        const std::shared_ptr<const GpuFrameResource> frameB = slotB ? slotB->frame : nullptr;
        const D3d11GpuFrameBacking* const backingA = d3dBacking(frameA);
        const D3d11GpuFrameBacking* const backingB = d3dBacking(frameB);

        // At least one frame must be present for rendering.
        if (!frameA && !frameB) {
            return false;
        }

        ID3D11DeviceContext& context = *lease.immediateContext.Get();
        HRESULT result = S_OK;
        if (state.viewMode == SurfaceViewMode::Difference) {
            // Difference view requires two frames. If fewer than two frames are present,
            // show the first frame plain (or black if no frames).
            if (!frameA || !frameB || backingA == nullptr || backingB == nullptr ||
                backingA->yView() == nullptr || backingA->uvView() == nullptr ||
                backingB->yView() == nullptr || backingB->uvView() == nullptr) {
                // Fall back to showing the first frame plain, or black if no frames.
                if (frameA && backingA && backingA->yView() && backingA->uvView()) {
                    const SurfaceRect bounds{
                        .x = 0.0F,
                        .y = 0.0F,
                        .width = state.logicalWidth,
                        .height = state.logicalHeight,
                    };
                    const SurfaceRect destination =
                        aspectFitRect(bounds, frameA->geometry().width, frameA->geometry().height);
                    if (!destination.isValid()) {
                        return false;
                    }
                    prepared = PreparedSetDraw{
                        .publication = publication,
                        .drawA = makeVideoDraw(state, destination, *frameA, *backingA),
                    };
                    appendLetterboxBars(state, bounds, destination, prepared);
                    result = writeConstantBuffer(context, *composeBufferA_.Get(), prepared.drawA.compose);
                    if (SUCCEEDED(result)) {
                        result = writeConstantBuffer(context, *colorBufferA_.Get(), prepared.drawA.color);
                    }
                } else {
                    // No frames available; draw black.
                    prepared = PreparedSetDraw{.publication = publication};
                }
            } else {
                const SurfaceRect bounds{
                    .x = 0.0F,
                    .y = 0.0F,
                    .width = state.logicalWidth,
                    .height = state.logicalHeight,
                };
                const GpuFrameResource& reference =
                    state.differenceReference == SurfaceDifferenceReference::SourceA ? *frameA
                                                                                     : *frameB;
                const SurfaceRect destination =
                    aspectFitRect(bounds, reference.geometry().width, reference.geometry().height);
                if (!destination.isValid()) {
                    return false;
                }
                prepared = PreparedSetDraw{
                    .publication = publication,
                    .difference = true,
                    .differenceDraw =
                        makeDifferenceDraw(state, destination, *frameA, *backingA, *frameB, *backingB),
                };
                appendLetterboxBars(state, bounds, destination, prepared);
                result = writeConstantBuffer(
                    context, *composeBufferA_.Get(), prepared.differenceDraw.compose);
                if (SUCCEEDED(result)) {
                    result = writeConstantBuffer(
                        context, *colorBufferA_.Get(), prepared.differenceDraw.colorA);
                }
                if (SUCCEEDED(result)) {
                    result = writeConstantBuffer(
                        context, *colorBufferB_.Get(), prepared.differenceDraw.colorB);
                }
                if (SUCCEEDED(result)) {
                    result = writeConstantBuffer(
                        context, *differenceBuffer_.Get(), prepared.differenceDraw.options);
                }
            }
        } else {
            // Side-by-side view: draw first two slots left/right; missing slot → cleared/black panel.
            const SurfaceSplitLayout split = computeSurfaceSplit(
                state.logicalWidth, state.logicalHeight, state.pixelWidth, state.pixelHeight);

            if (frameA && backingA && backingA->yView() && backingA->uvView()) {
                const SurfaceRect destinationA =
                    aspectFitRect(split.left, frameA->geometry().width, frameA->geometry().height);
                if (!destinationA.isValid()) {
                    return false;
                }
                prepared.drawA = makeVideoDraw(state, destinationA, *frameA, *backingA);
                appendLetterboxBars(state, split.left, destinationA, prepared);
            }
            if (frameB && backingB && backingB->yView() && backingB->uvView()) {
                const SurfaceRect destinationB =
                    aspectFitRect(split.right, frameB->geometry().width, frameB->geometry().height);
                if (!destinationB.isValid()) {
                    return false;
                }
                prepared.drawB = makeVideoDraw(state, destinationB, *frameB, *backingB);
                appendLetterboxBars(state, split.right, destinationB, prepared);
            }

            prepared.publication = publication;
            if (frameA && backingA && backingA->yView() && backingA->uvView()) {
                result = writeConstantBuffer(context, *composeBufferA_.Get(), prepared.drawA.compose);
                if (SUCCEEDED(result)) {
                    result = writeConstantBuffer(context, *colorBufferA_.Get(), prepared.drawA.color);
                }
            }
            if (SUCCEEDED(result) && frameB && backingB && backingB->yView() && backingB->uvView()) {
                result = writeConstantBuffer(context, *composeBufferB_.Get(), prepared.drawB.compose);
                if (SUCCEEDED(result)) {
                    result = writeConstantBuffer(context, *colorBufferB_.Get(), prepared.drawB.color);
                }
            }
        }
        for (std::size_t index = 0U; SUCCEEDED(result) && index < prepared.letterboxBarCount;
             ++index) {
            result = writeConstantBuffer(
                context, *blackComposeBuffers_[index].Get(), prepared.letterboxBars[index]);
        }
        return SUCCEEDED(result) || failDevice(result, lease);
    }

    void drawPreparedSet(const PreparedSetDraw& prepared,
                         ID3D11DeviceContext& context) const noexcept {
        context.PSSetShader(blackPixelShader_.Get(), nullptr, 0U);
        for (std::size_t index = 0U; index < prepared.letterboxBarCount; ++index) {
            ID3D11Buffer* const composeBuffer = blackComposeBuffers_[index].Get();
            context.VSSetConstantBuffers(0U, 1U, &composeBuffer);
            context.Draw(4U, 0U);
        }

        if (prepared.difference) {
            drawDifference(context, prepared.differenceDraw);
        } else {
            drawVideoSlots(context, prepared.drawA, prepared.drawB);
        }
        const std::array<ID3D11ShaderResourceView*, 4U> nullViews{};
        context.PSSetShaderResources(0U, static_cast<UINT>(nullViews.size()), nullViews.data());
    }

    void drawVideoSlots(ID3D11DeviceContext& context,
                        const VideoDraw& drawA,
                        const VideoDraw& drawB) const noexcept {
        context.PSSetShader(nv12PixelShader_.Get(), nullptr, 0U);
        ID3D11SamplerState* const sampler = linearSampler_.Get();
        context.PSSetSamplers(0U, 1U, &sampler);

        // Draw slot A if present (yView is non-null).
        if (drawA.yView != nullptr && drawA.uvView != nullptr) {
            ID3D11Buffer* composeBuffer = composeBufferA_.Get();
            ID3D11Buffer* colorBuffer = colorBufferA_.Get();
            const std::array<ID3D11ShaderResourceView*, 2U> viewsA{drawA.yView, drawA.uvView};
            context.VSSetConstantBuffers(0U, 1U, &composeBuffer);
            context.PSSetConstantBuffers(1U, 1U, &colorBuffer);
            context.PSSetShaderResources(0U, static_cast<UINT>(viewsA.size()), viewsA.data());
            context.Draw(4U, 0U);
        }

        // Draw slot B if present (yView is non-null).
        if (drawB.yView != nullptr && drawB.uvView != nullptr) {
            ID3D11Buffer* composeBuffer = composeBufferB_.Get();
            ID3D11Buffer* colorBuffer = colorBufferB_.Get();
            const std::array<ID3D11ShaderResourceView*, 2U> viewsB{drawB.yView, drawB.uvView};
            context.VSSetConstantBuffers(0U, 1U, &composeBuffer);
            context.PSSetConstantBuffers(1U, 1U, &colorBuffer);
            context.PSSetShaderResources(0U, static_cast<UINT>(viewsB.size()), viewsB.data());
            context.Draw(4U, 0U);
        }
    }

    void drawDifference(ID3D11DeviceContext& context, const DifferenceDraw& draw) const noexcept {
        context.PSSetShader(differencePixelShader_.Get(), nullptr, 0U);
        ID3D11SamplerState* const sampler = draw.filter == SurfaceDifferenceFilter::Bilinear
                                                ? linearSampler_.Get()
                                                : nearestSampler_.Get();
        context.PSSetSamplers(0U, 1U, &sampler);

        ID3D11Buffer* const composeBuffer = composeBufferA_.Get();
        const std::array<ID3D11Buffer*, 3U> pixelBuffers{
            colorBufferA_.Get(), colorBufferB_.Get(), differenceBuffer_.Get()};
        const std::array<ID3D11ShaderResourceView*, 4U> views{
            draw.yViewA, draw.uvViewA, draw.yViewB, draw.uvViewB};
        context.VSSetConstantBuffers(0U, 1U, &composeBuffer);
        context.PSSetConstantBuffers(
            1U, static_cast<UINT>(pixelBuffers.size()), pixelBuffers.data());
        context.PSSetShaderResources(0U, static_cast<UINT>(views.size()), views.data());
        context.Draw(4U, 0U);
    }

    [[nodiscard]] bool drawFrontOrBackground(const SurfaceRenderState& state,
                                             const GraphicsDeviceLease& lease) noexcept {
        if (!frontPublication_.has_value()) {
            return drawBackground(state, lease);
        }

        PreparedSetDraw prepared;
        if (!prepareSetDraw(state, *frontPublication_, lease, prepared)) {
            return false;
        }
        drawPreparedSet(prepared, *lease.immediateContext.Get());
        return true;
    }

    void retryPendingAcknowledgement() noexcept {
        if (!pendingAcknowledgement_.has_value() || acknowledgementClosed_) {
            return;
        }
        const PresentationAckPushResult result =
            acknowledgementMailbox_->tryPush(pendingAcknowledgement_->event);
        if (result == PresentationAckPushResult::Accepted) {
            pendingAcknowledgement_.reset();
            notifyAcknowledgementPublished();
        } else if (result == PresentationAckPushResult::Closed) {
            pendingAcknowledgement_.reset();
            acknowledgementClosed_ = true;
        }
    }

    [[nodiscard]] ComparisonRenderResult acknowledge(const FrameMailboxPublication& publication,
                                                    const GpuFrameSet& set) noexcept {
        if (acknowledgementClosed_) {
            return ComparisonRenderResult::Closed;
        }
        if (publication.publicationSerial <= highestAcknowledgementSerial_) {
            return pendingAcknowledgement_.has_value() ? ComparisonRenderResult::PresentedAckPending
                                                       : ComparisonRenderResult::Presented;
        }
        if (pendingAcknowledgement_.has_value()) {
            return ComparisonRenderResult::PresentedAckPending;
        }

        const application::FrameSetPresented event{
            .context = set.context(),
            .frameId = set.frameId(),
        };
        highestAcknowledgementSerial_ = publication.publicationSerial;
        const PresentationAckPushResult result = acknowledgementMailbox_->tryPush(event);
        if (result == PresentationAckPushResult::Accepted) {
            notifyAcknowledgementPublished();
            return ComparisonRenderResult::Presented;
        }
        if (result == PresentationAckPushResult::Full) {
            pendingAcknowledgement_ = PendingAcknowledgement{
                .publicationSerial = publication.publicationSerial,
                .event = event,
            };
            return ComparisonRenderResult::PresentedAckPending;
        }
        acknowledgementClosed_ = true;
        return ComparisonRenderResult::Closed;
    }

    void notifyAcknowledgementPublished() noexcept {
        if (const std::shared_ptr<IRenderActivitySink> sink = activitySink_.lock()) {
            sink->notifyAckPublished();
        }
    }

    struct PendingAcknowledgement final {
        std::uint64_t publicationSerial = 0U;
        application::FrameSetPresented event;
    };

    std::shared_ptr<GraphicsDeviceBroker> deviceBroker_;
    std::shared_ptr<FrameMailbox> frameMailbox_;
    std::shared_ptr<PresentationAckMailbox> acknowledgementMailbox_;
    std::weak_ptr<IRenderActivitySink> activitySink_;
    std::optional<FrameMailboxPublication> frontPublication_;
    std::optional<PendingAcknowledgement> pendingAcknowledgement_;
    std::uint64_t highestAcknowledgementSerial_ = 0U;
    bool acknowledgementClosed_ = false;

    domain::DeviceGeneration deviceGeneration_{0U};
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11VertexShader> vertexShader_;
    ComPtr<ID3D11PixelShader> blackPixelShader_;
    ComPtr<ID3D11PixelShader> nv12PixelShader_;
    ComPtr<ID3D11PixelShader> differencePixelShader_;
    ComPtr<ID3D11BlendState> blendState_;
    ComPtr<ID3D11DepthStencilState> depthState_;
    ComPtr<ID3D11DepthStencilState> stencilState_;
    ComPtr<ID3D11RasterizerState> rasterizerState_;
    ComPtr<ID3D11RasterizerState> scissorRasterizerState_;
    ComPtr<ID3D11SamplerState> nearestSampler_;
    ComPtr<ID3D11SamplerState> linearSampler_;
    std::array<ComPtr<ID3D11Buffer>, 8U> blackComposeBuffers_;
    ComPtr<ID3D11Buffer> composeBufferA_;
    ComPtr<ID3D11Buffer> composeBufferB_;
    ComPtr<ID3D11Buffer> colorBufferA_;
    ComPtr<ID3D11Buffer> colorBufferB_;
    ComPtr<ID3D11Buffer> differenceBuffer_;
};

D3d11ComparisonRenderer::D3d11ComparisonRenderer(
    std::shared_ptr<GraphicsDeviceBroker> deviceBroker,
    std::shared_ptr<FrameMailbox> frameMailbox,
    std::shared_ptr<PresentationAckMailbox> acknowledgementMailbox,
    std::weak_ptr<IRenderActivitySink> activitySink)
    : impl_(std::make_unique<Impl>(std::move(deviceBroker),
                                   std::move(frameMailbox),
                                   std::move(acknowledgementMailbox),
                                   std::move(activitySink))) {}

D3d11ComparisonRenderer::~D3d11ComparisonRenderer() = default;

ComparisonRenderResult D3d11ComparisonRenderer::render(const SurfaceRenderState& state) noexcept {
    return impl_->render(state);
}

void D3d11ComparisonRenderer::releaseResources() noexcept {
    impl_->releaseResources();
}

} // namespace dvs::platform
