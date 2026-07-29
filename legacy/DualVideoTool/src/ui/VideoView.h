#pragma once

#include "video/VideoFrame.h"

#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QRect>

class VideoView : public QWidget {
    Q_OBJECT

public:
    explicit VideoView(const QString& label, QWidget* parent = nullptr);

    void displayFrame(const VideoFrame& frame);
    void clear();
    void setLabel(const QString& label);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateTargetRect();

    QImage currentImage_;
    QString label_;
    QSize cachedImageSize_;
    QSize cachedWidgetSize_;
    QRect cachedTargetRect_;
};
