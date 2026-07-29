#include "ui/TimelineWidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSlider>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QMouseEvent>
#include <QStyle>

TimelineWidget::TimelineWidget(QWidget* parent)
    : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 4, 8, 4);

    // Info row
    auto* infoLayout = new QHBoxLayout();
    frameLabel_ = new QLabel("帧: 0 / 0");
    timeLabel_ = new QLabel("时间: 00:00.000");
    speedLabel_ = new QLabel("1.0x");
    clipInfoLabel_ = new QLabel("");

    infoLayout->addWidget(frameLabel_);
    infoLayout->addSpacing(20);
    infoLayout->addWidget(timeLabel_);
    infoLayout->addSpacing(20);
    infoLayout->addWidget(speedLabel_);
    infoLayout->addStretch();
    infoLayout->addWidget(clipInfoLabel_);
    mainLayout->addLayout(infoLayout);

    // Slider
    slider_ = new QSlider(Qt::Horizontal);
    slider_->setMinimum(0);
    slider_->setMaximum(0);
    slider_->setTickPosition(QSlider::NoTicks);
    slider_->installEventFilter(this);
    mainLayout->addWidget(slider_);

    // Custom timeline bar (painted below slider)
    setMinimumHeight(60);

    dragThrottle_.start();

    // Mouse handling is done in eventFilter so single clicks and drags follow
    // the same preview-then-commit seek flow.
}

void TimelineWidget::setTotalFrames(int64_t totalFrames) {
    totalFrames_ = totalFrames;
    QSignalBlocker blocker(slider_);
    slider_->setMaximum(static_cast<int>(totalFrames - 1));
    updateLabels();
    update();
}

void TimelineWidget::setCurrentFrame(int64_t frame) {
    currentFrame_ = frame;
    if (!isDragging_) {
        QSignalBlocker blocker(slider_);
        slider_->setValue(static_cast<int>(frame));
    }
    updateLabels();
    update(); // trigger repaint for playhead
}

void TimelineWidget::setFps(double fps) {
    fps_ = fps;
    updateLabels();
}

void TimelineWidget::setSpeed(double speed) {
    speedLabel_->setText(QString("%1x").arg(speed, 0, 'f', speed < 1.0 ? 2 : 1));
}

void TimelineWidget::setInMarker(std::optional<int64_t> frame) {
    inMarker_ = frame;
    updateLabels();
    update();
}

void TimelineWidget::setOutMarker(std::optional<int64_t> frame) {
    outMarker_ = frame;
    updateLabels();
    update();
}

void TimelineWidget::updateLabels() {
    frameLabel_->setText(QString("帧: %1 / %2").arg(currentFrame_).arg(totalFrames_));

    double seconds = (fps_ > 0) ? (currentFrame_ / fps_) : 0.0;
    int mins = static_cast<int>(seconds) / 60;
    double secs = seconds - mins * 60;
    timeLabel_->setText(QString("时间: %1:%2")
                       .arg(mins, 2, 10, QChar('0'))
                       .arg(secs, 6, 'f', 3, QChar('0')));

    // Clip info
    if (inMarker_.has_value() && outMarker_.has_value()) {
        int64_t len = *outMarker_ - *inMarker_ + 1;
        double dur = (fps_ > 0) ? (len / fps_) : 0.0;
        clipInfoLabel_->setText(QString("片段: %1 - %2 (%3 帧, %4s)")
                                .arg(*inMarker_).arg(*outMarker_)
                                .arg(len).arg(dur, 0, 'f', 2));
    } else if (inMarker_.has_value()) {
        clipInfoLabel_->setText(QString("入点: %1").arg(*inMarker_));
    } else if (outMarker_.has_value()) {
        clipInfoLabel_->setText(QString("出点: %1").arg(*outMarker_));
    } else {
        clipInfoLabel_->setText("");
    }
}

int64_t TimelineWidget::frameFromSliderPosition(int x) const {
    if (totalFrames_ <= 1) return 0;

    int min = slider_->minimum();
    int max = slider_->maximum();
    int value = QStyle::sliderValueFromPosition(
        min, max, x, std::max(1, slider_->width()), false);
    return static_cast<int64_t>(std::clamp(value, min, max));
}

void TimelineWidget::setDragFrame(int64_t frame) {
    currentFrame_ = std::clamp(frame, static_cast<int64_t>(0),
                               totalFrames_ > 0 ? totalFrames_ - 1 : static_cast<int64_t>(0));
    QSignalBlocker blocker(slider_);
    slider_->setValue(static_cast<int>(currentFrame_));
    updateLabels();
    update();
}

bool TimelineWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched != slider_) {
        return QWidget::eventFilter(watched, event);
    }

    if (totalFrames_ <= 0) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) {
            return QWidget::eventFilter(watched, event);
        }

        isDragging_ = true;
        int64_t frame = frameFromSliderPosition(static_cast<int>(mouse->position().x()));
        setDragFrame(frame);
        dragThrottle_.restart();
        emit previewRequested(frame);
        return true;
    }

    if (event->type() == QEvent::MouseMove && isDragging_) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        int64_t frame = frameFromSliderPosition(static_cast<int>(mouse->position().x()));
        setDragFrame(frame);
        if (dragThrottle_.elapsed() >= 100) {
            dragThrottle_.restart();
            emit previewRequested(frame);
        }
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease && isDragging_) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) {
            return QWidget::eventFilter(watched, event);
        }

        int64_t frame = frameFromSliderPosition(static_cast<int>(mouse->position().x()));
        setDragFrame(frame);
        isDragging_ = false;
        emit seekRequested(frame);
        return true;
    }

    return QWidget::eventFilter(watched, event);
}

void TimelineWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Timeline bar area (below the slider)
    int barY = slider_->height() + 16;
    int barH = 24;
    int barX = 8;
    int barW = width() - 16;

    if (totalFrames_ <= 0) return;

    auto frameToX = [&](int64_t frame) -> int {
        return barX + static_cast<int>((double)frame / (totalFrames_ - 1) * barW);
    };

    // Background
    p.fillRect(barX, barY, barW, barH, QColor(40, 40, 40));

    // Selected range highlight
    if (inMarker_.has_value() && outMarker_.has_value()) {
        int x1 = frameToX(*inMarker_);
        int x2 = frameToX(*outMarker_);
        p.fillRect(x1, barY, x2 - x1, barH, QColor(60, 120, 200, 80));
    }

    // In marker
    if (inMarker_.has_value()) {
        int x = frameToX(*inMarker_);
        // Line
        p.setPen(QPen(QColor(80, 220, 80), 3));
        p.drawLine(x, barY, x, barY + barH);
        // Triangle handle
        QPolygon tri;
        tri << QPoint(x - 5, barY) << QPoint(x + 5, barY) << QPoint(x, barY + 6);
        p.setBrush(QColor(80, 220, 80));
        p.setPen(Qt::NoPen);
        p.drawPolygon(tri);
        // Label
        p.setPen(QColor(80, 220, 80));
        p.setFont(QFont("Arial", 7, QFont::Bold));
        p.drawText(x - 8, barY - 3, "IN");
    }

    // Out marker
    if (outMarker_.has_value()) {
        int x = frameToX(*outMarker_);
        p.setPen(QPen(QColor(220, 80, 80), 3));
        p.drawLine(x, barY, x, barY + barH);
        QPolygon tri;
        tri << QPoint(x - 5, barY) << QPoint(x + 5, barY) << QPoint(x, barY + 6);
        p.setBrush(QColor(220, 80, 80));
        p.setPen(Qt::NoPen);
        p.drawPolygon(tri);
        p.setPen(QColor(220, 80, 80));
        p.setFont(QFont("Arial", 7, QFont::Bold));
        p.drawText(x - 10, barY - 3, "OUT");
    }

    // Playhead (current frame)
    int px = frameToX(currentFrame_);
    p.setPen(QPen(Qt::white, 2));
    p.drawLine(px, barY - 2, px, barY + barH + 2);
}
