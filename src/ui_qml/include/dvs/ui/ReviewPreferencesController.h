#pragma once

#include "dvs/application/Ports.h"

#include <QObject>

#include <memory>

namespace dvs::ui {

class ReviewPreferencesController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(
        int shortcutPreset READ shortcutPreset WRITE setShortcutPreset NOTIFY preferencesChanged)
    Q_PROPERTY(bool dropFrameTimecode READ dropFrameTimecode WRITE setDropFrameTimecode NOTIFY
                   preferencesChanged)
    Q_PROPERTY(ViewMode viewMode READ viewMode WRITE setViewMode NOTIFY preferencesChanged)
    Q_PROPERTY(DifferenceMetric differenceMetric READ differenceMetric WRITE setDifferenceMetric
                   NOTIFY preferencesChanged)
    Q_PROPERTY(DifferenceGain differenceGain READ differenceGain WRITE setDifferenceGain NOTIFY
                   preferencesChanged)
    Q_PROPERTY(DifferenceEdge differenceEdge READ differenceEdge WRITE setDifferenceEdge NOTIFY
                   preferencesChanged)
    Q_PROPERTY(DifferenceFilter differenceFilter READ differenceFilter WRITE setDifferenceFilter
                   NOTIFY preferencesChanged)
    Q_PROPERTY(int oscMode READ oscMode WRITE setOscMode NOTIFY preferencesChanged)

public:
    enum class ViewMode {
        SideBySide,
        ThreeUp,
        ReferenceFocus,
        Difference,
        AnalysisGrid,
        Wipe,
    };
    Q_ENUM(ViewMode)

    enum class DifferenceMetric {
        RgbAbsolute,
        Luma,
        Chroma,
        Heatmap,
        ExactPlanes,
    };
    Q_ENUM(DifferenceMetric)

    enum class DifferenceGain {
        Gain1x,
        Gain2x,
        Gain4x,
        Gain8x,
        Gain16x,
    };
    Q_ENUM(DifferenceGain)

    // Which two source slots the difference view compares (slot order = session source order).
    enum class DifferenceEdge {
        Edge0And1,
        Edge0And2,
        Edge1And2,
    };
    Q_ENUM(DifferenceEdge)

    enum class DifferenceFilter {
        Nearest,
        Bilinear,
        Bicubic,
    };
    Q_ENUM(DifferenceFilter)

    explicit ReviewPreferencesController(
        std::shared_ptr<application::ISettingsRepository> repository, QObject* parent = nullptr);
    ~ReviewPreferencesController() override;

    ReviewPreferencesController(const ReviewPreferencesController&) = delete;
    ReviewPreferencesController& operator=(const ReviewPreferencesController&) = delete;
    ReviewPreferencesController(ReviewPreferencesController&&) = delete;
    ReviewPreferencesController& operator=(ReviewPreferencesController&&) = delete;

    [[nodiscard]] int shortcutPreset() const noexcept;
    [[nodiscard]] bool dropFrameTimecode() const noexcept;
    [[nodiscard]] ViewMode viewMode() const noexcept;
    [[nodiscard]] DifferenceMetric differenceMetric() const noexcept;
    [[nodiscard]] DifferenceGain differenceGain() const noexcept;
    [[nodiscard]] DifferenceEdge differenceEdge() const noexcept;
    [[nodiscard]] DifferenceFilter differenceFilter() const noexcept;
    [[nodiscard]] int oscMode() const noexcept;

    void setShortcutPreset(int value);
    void setDropFrameTimecode(bool value);
    void setViewMode(ViewMode value);
    void setDifferenceMetric(DifferenceMetric value);
    void setDifferenceGain(DifferenceGain value);
    void setDifferenceEdge(DifferenceEdge value);
    void setDifferenceFilter(DifferenceFilter value);
    void setOscMode(int value);

    Q_INVOKABLE void stop() noexcept;
    void processRepositoryEvents() noexcept;

Q_SIGNALS:
    void preferencesChanged();

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::ui
