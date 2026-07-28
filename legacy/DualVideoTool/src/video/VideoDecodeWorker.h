#pragma once

#include "video/VideoDecoder.h"
#include "video/VideoMetadata.h"
#include "video/VideoFrame.h"

#include <QObject>
#include <QString>
#include <QThread>
#include <atomic>
#include <memory>

// Worker that owns a VideoDecoder and runs decoding on a dedicated QThread.
// All heavy FFmpeg work happens here, never on the UI thread.
// Every request carries a generationId; stale results are discarded by the UI.
class VideoDecodeWorker : public QObject {
    Q_OBJECT

public:
    explicit VideoDecodeWorker(QObject* parent = nullptr);
    ~VideoDecodeWorker();

    void openVideo(const QString& path);
    void requestFrame(int64_t frameIndex, uint64_t generationId);
    void prefetchAround(int64_t centerFrame, int backwardRadius, int forwardRadius, uint64_t generationId);
    void cancelAll(uint64_t generationId);

signals:
    void metadataReady(VideoMetadata metadata);
    void frameReady(int64_t frameIndex, VideoFrame frame, uint64_t generationId);
    void openFinished(bool success);
    void errorOccurred(QString message);

public slots:
    void doOpen(const QString& path);
    void doRequestFrame(int64_t frameIndex, uint64_t generationId);
    void doPrefetch(int64_t centerFrame, int backwardRadius, int forwardRadius, uint64_t generationId);

private:
    void ensureDecoder();

    std::unique_ptr<VideoDecoder> decoder_;
    std::atomic<uint64_t> currentGeneration_{0};
    std::atomic<uint64_t> latestRequestGeneration_{0};
    std::atomic<int64_t> latestRequestFrame_{-1};
    std::atomic<bool> cancelFlag_{false};
};
