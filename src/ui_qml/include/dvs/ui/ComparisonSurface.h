#pragma once

#include <QQuickItem>

#include <memory>

namespace dvs::platform {
class FrameMailbox;
class GraphicsDeviceBroker;
class IRenderActivitySink;
class PresentationAckMailbox;
} // namespace dvs::platform

namespace dvs::ui {

class ComparisonRenderNode;

// Thin Qt Quick synchronization boundary. All D3D resources and rendering behavior remain owned
// by the platform renderer living in the scene-graph node.
// Qt's QML type wrapper derives from registered C++ elements, so this visual boundary cannot be
// final even though production code does not subclass it directly.
class ComparisonSurface : public QQuickItem {
    Q_OBJECT

    Q_PROPERTY(ViewMode viewMode READ viewMode WRITE setViewMode NOTIFY viewModeChanged)
    Q_PROPERTY(DifferenceMetric differenceMetric READ differenceMetric WRITE setDifferenceMetric
                   NOTIFY differenceMetricChanged)
    Q_PROPERTY(DifferenceGain differenceGain READ differenceGain WRITE setDifferenceGain NOTIFY
                   differenceGainChanged)
    Q_PROPERTY(DifferenceReference differenceReference READ differenceReference WRITE
                   setDifferenceReference NOTIFY differenceReferenceChanged)
    Q_PROPERTY(DifferenceFilter differenceFilter READ differenceFilter WRITE setDifferenceFilter
                   NOTIFY differenceFilterChanged)

public:
    enum ViewMode {
        SideBySide,
        Difference,
    };
    Q_ENUM(ViewMode)

    enum DifferenceMetric {
        RgbAbsolute,
        Luma,
        Chroma,
        Heatmap,
    };
    Q_ENUM(DifferenceMetric)

    enum DifferenceGain {
        Gain1x,
        Gain2x,
        Gain4x,
        Gain8x,
        Gain16x,
    };
    Q_ENUM(DifferenceGain)

    enum DifferenceReference {
        ReferenceA,
        ReferenceB,
    };
    Q_ENUM(DifferenceReference)

    enum DifferenceFilter {
        Nearest,
        Bilinear,
        Bicubic,
    };
    Q_ENUM(DifferenceFilter)

    explicit ComparisonSurface(QQuickItem* parent = nullptr);
    ~ComparisonSurface() override;

    ComparisonSurface(const ComparisonSurface&) = delete;
    ComparisonSurface& operator=(const ComparisonSurface&) = delete;

    [[nodiscard]] ViewMode viewMode() const noexcept;
    void setViewMode(ViewMode value);
    [[nodiscard]] DifferenceMetric differenceMetric() const noexcept;
    void setDifferenceMetric(DifferenceMetric value);
    [[nodiscard]] DifferenceGain differenceGain() const noexcept;
    void setDifferenceGain(DifferenceGain value);
    [[nodiscard]] DifferenceReference differenceReference() const noexcept;
    void setDifferenceReference(DifferenceReference value);
    [[nodiscard]] DifferenceFilter differenceFilter() const noexcept;
    void setDifferenceFilter(DifferenceFilter value);

    [[nodiscard]] bool
    attachRendererServices(std::shared_ptr<platform::GraphicsDeviceBroker> deviceBroker,
                           std::shared_ptr<platform::FrameMailbox> frameMailbox,
                           std::shared_ptr<platform::PresentationAckMailbox> acknowledgementMailbox,
                           std::weak_ptr<platform::IRenderActivitySink> activitySink);
    void detachRendererServices() noexcept;
    [[nodiscard]] bool hasRendererServices() const noexcept;

signals:
    void viewModeChanged();
    void differenceMetricChanged();
    void differenceGainChanged();
    void differenceReferenceChanged();
    void differenceFilterChanged();

protected:
    [[nodiscard]] QSGNode* updatePaintNode(QSGNode* oldNode,
                                           UpdatePaintNodeData* updateData) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    friend class ComparisonRenderNode;

    class Services;

    std::shared_ptr<const Services> services_;
    ViewMode viewMode_ = SideBySide;
    DifferenceMetric differenceMetric_ = RgbAbsolute;
    DifferenceGain differenceGain_ = Gain1x;
    DifferenceReference differenceReference_ = ReferenceA;
    DifferenceFilter differenceFilter_ = Bilinear;
};

} // namespace dvs::ui
