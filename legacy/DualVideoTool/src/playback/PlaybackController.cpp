#include "playback/PlaybackController.h"
#include <algorithm>

PlaybackController::PlaybackController(QObject* parent)
    : QObject(parent), timer_(new QTimer(this)) {
    timer_->setTimerType(Qt::PreciseTimer);
    timelineThrottle_.start();
    connect(timer_, &QTimer::timeout, this, &PlaybackController::onTick);
}

void PlaybackController::setFps(double fps) {
    fps_ = fps;
}

void PlaybackController::setTotalFrames(int64_t totalFrames) {
    totalFrames_ = totalFrames;
}

void PlaybackController::play() {
    if (playing_ || totalFrames_ <= 0) return;
    playing_ = true;
    generationId_++;
    startFrame_ = currentFrame_;
    clock_.start();
    int tickMs = std::max(2, static_cast<int>(500.0 / fps_));
    timer_->start(tickMs);
    emit playbackStateChanged(true);
    emit cancelRequested(generationId_);
}

void PlaybackController::pause() {
    bool wasPlaying = playing_;
    playing_ = false;
    timer_->stop();
    generationId_++;
    if (wasPlaying) {
        emit playbackStateChanged(false);
    }
    emit cancelRequested(generationId_);
}

void PlaybackController::togglePlay() {
    if (playing_) pause();
    else play();
}

bool PlaybackController::isPlaying() const {
    return playing_;
}

void PlaybackController::setSpeed(double speed) {
    if (playing_) {
        startFrame_ = currentFrame_;
        clock_.restart();
    }
    playbackSpeed_ = speed;
    emit speedChanged(speed);
}

double PlaybackController::speed() const {
    return playbackSpeed_;
}

void PlaybackController::seekToFrame(int64_t frame) {
    currentFrame_ = std::clamp(frame, static_cast<int64_t>(0),
                               totalFrames_ > 0 ? totalFrames_ - 1 : static_cast<int64_t>(0));
    generationId_++;
    if (playing_) {
        playing_ = false;
        timer_->stop();
        startFrame_ = currentFrame_;
        clock_.restart();
        emit playbackStateChanged(false);
    }
    emit cancelRequested(generationId_);
    emit frameChanged(currentFrame_, generationId_);

    if (timelineThrottle_.elapsed() >= kTimelineIntervalMs) {
        timelineThrottle_.restart();
        emit timelineUpdate(currentFrame_);
    }
}

void PlaybackController::stepFrames(int64_t delta) {
    seekToFrame(currentFrame_ + delta);
}

int64_t PlaybackController::currentFrame() const {
    return currentFrame_;
}

int64_t PlaybackController::targetFrame() const {
    if (!playing_) return currentFrame_;
    double elapsed = clock_.elapsed() / 1000.0;
    int64_t target = startFrame_ + static_cast<int64_t>(elapsed * fps_ * playbackSpeed_);
    return std::clamp(target, static_cast<int64_t>(0),
                      totalFrames_ > 0 ? totalFrames_ - 1 : static_cast<int64_t>(0));
}

uint64_t PlaybackController::generationId() const {
    return generationId_;
}

void PlaybackController::onTick() {
    if (!playing_) return;

    int64_t newFrame = targetFrame();

    if (newFrame >= totalFrames_ - 1) {
        currentFrame_ = totalFrames_ - 1;
        pause();
        emit frameChanged(currentFrame_, generationId_);
        emit timelineUpdate(currentFrame_);
        return;
    }

    if (newFrame != currentFrame_) {
        currentFrame_ = newFrame;
        emit frameChanged(currentFrame_, generationId_);

        if (timelineThrottle_.elapsed() >= kTimelineIntervalMs) {
            timelineThrottle_.restart();
            emit timelineUpdate(currentFrame_);
        }

        if (std::abs(currentFrame_ - lastPrefetchFrame_) >= 10) {
            lastPrefetchFrame_ = currentFrame_;
            emit prefetchRequested(currentFrame_, generationId_);
        }
    }
}
