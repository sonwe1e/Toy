#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <atomic>
#include <cstdint>

class PlaybackController : public QObject {
    Q_OBJECT

public:
    explicit PlaybackController(QObject* parent = nullptr);

    void setFps(double fps);
    void setTotalFrames(int64_t totalFrames);

    void play();
    void pause();
    void togglePlay();
    bool isPlaying() const;

    void setSpeed(double speed);
    double speed() const;

    void seekToFrame(int64_t frame);
    void stepFrames(int64_t delta);
    int64_t currentFrame() const;
    int64_t targetFrame() const;

    // Generation ID: incremented on every state change that invalidates old decode results.
    // Any frameReady signal carrying an old generation must be discarded.
    uint64_t generationId() const;

signals:
    void frameChanged(int64_t frameIndex, uint64_t generationId);
    void timelineUpdate(int64_t frameIndex);
    void playbackStateChanged(bool playing);
    void speedChanged(double speed);
    void prefetchRequested(int64_t centerFrame, uint64_t generationId);
    // Emitted on pause/seek to cancel stale background work
    void cancelRequested(uint64_t newGenerationId);

private:
    void onTick();

    QTimer* timer_;
    QElapsedTimer clock_;

    int64_t startFrame_ = 0;
    int64_t currentFrame_ = 0;
    int64_t totalFrames_ = 0;
    double playbackSpeed_ = 1.0;
    double fps_ = 30.0;
    bool playing_ = false;

    // Generation ID for stale request rejection
    uint64_t generationId_ = 0;

    // Throttle timeline updates to ~12 Hz
    QElapsedTimer timelineThrottle_;
    static constexpr int kTimelineIntervalMs = 80;

    // Prefetch interval
    int64_t lastPrefetchFrame_ = -10;
};
