#pragma once

#include "dvs/application/Ports.h"

#include <QObject>

#include <memory>

namespace dvs::ui {

class ReviewPreferencesController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(
        int largeStepFrames READ largeStepFrames WRITE setLargeStepFrames NOTIFY preferencesChanged)
    Q_PROPERTY(ViewMode viewMode READ viewMode WRITE setViewMode NOTIFY preferencesChanged)
    Q_PROPERTY(DifferenceMetric differenceMetric READ differenceMetric WRITE setDifferenceMetric
                   NOTIFY preferencesChanged)
    Q_PROPERTY(DifferenceGain differenceGain READ differenceGain WRITE setDifferenceGain NOTIFY
                   preferencesChanged)
    Q_PROPERTY(DifferenceReference differenceReference READ differenceReference WRITE
                   setDifferenceReference NOTIFY preferencesChanged)
    Q_PROPERTY(DifferenceFilter differenceFilter READ differenceFilter WRITE setDifferenceFilter
                   NOTIFY preferencesChanged)

public:
    enum class ViewMode {
        SideBySide,
        Difference,
    };
    Q_ENUM(ViewMode)

    enum class DifferenceMetric {
        RgbAbsolute,
        Luma,
        Chroma,
        Heatmap,
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

    enum class DifferenceReference {
        ReferenceA,
        ReferenceB,
    };
    Q_ENUM(DifferenceReference)

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

    [[nodiscard]] int largeStepFrames() const noexcept;
    [[nodiscard]] ViewMode viewMode() const noexcept;
    [[nodiscard]] DifferenceMetric differenceMetric() const noexcept;
    [[nodiscard]] DifferenceGain differenceGain() const noexcept;
    [[nodiscard]] DifferenceReference differenceReference() const noexcept;
    [[nodiscard]] DifferenceFilter differenceFilter() const noexcept;

    void setLargeStepFrames(int value);
    void setViewMode(ViewMode value);
    void setDifferenceMetric(DifferenceMetric value);
    void setDifferenceGain(DifferenceGain value);
    void setDifferenceReference(DifferenceReference value);
    void setDifferenceFilter(DifferenceFilter value);

    Q_INVOKABLE void stop() noexcept;

Q_SIGNALS:
    void preferencesChanged();

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::ui
