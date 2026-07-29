#pragma once

#include "video/VideoMetadata.h"
#include "video/VideoFrame.h"
#include "video/FrameCache.h"

#include <QObject>
#include <QString>
#include <optional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

class VideoDecoder : public QObject {
    Q_OBJECT

public:
    explicit VideoDecoder(QObject* parent = nullptr);
    ~VideoDecoder();

    bool open(const QString& path);
    void close();
    bool isOpen() const;

    VideoMetadata metadata() const;

    // Decode frame at index. Returns nullopt on failure.
    // Uses cache for nearby frames, seeks for distant frames.
    std::optional<VideoFrame> decodeFrame(int64_t frameIndex);

    // Access cache for prefetch coordination
    FrameCache& cache();
    const FrameCache& cache() const;

signals:
    void error(const QString& message);

private:
    bool readMetadata();
    bool seekToFrame(int64_t frameIndex);
    bool decodeNextFrame(AVFrame* frame, AVPacket* pkt);
    VideoFrame convertFrame(AVFrame* frame, int64_t frameIndex);

    AVFormatContext* fmtCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    int videoStreamIdx_ = -1;

    VideoMetadata metadata_;
    FrameCache cache_;
    int64_t lastDecodedFrame_ = -1;
    bool open_ = false;
};
