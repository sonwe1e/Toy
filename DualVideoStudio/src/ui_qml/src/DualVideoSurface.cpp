#include "dvs/ui/DualVideoSurface.h"

#include "dvs/platform/D3d11DualVideoRenderer.h"
#include "dvs/platform/FrameMailbox.h"
#include "dvs/platform/GraphicsDeviceBroker.h"
#include "dvs/platform/PresentationAckMailbox.h"
#include "dvs/platform/RenderActivitySink.h"
#include "dvs/ui/GraphicsBackend.h"

#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGRenderNode>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace dvs::ui {

class DualVideoSurface::Services final {
public:
    Services(std::shared_ptr<platform::GraphicsDeviceBroker> deviceBrokerValue,
             std::shared_ptr<platform::FrameMailbox> frameMailboxValue,
             std::shared_ptr<platform::PresentationAckMailbox> acknowledgementMailboxValue,
             std::weak_ptr<platform::IRenderActivitySink> activitySinkValue) noexcept
        : deviceBroker(std::move(deviceBrokerValue)), frameMailbox(std::move(frameMailboxValue)),
          acknowledgementMailbox(std::move(acknowledgementMailboxValue)),
          activitySink(std::move(activitySinkValue)) {}

    std::shared_ptr<platform::GraphicsDeviceBroker> deviceBroker;
    std::shared_ptr<platform::FrameMailbox> frameMailbox;
    std::shared_ptr<platform::PresentationAckMailbox> acknowledgementMailbox;
    std::weak_ptr<platform::IRenderActivitySink> activitySink;
};

namespace {

struct PresentationOptions final {
    platform::SurfaceViewMode viewMode = platform::SurfaceViewMode::SideBySide;
    platform::SurfaceDifferenceMetric differenceMetric =
        platform::SurfaceDifferenceMetric::RgbAbsolute;
    platform::SurfaceDifferenceGain differenceGain = platform::SurfaceDifferenceGain::Gain1x;
    platform::SurfaceDifferenceReference differenceReference =
        platform::SurfaceDifferenceReference::SourceA;
    platform::SurfaceDifferenceFilter differenceFilter =
        platform::SurfaceDifferenceFilter::Bilinear;
};

[[nodiscard]] platform::SurfaceViewMode
nativeViewMode(const DualVideoSurface::ViewMode value) noexcept {
    return value == DualVideoSurface::Difference ? platform::SurfaceViewMode::Difference
                                                 : platform::SurfaceViewMode::SideBySide;
}

[[nodiscard]] platform::SurfaceDifferenceMetric
nativeDifferenceMetric(const DualVideoSurface::DifferenceMetric value) noexcept {
    switch (value) {
    case DualVideoSurface::RgbAbsolute:
        return platform::SurfaceDifferenceMetric::RgbAbsolute;
    case DualVideoSurface::Luma:
        return platform::SurfaceDifferenceMetric::Luma;
    case DualVideoSurface::Chroma:
        return platform::SurfaceDifferenceMetric::Chroma;
    case DualVideoSurface::Heatmap:
        return platform::SurfaceDifferenceMetric::Heatmap;
    }
    return platform::SurfaceDifferenceMetric::RgbAbsolute;
}

[[nodiscard]] platform::SurfaceDifferenceGain
nativeDifferenceGain(const DualVideoSurface::DifferenceGain value) noexcept {
    switch (value) {
    case DualVideoSurface::Gain1x:
        return platform::SurfaceDifferenceGain::Gain1x;
    case DualVideoSurface::Gain2x:
        return platform::SurfaceDifferenceGain::Gain2x;
    case DualVideoSurface::Gain4x:
        return platform::SurfaceDifferenceGain::Gain4x;
    case DualVideoSurface::Gain8x:
        return platform::SurfaceDifferenceGain::Gain8x;
    case DualVideoSurface::Gain16x:
        return platform::SurfaceDifferenceGain::Gain16x;
    }
    return platform::SurfaceDifferenceGain::Gain1x;
}

[[nodiscard]] platform::SurfaceDifferenceReference
nativeDifferenceReference(const DualVideoSurface::DifferenceReference value) noexcept {
    return value == DualVideoSurface::ReferenceB ? platform::SurfaceDifferenceReference::SourceB
                                                 : platform::SurfaceDifferenceReference::SourceA;
}

[[nodiscard]] platform::SurfaceDifferenceFilter
nativeDifferenceFilter(const DualVideoSurface::DifferenceFilter value) noexcept {
    switch (value) {
    case DualVideoSurface::Nearest:
        return platform::SurfaceDifferenceFilter::Nearest;
    case DualVideoSurface::Bilinear:
        return platform::SurfaceDifferenceFilter::Bilinear;
    case DualVideoSurface::Bicubic:
        return platform::SurfaceDifferenceFilter::Bicubic;
    }
    return platform::SurfaceDifferenceFilter::Bilinear;
}

[[nodiscard]] PresentationOptions presentationOptions(const DualVideoSurface& surface) noexcept {
    return PresentationOptions{
        .viewMode = nativeViewMode(surface.viewMode()),
        .differenceMetric = nativeDifferenceMetric(surface.differenceMetric()),
        .differenceGain = nativeDifferenceGain(surface.differenceGain()),
        .differenceReference = nativeDifferenceReference(surface.differenceReference()),
        .differenceFilter = nativeDifferenceFilter(surface.differenceFilter()),
    };
}

[[nodiscard]] std::uint32_t pixelExtent(const qreal logicalExtent, const qreal dpr) noexcept {
    if (!std::isfinite(logicalExtent) || !std::isfinite(dpr) || logicalExtent <= 0.0 ||
        dpr <= 0.0) {
        return 0U;
    }
    const qreal pixels = std::round(logicalExtent * dpr);
    if (pixels <= 0.0 || pixels > static_cast<qreal>((std::numeric_limits<std::uint32_t>::max)())) {
        return 0U;
    }
    return static_cast<std::uint32_t>(pixels);
}

[[nodiscard]] std::array<float, 16U> matrixValues(const QMatrix4x4& matrix) noexcept {
    std::array<float, 16U> result{};
    matrix.copyDataTo(result.data());
    return result;
}

} // namespace

class DualVideoRenderNode final : public QSGRenderNode {
public:
    DualVideoRenderNode(QQuickWindow& window,
                        const std::shared_ptr<const DualVideoSurface::Services>& services)
        : window_(window), services_(services), renderer_(services_->deviceBroker,
                                                          services_->frameMailbox,
                                                          services_->acknowledgementMailbox,
                                                          services_->activitySink) {}

    void synchronize(const QRectF& bounds,
                     const qreal devicePixelRatio,
                     const PresentationOptions& options) noexcept {
        bounds_ = bounds;
        devicePixelRatio_ = devicePixelRatio;
        presentationOptions_ = options;
    }

    [[nodiscard]] bool
    usesServices(const std::shared_ptr<const DualVideoSurface::Services>& services) const noexcept {
        return services_ == services;
    }

    void render(const RenderState* const renderState) override {
        const GraphicsBackendResult backendResult =
            bindGraphicsBackendOnRenderThread(window_, *services_->deviceBroker);
        if (backendResult != GraphicsBackendResult::Ready &&
            backendResult != GraphicsBackendResult::AlreadyReady) {
            return;
        }
        if (renderState == nullptr || renderState->projectionMatrix() == nullptr ||
            matrix() == nullptr || !bounds_.isValid()) {
            return;
        }

        const QMatrix4x4 clipFromItem = *renderState->projectionMatrix() * *matrix();
        const QRect scissor = renderState->scissorRect();
        const int stencilValue = renderState->stencilValue();
        const platform::SurfaceRenderState state{
            .clipFromItem = matrixValues(clipFromItem),
            .logicalWidth = static_cast<float>(bounds_.width()),
            .logicalHeight = static_cast<float>(bounds_.height()),
            .pixelWidth = pixelExtent(bounds_.width(), devicePixelRatio_),
            .pixelHeight = pixelExtent(bounds_.height(), devicePixelRatio_),
            .opacity = static_cast<float>(std::clamp(inheritedOpacity(), 0.0, 1.0)),
            .scissorEnabled = renderState->scissorEnabled(),
            .scissor =
                platform::SurfaceScissorRect{
                    .left = scissor.left(),
                    .bottom = scissor.top(),
                    .right = scissor.right() + 1,
                    .top = scissor.bottom() + 1,
                },
            .stencilEnabled = renderState->stencilEnabled(),
            .stencilReference = static_cast<std::uint32_t>(std::clamp(stencilValue, 0, 255)),
            .viewMode = presentationOptions_.viewMode,
            .differenceMetric = presentationOptions_.differenceMetric,
            .differenceGain = presentationOptions_.differenceGain,
            .differenceReference = presentationOptions_.differenceReference,
            .differenceFilter = presentationOptions_.differenceFilter,
        };
        static_cast<void>(renderer_.render(state));
    }

    void releaseResources() override {
        renderer_.releaseResources();
    }

    [[nodiscard]] StateFlags changedStates() const override {
        return ScissorState | DepthState | StencilState | ColorState | BlendState | CullState;
    }

    [[nodiscard]] RenderingFlags flags() const override {
        return BoundedRectRendering | DepthAwareRendering;
    }

    [[nodiscard]] QRectF rect() const override {
        return bounds_;
    }

private:
    QQuickWindow& window_;
    std::shared_ptr<const DualVideoSurface::Services> services_;
    platform::D3d11DualVideoRenderer renderer_;
    QRectF bounds_;
    qreal devicePixelRatio_ = 1.0;
    PresentationOptions presentationOptions_;
};

DualVideoSurface::DualVideoSurface(QQuickItem* const parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
}

DualVideoSurface::~DualVideoSurface() = default;

DualVideoSurface::ViewMode DualVideoSurface::viewMode() const noexcept {
    return viewMode_;
}

void DualVideoSurface::setViewMode(const ViewMode value) {
    if ((value != SideBySide && value != Difference) || viewMode_ == value) {
        return;
    }
    viewMode_ = value;
    emit viewModeChanged();
    update();
}

DualVideoSurface::DifferenceMetric DualVideoSurface::differenceMetric() const noexcept {
    return differenceMetric_;
}

void DualVideoSurface::setDifferenceMetric(const DifferenceMetric value) {
    if ((value != RgbAbsolute && value != Luma && value != Chroma && value != Heatmap) ||
        differenceMetric_ == value) {
        return;
    }
    differenceMetric_ = value;
    emit differenceMetricChanged();
    update();
}

DualVideoSurface::DifferenceGain DualVideoSurface::differenceGain() const noexcept {
    return differenceGain_;
}

void DualVideoSurface::setDifferenceGain(const DifferenceGain value) {
    if ((value != Gain1x && value != Gain2x && value != Gain4x && value != Gain8x &&
         value != Gain16x) ||
        differenceGain_ == value) {
        return;
    }
    differenceGain_ = value;
    emit differenceGainChanged();
    update();
}

DualVideoSurface::DifferenceReference DualVideoSurface::differenceReference() const noexcept {
    return differenceReference_;
}

void DualVideoSurface::setDifferenceReference(const DifferenceReference value) {
    if ((value != ReferenceA && value != ReferenceB) || differenceReference_ == value) {
        return;
    }
    differenceReference_ = value;
    emit differenceReferenceChanged();
    update();
}

DualVideoSurface::DifferenceFilter DualVideoSurface::differenceFilter() const noexcept {
    return differenceFilter_;
}

void DualVideoSurface::setDifferenceFilter(const DifferenceFilter value) {
    if ((value != Nearest && value != Bilinear && value != Bicubic) || differenceFilter_ == value) {
        return;
    }
    differenceFilter_ = value;
    emit differenceFilterChanged();
    update();
}

bool DualVideoSurface::attachRendererServices(
    std::shared_ptr<platform::GraphicsDeviceBroker> deviceBroker,
    std::shared_ptr<platform::FrameMailbox> frameMailbox,
    std::shared_ptr<platform::PresentationAckMailbox> acknowledgementMailbox,
    std::weak_ptr<platform::IRenderActivitySink> activitySink) {
    if (!deviceBroker || !frameMailbox || !acknowledgementMailbox) {
        return false;
    }
    services_ = std::make_shared<Services>(std::move(deviceBroker),
                                           std::move(frameMailbox),
                                           std::move(acknowledgementMailbox),
                                           std::move(activitySink));
    update();
    return true;
}

void DualVideoSurface::detachRendererServices() noexcept {
    services_.reset();
    update();
}

bool DualVideoSurface::hasRendererServices() const noexcept {
    return services_ != nullptr;
}

QSGNode* DualVideoSurface::updatePaintNode(QSGNode* const oldNode, UpdatePaintNodeData*) {
    QQuickWindow* const itemWindow = window();
    if (!services_ || itemWindow == nullptr || width() <= 0.0 || height() <= 0.0) {
        delete oldNode;
        return nullptr;
    }

    auto* node = static_cast<DualVideoRenderNode*>(oldNode);
    if (node != nullptr && !node->usesServices(services_)) {
        delete node;
        node = nullptr;
    }
    if (node == nullptr) {
        node = new DualVideoRenderNode{*itemWindow, services_};
    }

    node->synchronize(
        boundingRect(), itemWindow->effectiveDevicePixelRatio(), presentationOptions(*this));
    return node;
}

void DualVideoSurface::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

} // namespace dvs::ui
