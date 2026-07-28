#pragma once

#include "video/VideoMetadata.h"

#include <QObject>
#include <QString>
#include <QImage>
#include <cstdint>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

class VideoEncoder : public QObject {
    Q_OBJECT

public:
    explicit VideoEncoder(QObject* parent = nullptr);
    ~VideoEncoder();

    bool open(const QString& path, int width, int height, double fps);
    bool writeFrame(const QImage& frame);
    void close();
    bool isOpen() const;

signals:
    void error(const QString& message);

private:
    bool drainPackets(bool flushing);

    AVFormatContext* fmtCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    AVStream* stream_ = nullptr;
    int64_t frameIndex_ = 0;
    bool open_ = false;
};
