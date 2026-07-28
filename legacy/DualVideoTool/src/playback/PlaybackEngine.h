#pragma once

#include "video/VideoDecodeWorker.h"
#include "video/VideoFrame.h"
#include "video/VideoMetadata.h"
#include "playback/ProxyBuilder.h"

#include <QObject>
#include <QElapsedTimer>
#include <QMetaType>
#include <QThread>
#include <QTimer>
#include <QString>
#include <cstdint>
#include <optional>

enum class PlaybackState {
    Stopped,
    Opening,
    PreparingProxy,
    ProxyReady,
    ProxyFailed,
    Paused,
    Playing,
    Seeking,
    Buffering,
    Exporting
};

struct ClipRange {
    std::optional<int64_t> inUs;
    std::optional<int64_t> outUs;

    bool isComplete() const { return inUs.has_value() && outUs.has_value(); }
};

Q_DECLARE_METATYPE(ClipRange)

struct FramePair {
    VideoFrame left;
    VideoFrame right;
    int64_t positionUs = 0;
    int64_t frameIndexA = 0;
    int64_t frameIndexB = 0;
    bool exactLeft = true;
    bool exactRight = true;

    bool isValid() const { return left.isValid() && right.isValid(); }
};

Q_DECLARE_METATYPE(FramePair)
Q_DECLARE_METATYPE(PlaybackState)

struct PlaybackPerfStats {
    int64_t currentFrame = 0;
    int64_t targetFrame = 0;
    int64_t displayedFrame = 0;
    int64_t displayedFrameCount = 0;
    int64_t droppedTargetCount = 0;
    int64_t repeatedTargetCount = 0;
    double displayedFps = 0.0;
    double averageFrameLatencyMs = 0.0;
    double maxFrameLatencyMs = 0.0;
    double averageTickIntervalMs = 0.0;
    double maxTickIntervalMs = 0.0;
    double seekLatencyMs = 0.0;
};

Q_DECLARE_METATYPE(PlaybackPerfStats)

class PlaybackEngine : public QObject {
    Q_OBJECT

public:
    explicit PlaybackEngine(QObject* parent = nullptr);
    ~PlaybackEngine();

    void openVideoA(const QString& path);
    void openVideoB(const QString& path);
    void buildProxy(bool forceRebuild = false);

    void play();
    void pause();
    void togglePlay();
    void seekTo(int64_t positionUs);
    void previewAt(int64_t positionUs);
    void stepFrame(int direction);
    void setSpeed(double speed);

    void setClipIn(int64_t positionUs);
    void setClipOut(int64_t positionUs);
    void clearClipIn();
    void clearClipOut();

    bool isReady() const;
    bool sourcesReady() const;
    bool isPlaying() const;
    PlaybackState state() const;
    ProxyStatus proxyStatus() const;
    ProxySettings proxySettings() const;
    void setProxySettings(const ProxySettings& settings);
    int64_t positionUs() const;
    int64_t durationUs() const;
    double fps() const;
    int64_t frameCount() const;
    uint64_t generationId() const;
    PlaybackPerfStats perfStats() const;
    void setPerfDiagnosticsEnabled(bool enabled);

    const VideoMetadata& metadataA() const;
    const VideoMetadata& metadataB() const;
    const QString& videoPathA() const;
    const QString& videoPathB() const;
    const QString& proxyPath() const;
    ClipRange clipRange() const;

    int64_t frameToUs(int64_t frameIndex) const;
    int64_t usToFrameA(int64_t positionUs) const;
    int64_t usToFrameB(int64_t positionUs) const;

signals:
    void metadataReadyA(VideoMetadata metadata);
    void metadataReadyB(VideoMetadata metadata);
    void openFinishedA(bool success);
    void openFinishedB(bool success);
    void errorOccurred(QString message);
    void framePairReady(FramePair pair);
    void positionChanged(int64_t positionUs);
    void stateChanged(PlaybackState state);
    void playingChanged(bool playing);
    void speedChanged(double speed);
    void clipChanged(ClipRange range);
    void proxyStatusChanged(ProxyStatus status, QString message);
    void proxyLogMessage(QString message);
    void proxyReady(QString proxyPath);
    void proxyFailed(QString message);
    void perfStatsUpdated(PlaybackPerfStats stats);

private:
    void setupWorkers();
    void setState(PlaybackState state);
    void setProxyStatus(ProxyStatus status, const QString& message = {});
    void resetProxyState();
    void openProxy(const QString& path);
    void invalidateRequests();
    void requestFramePairAt(int64_t positionUs);
    void requestPrefetchAt(int64_t positionUs);
    void onTick();
    void onFrameA(int64_t frameIndex, const VideoFrame& frame, uint64_t generationId);
    void onFrameB(int64_t frameIndex, const VideoFrame& frame, uint64_t generationId);
    void onProxyFrame(int64_t frameIndex, const VideoFrame& frame, uint64_t generationId);
    void maybeEmitFramePair();
    void resetPerfWindow();
    void recordTickMetric();
    void recordTargetFrame(int64_t proxyFrame, int64_t currentFrame);
    void recordFrameDelivered(int64_t proxyFrame, int64_t displayFrame);
    void emitPerfStatsIfDue();
    int64_t clampPosition(int64_t positionUs) const;
    int64_t metadataDurationUs(const VideoMetadata& metadata) const;
    int64_t usToProxyFrame(int64_t positionUs) const;

    QThread* threadA_ = nullptr;
    QThread* threadB_ = nullptr;
    QThread* proxyThread_ = nullptr;
    VideoDecodeWorker* workerA_ = nullptr;
    VideoDecodeWorker* workerB_ = nullptr;
    VideoDecodeWorker* proxyWorker_ = nullptr;
    ProxyBuilder* proxyBuilder_ = nullptr;

    QString pathA_;
    QString pathB_;
    QString proxyPath_;
    VideoMetadata metaA_;
    VideoMetadata metaB_;
    VideoMetadata proxyMeta_;
    bool readyA_ = false;
    bool readyB_ = false;
    bool proxyReady_ = false;
    bool proxyOpening_ = false;
    ProxyStatus proxyStatus_ = ProxyStatus::Idle;
    ProxySettings proxySettings_;

    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
    int64_t startPositionUs_ = 0;
    int64_t positionUs_ = 0;
    double speed_ = 1.0;
    PlaybackState state_ = PlaybackState::Stopped;
    uint64_t generationId_ = 0;

    ClipRange clipRange_;

    std::optional<VideoFrame> pendingA_;
    std::optional<VideoFrame> pendingB_;
    int64_t pendingFrameA_ = -1;
    int64_t pendingFrameB_ = -1;
    int64_t pendingPositionUs_ = 0;
    uint64_t pendingGeneration_ = 0;
    bool waitingForPair_ = false;
    int64_t pendingProxyFrame_ = -1;
    int64_t requestedProxyFrame_ = -1;
    int64_t displayedProxyFrame_ = -1;
    int64_t lastTargetProxyFrame_ = -1;

    int64_t lastPrefetchPositionUs_ = -1;

    bool perfDiagnosticsEnabled_ = false;
    PlaybackPerfStats perfStats_;
    QElapsedTimer perfWindowTimer_;
    QElapsedTimer lastTickTimer_;
    QElapsedTimer frameRequestTimer_;
    QElapsedTimer seekLatencyTimer_;
    bool seekLatencyPending_ = false;
    int64_t perfWindowDisplayed_ = 0;
    int64_t perfWindowTickCount_ = 0;
    int64_t perfWindowFrameLatencyCount_ = 0;
    double perfWindowTickTotalMs_ = 0.0;
    double perfWindowTickMaxMs_ = 0.0;
    double perfWindowFrameLatencyTotalMs_ = 0.0;
    double perfWindowFrameLatencyMaxMs_ = 0.0;
};
