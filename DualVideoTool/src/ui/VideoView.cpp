#include "ui/VideoView.h"

#include <QPainter>
#include <QPalette>
#include <QResizeEvent>

VideoView::VideoView(const QString& label, QWidget* parent)
    : QWidget(parent), label_(label) {
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setAutoFillBackground(true);
    setPalette(pal);
}

void VideoView::displayFrame(const VideoFrame& frame) {
    if (currentImage_.cacheKey() == frame.image.cacheKey()) {
        return;
    }
    currentImage_ = frame.image;
    updateTargetRect();
    update();
}

void VideoView::clear() {
    if (currentImage_.isNull()) {
        return;
    }
    currentImage_ = {};
    cachedImageSize_ = {};
    cachedTargetRect_ = {};
    update();
}

void VideoView::setLabel(const QString& label) {
    if (label_ == label) {
        return;
    }
    label_ = label;
    update();
}

void VideoView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateTargetRect();
}

void VideoView::updateTargetRect() {
    if (currentImage_.isNull()) {
        cachedImageSize_ = {};
        cachedWidgetSize_ = size();
        cachedTargetRect_ = {};
        return;
    }

    QSize imgSize = currentImage_.size();
    QSize widgetSize = size();
    if (imgSize == cachedImageSize_ && widgetSize == cachedWidgetSize_ && !cachedTargetRect_.isNull()) {
        return;
    }

    QSize scaledSize = imgSize.scaled(widgetSize, Qt::KeepAspectRatio);
    cachedTargetRect_ = QRect(
        (widgetSize.width() - scaledSize.width()) / 2,
        (widgetSize.height() - scaledSize.height()) / 2,
        scaledSize.width(),
        scaledSize.height()
    );
    cachedImageSize_ = imgSize;
    cachedWidgetSize_ = widgetSize;
}

void VideoView::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    if (currentImage_.isNull()) {
        // Draw placeholder
        painter.fillRect(rect(), Qt::black);
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, label_);
        return;
    }

    updateTargetRect();

    painter.fillRect(rect(), Qt::black);
    painter.drawImage(cachedTargetRect_, currentImage_);
}
