#pragma once

#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QElapsedTimer>
#include <cstdint>
#include <optional>

class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void setTotalFrames(int64_t totalFrames);
    void setCurrentFrame(int64_t frame);
    void setFps(double fps);
    void setSpeed(double speed);

    // Marker display is visual only; authoritative clip state lives in PlaybackEngine.
    void setInMarker(std::optional<int64_t> frame);
    void setOutMarker(std::optional<int64_t> frame);

signals:
    // Emitted on slider release (debounced seek)
    void seekRequested(int64_t frameIndex);
    // Emitted during drag at throttled rate (preview)
    void previewRequested(int64_t frameIndex);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void updateLabels();
    int64_t frameFromSliderPosition(int x) const;
    void setDragFrame(int64_t frame);

    QSlider* slider_;
    QLabel* frameLabel_;
    QLabel* timeLabel_;
    QLabel* speedLabel_;
    QLabel* clipInfoLabel_;

    int64_t totalFrames_ = 0;
    int64_t currentFrame_ = 0;
    double fps_ = 30.0;

    std::optional<int64_t> inMarker_;
    std::optional<int64_t> outMarker_;

    // Drag state
    bool isDragging_ = false;
    QElapsedTimer dragThrottle_;
};
