#pragma once

#include <QImage>
#include <QMetaType>
#include <cstdint>

struct VideoFrame {
    QImage image;
    int64_t pts = 0;       // presentation timestamp in stream time_base
    int64_t frameIndex = 0;

    bool isValid() const { return !image.isNull(); }
};

Q_DECLARE_METATYPE(VideoFrame)
