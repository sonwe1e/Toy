#include "video/VideoDecodeWorker.h"

VideoDecodeWorker::VideoDecodeWorker(QObject* parent)
    : QObject(parent) {
}

VideoDecodeWorker::~VideoDecodeWorker() = default;

void VideoDecodeWorker::openVideo(const QString& path) {
    QMetaObject::invokeMethod(this, "doOpen", Qt::QueuedConnection,
                              Q_ARG(QString, path));
}

void VideoDecodeWorker::requestFrame(int64_t frameIndex, uint64_t generationId) {
    currentGeneration_.store(generationId);
    latestRequestGeneration_.store(generationId);
    latestRequestFrame_.store(frameIndex);
    cancelFlag_.store(false);
    QMetaObject::invokeMethod(this, "doRequestFrame", Qt::QueuedConnection,
                              Q_ARG(int64_t, frameIndex),
                              Q_ARG(uint64_t, generationId));
}

void VideoDecodeWorker::prefetchAround(int64_t centerFrame, int backwardRadius, int forwardRadius, uint64_t generationId) {
    if (generationId != currentGeneration_.load()) return;
    cancelFlag_.store(false);
    QMetaObject::invokeMethod(this, "doPrefetch", Qt::QueuedConnection,
                              Q_ARG(int64_t, centerFrame),
                              Q_ARG(int, backwardRadius),
                              Q_ARG(int, forwardRadius),
                              Q_ARG(uint64_t, generationId));
}

void VideoDecodeWorker::cancelAll(uint64_t generationId) {
    currentGeneration_.store(generationId);
    latestRequestGeneration_.store(generationId);
    latestRequestFrame_.store(-1);
    cancelFlag_.store(true);
}

void VideoDecodeWorker::doOpen(const QString& path) {
    ensureDecoder();
    cancelFlag_.store(true);
    latestRequestFrame_.store(-1);
    if (decoder_->isOpen()) {
        decoder_->close();
    }

    bool ok = decoder_->open(path);
    if (ok) {
        emit metadataReady(decoder_->metadata());
    }
    emit openFinished(ok);
}

void VideoDecodeWorker::doRequestFrame(int64_t frameIndex, uint64_t generationId) {
    ensureDecoder();
    if (!decoder_->isOpen()) return;

    if (generationId != currentGeneration_.load()) return;
    if (generationId != latestRequestGeneration_.load()) return;
    if (frameIndex != latestRequestFrame_.load()) return;

    auto frame = decoder_->decodeFrame(frameIndex);
    if (generationId != currentGeneration_.load()) return;
    if (generationId != latestRequestGeneration_.load()) return;
    if (frameIndex != latestRequestFrame_.load()) return;
    if (frame.has_value()) {
        emit frameReady(frameIndex, *frame, generationId);
    }
}

void VideoDecodeWorker::doPrefetch(int64_t centerFrame, int backwardRadius, int forwardRadius, uint64_t generationId) {
    ensureDecoder();
    if (!decoder_->isOpen()) return;
    if (generationId != currentGeneration_.load()) return;

    // Prefetch forward frames
    for (int64_t i = centerFrame + 1; i <= centerFrame + forwardRadius; ++i) {
        if (cancelFlag_.load()) return;
        if (generationId != currentGeneration_.load()) return;
        decoder_->decodeFrame(i);
    }

    // Prefetch backward frames
    for (int64_t i = centerFrame - 1; i >= centerFrame - backwardRadius; --i) {
        if (cancelFlag_.load()) return;
        if (generationId != currentGeneration_.load()) return;
        if (i < 0) break;
        decoder_->decodeFrame(i);
    }
}

void VideoDecodeWorker::ensureDecoder() {
    if (decoder_) return;

    decoder_ = std::make_unique<VideoDecoder>();
    connect(decoder_.get(), &VideoDecoder::error, this, &VideoDecodeWorker::errorOccurred);
}
