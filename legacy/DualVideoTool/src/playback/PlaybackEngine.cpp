#include "playback/PlaybackEngine.h"

#include <algorithm>
#include <cmath>
#include <QFileInfo>
#include <QImage>
#include <QMetaType>

namespace {
constexpr int64_t kUsPerSecond = 1000000;
constexpr int kPlaybackTickMs = 8;
constexpr int64_t kPrefetchIntervalUs = 250000;
}

PlaybackEngine::PlaybackEngine(QObject* parent)
    : QObject(parent)
    , timer_(new QTimer(this)) {
    qRegisterMetaType<VideoFrame>("VideoFrame");
    qRegisterMetaType<VideoMetadata>("VideoMetadata");
    qRegisterMetaType<FramePair>("FramePair");
    qRegisterMetaType<ClipRange>("ClipRange");
    qRegisterMetaType<PlaybackState>("PlaybackState");
    qRegisterMetaType<PlaybackPerfStats>("PlaybackPerfStats");
    qRegisterMetaType<ProxyStatus>("ProxyStatus");
    qRegisterMetaType<ProxySettings>("ProxySettings");
    qRegisterMetaType<int64_t>("int64_t");
    qRegisterMetaType<uint64_t>("uint64_t");

    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &PlaybackEngine::onTick);
    setupWorkers();
}

PlaybackEngine::~PlaybackEngine() {
    if (threadA_) {
        threadA_->quit();
        threadA_->wait();
    }
    if (threadB_) {
        threadB_->quit();
        threadB_->wait();
    }
    if (proxyThread_) {
        proxyThread_->quit();
        proxyThread_->wait();
    }
}

void PlaybackEngine::setupWorkers() {
    threadA_ = new QThread(this);
    workerA_ = new VideoDecodeWorker();
    workerA_->moveToThread(threadA_);
    connect(threadA_, &QThread::finished, workerA_, &QObject::deleteLater);
    threadA_->start();

    threadB_ = new QThread(this);
    workerB_ = new VideoDecodeWorker();
    workerB_->moveToThread(threadB_);
    connect(threadB_, &QThread::finished, workerB_, &QObject::deleteLater);
    threadB_->start();

    proxyThread_ = new QThread(this);
    proxyWorker_ = new VideoDecodeWorker();
    proxyWorker_->moveToThread(proxyThread_);
    connect(proxyThread_, &QThread::finished, proxyWorker_, &QObject::deleteLater);
    proxyThread_->start();

    proxyBuilder_ = new ProxyBuilder(this);

    connect(workerA_, &VideoDecodeWorker::metadataReady, this, [this](VideoMetadata metadata) {
        metaA_ = metadata;
        emit metadataReadyA(metadata);
    });
    connect(workerB_, &VideoDecodeWorker::metadataReady, this, [this](VideoMetadata metadata) {
        metaB_ = metadata;
        emit metadataReadyB(metadata);
    });

    connect(workerA_, &VideoDecodeWorker::openFinished, this, [this](bool success) {
        readyA_ = success;
        if (!success) {
            setState(PlaybackState::Stopped);
        } else if (sourcesReady()) {
            positionUs_ = 0;
            if (proxySettings_.autoGenerate) {
                buildProxy(false);
            } else {
                setProxyStatus(ProxyStatus::Idle, "原始视频已加载，请生成代理后播放。");
                setState(PlaybackState::Stopped);
            }
        }
        emit openFinishedA(success);
    });
    connect(workerB_, &VideoDecodeWorker::openFinished, this, [this](bool success) {
        readyB_ = success;
        if (!success) {
            setState(PlaybackState::Stopped);
        } else if (sourcesReady()) {
            positionUs_ = 0;
            if (proxySettings_.autoGenerate) {
                buildProxy(false);
            } else {
                setProxyStatus(ProxyStatus::Idle, "原始视频已加载，请生成代理后播放。");
                setState(PlaybackState::Stopped);
            }
        }
        emit openFinishedB(success);
    });

    connect(workerA_, &VideoDecodeWorker::errorOccurred, this, [this](const QString& message) {
        emit errorOccurred("视频 A 错误: " + message);
    });
    connect(workerB_, &VideoDecodeWorker::errorOccurred, this, [this](const QString& message) {
        emit errorOccurred("视频 B 错误: " + message);
    });

    connect(workerA_, &VideoDecodeWorker::frameReady, this, &PlaybackEngine::onFrameA);
    connect(workerB_, &VideoDecodeWorker::frameReady, this, &PlaybackEngine::onFrameB);
    connect(proxyWorker_, &VideoDecodeWorker::metadataReady, this, [this](VideoMetadata metadata) {
        proxyMeta_ = metadata;
    });
    connect(proxyWorker_, &VideoDecodeWorker::openFinished, this, [this](bool success) {
        proxyOpening_ = false;
        proxyReady_ = success;
        if (!success) {
            setProxyStatus(ProxyStatus::ProxyFailed, "代理视频打开失败。请重建代理或检查缓存目录。");
            setState(PlaybackState::ProxyFailed);
            emit proxyFailed("代理视频打开失败。请重建代理或检查缓存目录。");
            return;
        }

        positionUs_ = 0;
        setProxyStatus(ProxyStatus::ProxyReady, "代理已就绪，可正常播放。");
        setState(PlaybackState::Paused);
        emit proxyReady(proxyPath_);
        emit positionChanged(positionUs_);
        requestFramePairAt(0);
    });
    connect(proxyWorker_, &VideoDecodeWorker::errorOccurred, this, [this](const QString& message) {
        emit errorOccurred("代理视频错误: " + message);
    });
    connect(proxyWorker_, &VideoDecodeWorker::frameReady, this, &PlaybackEngine::onProxyFrame);

    connect(proxyBuilder_, &ProxyBuilder::started, this, [this](const QString& commandLine) {
        emit proxyLogMessage("开始生成代理: " + commandLine);
    });
    connect(proxyBuilder_, &ProxyBuilder::logMessage, this, &PlaybackEngine::proxyLogMessage);
    connect(proxyBuilder_, &ProxyBuilder::finished, this, [this](const QString& outputPath) {
        emit proxyLogMessage("代理生成完成: " + outputPath);
        openProxy(outputPath);
    });
    connect(proxyBuilder_, &ProxyBuilder::failed, this, [this](const QString& message) {
        proxyReady_ = false;
        proxyOpening_ = false;
        setProxyStatus(ProxyStatus::ProxyFailed, message);
        setState(PlaybackState::ProxyFailed);
        emit proxyFailed(message);
        emit errorOccurred(message);
    });
}

void PlaybackEngine::openVideoA(const QString& path) {
    pause();
    readyA_ = false;
    pathA_ = path;
    metaA_ = {};
    resetProxyState();
    clipRange_ = {};
    emit clipChanged(clipRange_);
    invalidateRequests();
    setState(PlaybackState::Opening);
    workerA_->openVideo(path);
}

void PlaybackEngine::openVideoB(const QString& path) {
    pause();
    readyB_ = false;
    pathB_ = path;
    metaB_ = {};
    resetProxyState();
    clipRange_ = {};
    emit clipChanged(clipRange_);
    invalidateRequests();
    setState(PlaybackState::Opening);
    workerB_->openVideo(path);
}

void PlaybackEngine::buildProxy(bool forceRebuild) {
    if (!sourcesReady()) {
        setProxyStatus(ProxyStatus::ProxyFailed, "请先打开视频 A 和视频 B。");
        setState(PlaybackState::ProxyFailed);
        return;
    }

    pause();
    proxyReady_ = false;
    proxyOpening_ = false;
    proxyMeta_ = {};
    invalidateRequests();

    proxyPath_ = ProxyBuilder::proxyPathFor(pathA_, pathB_, metaA_, metaB_, proxySettings_);
    QFileInfo cached(proxyPath_);
    if (!forceRebuild && cached.exists() && cached.size() > 0) {
        setProxyStatus(ProxyStatus::PreparingProxy, "正在打开已缓存代理...");
        setState(PlaybackState::PreparingProxy);
        emit proxyLogMessage("使用已缓存代理: " + proxyPath_);
        openProxy(proxyPath_);
        return;
    }

    setProxyStatus(ProxyStatus::PreparingProxy, "正在生成播放代理...");
    setState(PlaybackState::PreparingProxy);
    proxyBuilder_->build(pathA_, pathB_, metaA_, metaB_, proxyPath_, proxySettings_);
}

void PlaybackEngine::play() {
    if (!isReady() || state_ == PlaybackState::Playing) return;
    invalidateRequests();
    startPositionUs_ = positionUs_;
    clock_.restart();
    if (perfDiagnosticsEnabled_) {
        perfStats_ = {};
        perfStats_.currentFrame = usToFrameA(positionUs_);
        perfStats_.targetFrame = usToProxyFrame(positionUs_);
        perfStats_.displayedFrame = displayedProxyFrame_;
        lastTargetProxyFrame_ = -1;
        resetPerfWindow();
        lastTickTimer_.restart();
    }
    timer_->start(kPlaybackTickMs);
    setState(PlaybackState::Playing);
    emit playingChanged(true);
    requestFramePairAt(positionUs_);
}

void PlaybackEngine::pause() {
    bool wasPlaying = state_ == PlaybackState::Playing;
    timer_->stop();
    invalidateRequests();
    if (isReady()) {
        setState(PlaybackState::Paused);
    } else if (state_ != PlaybackState::Opening) {
        setState(PlaybackState::Stopped);
    }
    if (wasPlaying) {
        emit playingChanged(false);
    }
}

void PlaybackEngine::togglePlay() {
    if (state_ == PlaybackState::Playing) {
        pause();
    } else {
        play();
    }
}

void PlaybackEngine::seekTo(int64_t positionUs) {
    if (!isReady()) return;
    bool wasPlaying = state_ == PlaybackState::Playing;
    timer_->stop();
    positionUs_ = clampPosition(positionUs);
    emit positionChanged(positionUs_);
    invalidateRequests();
    if (perfDiagnosticsEnabled_) {
        seekLatencyPending_ = true;
        seekLatencyTimer_.restart();
    }
    setState(PlaybackState::Seeking);
    requestFramePairAt(positionUs_);
    if (wasPlaying) {
        startPositionUs_ = positionUs_;
        clock_.restart();
        timer_->start(kPlaybackTickMs);
        setState(PlaybackState::Playing);
        emit playingChanged(true);
    }
}

void PlaybackEngine::previewAt(int64_t positionUs) {
    if (!isReady()) return;
    bool wasPlaying = state_ == PlaybackState::Playing;
    timer_->stop();
    positionUs_ = clampPosition(positionUs);
    emit positionChanged(positionUs_);
    invalidateRequests();
    if (perfDiagnosticsEnabled_) {
        seekLatencyPending_ = true;
        seekLatencyTimer_.restart();
    }
    setState(PlaybackState::Seeking);
    if (wasPlaying) {
        emit playingChanged(false);
    }
    requestFramePairAt(positionUs_);
}

void PlaybackEngine::stepFrame(int direction) {
    if (!isReady()) return;
    int64_t frame = usToFrameA(positionUs_);
    int64_t nextFrame = std::clamp(frame + direction, static_cast<int64_t>(0), frameCount() - 1);
    seekTo(frameToUs(nextFrame));
}

void PlaybackEngine::setSpeed(double speed) {
    speed_ = std::max(0.05, speed);
    if (state_ == PlaybackState::Playing) {
        startPositionUs_ = positionUs_;
        clock_.restart();
    }
    emit speedChanged(speed_);
}

void PlaybackEngine::setClipIn(int64_t positionUs) {
    clipRange_.inUs = clampPosition(positionUs);
    if (clipRange_.outUs && *clipRange_.outUs < *clipRange_.inUs) {
        clipRange_.outUs = clipRange_.inUs;
    }
    emit clipChanged(clipRange_);
}

void PlaybackEngine::setClipOut(int64_t positionUs) {
    clipRange_.outUs = clampPosition(positionUs);
    if (clipRange_.inUs && *clipRange_.inUs > *clipRange_.outUs) {
        clipRange_.inUs = clipRange_.outUs;
    }
    emit clipChanged(clipRange_);
}

void PlaybackEngine::clearClipIn() {
    clipRange_.inUs = std::nullopt;
    emit clipChanged(clipRange_);
}

void PlaybackEngine::clearClipOut() {
    clipRange_.outUs = std::nullopt;
    emit clipChanged(clipRange_);
}

bool PlaybackEngine::isReady() const {
    return sourcesReady() && proxyReady_ && proxyMeta_.isValid();
}

bool PlaybackEngine::sourcesReady() const {
    return readyA_ && readyB_ && metaA_.isValid() && metaB_.isValid();
}

bool PlaybackEngine::isPlaying() const {
    return state_ == PlaybackState::Playing;
}

PlaybackState PlaybackEngine::state() const {
    return state_;
}

ProxyStatus PlaybackEngine::proxyStatus() const {
    return proxyStatus_;
}

ProxySettings PlaybackEngine::proxySettings() const {
    return proxySettings_;
}

void PlaybackEngine::setProxySettings(const ProxySettings& settings) {
    proxySettings_ = settings;
}

int64_t PlaybackEngine::positionUs() const {
    return positionUs_;
}

int64_t PlaybackEngine::durationUs() const {
    if (!sourcesReady()) return 0;
    return std::min(metadataDurationUs(metaA_), metadataDurationUs(metaB_));
}

double PlaybackEngine::fps() const {
    return metaA_.fps > 0.0 ? metaA_.fps : 30.0;
}

int64_t PlaybackEngine::frameCount() const {
    return metaA_.frameCount > 0 ? metaA_.frameCount : 0;
}

uint64_t PlaybackEngine::generationId() const {
    return generationId_;
}

PlaybackPerfStats PlaybackEngine::perfStats() const {
    return perfStats_;
}

void PlaybackEngine::setPerfDiagnosticsEnabled(bool enabled) {
    perfDiagnosticsEnabled_ = enabled;
    perfStats_ = {};
    if (enabled) {
        resetPerfWindow();
    }
}

const VideoMetadata& PlaybackEngine::metadataA() const {
    return metaA_;
}

const VideoMetadata& PlaybackEngine::metadataB() const {
    return metaB_;
}

const QString& PlaybackEngine::videoPathA() const {
    return pathA_;
}

const QString& PlaybackEngine::videoPathB() const {
    return pathB_;
}

const QString& PlaybackEngine::proxyPath() const {
    return proxyPath_;
}

ClipRange PlaybackEngine::clipRange() const {
    return clipRange_;
}

int64_t PlaybackEngine::frameToUs(int64_t frameIndex) const {
    double currentFps = fps();
    if (currentFps <= 0.0) return 0;
    return static_cast<int64_t>(std::llround((frameIndex / currentFps) * kUsPerSecond));
}

int64_t PlaybackEngine::usToFrameA(int64_t positionUs) const {
    double currentFps = metaA_.fps > 0.0 ? metaA_.fps : fps();
    int64_t frame = static_cast<int64_t>(std::llround((positionUs / static_cast<double>(kUsPerSecond)) * currentFps));
    int64_t maxFrame = std::max<int64_t>(0, metaA_.frameCount - 1);
    return std::clamp(frame, static_cast<int64_t>(0), maxFrame);
}

int64_t PlaybackEngine::usToFrameB(int64_t positionUs) const {
    double currentFps = metaB_.fps > 0.0 ? metaB_.fps : fps();
    int64_t frame = static_cast<int64_t>(std::llround((positionUs / static_cast<double>(kUsPerSecond)) * currentFps));
    int64_t maxFrame = std::max<int64_t>(0, metaB_.frameCount - 1);
    return std::clamp(frame, static_cast<int64_t>(0), maxFrame);
}

void PlaybackEngine::setState(PlaybackState state) {
    if (state_ == state) return;
    state_ = state;
    emit stateChanged(state_);
}

void PlaybackEngine::setProxyStatus(ProxyStatus status, const QString& message) {
    proxyStatus_ = status;
    emit proxyStatusChanged(status, message);
}

void PlaybackEngine::resetProxyState() {
    if (proxyBuilder_) {
        proxyBuilder_->cancel();
    }
    proxyReady_ = false;
    proxyOpening_ = false;
    proxyPath_.clear();
    proxyMeta_ = {};
    setProxyStatus(ProxyStatus::Idle, "代理未生成。");
}

void PlaybackEngine::openProxy(const QString& path) {
    proxyOpening_ = true;
    proxyReady_ = false;
    proxyPath_ = path;
    proxyMeta_ = {};
    proxyWorker_->openVideo(path);
}

void PlaybackEngine::invalidateRequests() {
    generationId_++;
    pendingA_.reset();
    pendingB_.reset();
    pendingFrameA_ = -1;
    pendingFrameB_ = -1;
    pendingProxyFrame_ = -1;
    requestedProxyFrame_ = -1;
    waitingForPair_ = false;
    if (workerA_) workerA_->cancelAll(generationId_);
    if (workerB_) workerB_->cancelAll(generationId_);
    if (proxyWorker_) proxyWorker_->cancelAll(generationId_);
}

void PlaybackEngine::requestFramePairAt(int64_t positionUs) {
    if (!isReady()) return;
    pendingPositionUs_ = clampPosition(positionUs);
    pendingGeneration_ = generationId_;
    pendingFrameA_ = usToFrameA(pendingPositionUs_);
    pendingFrameB_ = usToFrameB(pendingPositionUs_);
    pendingProxyFrame_ = usToProxyFrame(pendingPositionUs_);
    if (requestedProxyFrame_ == pendingProxyFrame_ && pendingGeneration_ == generationId_) {
        return;
    }
    pendingA_.reset();
    pendingB_.reset();
    waitingForPair_ = true;
    requestedProxyFrame_ = pendingProxyFrame_;
    if (perfDiagnosticsEnabled_) {
        frameRequestTimer_.restart();
    }
    proxyWorker_->requestFrame(pendingProxyFrame_, generationId_);
}

void PlaybackEngine::requestPrefetchAt(int64_t positionUs) {
    if (!isReady()) return;
    if (lastPrefetchPositionUs_ >= 0 && std::llabs(positionUs - lastPrefetchPositionUs_) < kPrefetchIntervalUs) {
        return;
    }
    lastPrefetchPositionUs_ = positionUs;
    proxyWorker_->prefetchAround(usToProxyFrame(positionUs), 12, 48, generationId_);
}

void PlaybackEngine::onTick() {
    if (state_ != PlaybackState::Playing || !isReady()) return;

    recordTickMetric();
    int64_t elapsedUs = static_cast<int64_t>(clock_.elapsed() * 1000.0 * speed_);
    int64_t nextPosition = clampPosition(startPositionUs_ + elapsedUs);
    int64_t nextProxyFrame = usToProxyFrame(nextPosition);
    int64_t nextDisplayFrame = usToFrameA(nextPosition);
    recordTargetFrame(nextProxyFrame, nextDisplayFrame);
    if (nextPosition >= durationUs()) {
        positionUs_ = durationUs();
        pause();
        emit positionChanged(positionUs_);
        requestFramePairAt(positionUs_);
        return;
    }

    if (nextProxyFrame == requestedProxyFrame_ || nextProxyFrame == displayedProxyFrame_) {
        emitPerfStatsIfDue();
        return;
    }

    if (std::llabs(nextPosition - positionUs_) < static_cast<int64_t>(kUsPerSecond / std::max(1.0, fps()))) {
        emitPerfStatsIfDue();
        return;
    }

    positionUs_ = nextPosition;
    emit positionChanged(positionUs_);
    requestFramePairAt(positionUs_);
    requestPrefetchAt(positionUs_);
    emitPerfStatsIfDue();
}

void PlaybackEngine::onFrameA(int64_t frameIndex, const VideoFrame& frame, uint64_t generationId) {
    if (generationId != pendingGeneration_ || generationId != generationId_) return;
    if (frameIndex != pendingFrameA_) return;
    pendingA_ = frame;
    maybeEmitFramePair();
}

void PlaybackEngine::onFrameB(int64_t frameIndex, const VideoFrame& frame, uint64_t generationId) {
    if (generationId != pendingGeneration_ || generationId != generationId_) return;
    if (frameIndex != pendingFrameB_) return;
    pendingB_ = frame;
    maybeEmitFramePair();
}

void PlaybackEngine::maybeEmitFramePair() {
    if (!waitingForPair_ || !pendingA_ || !pendingB_) return;

    FramePair pair;
    pair.left = *pendingA_;
    pair.right = *pendingB_;
    pair.positionUs = pendingPositionUs_;
    pair.frameIndexA = pendingFrameA_;
    pair.frameIndexB = pendingFrameB_;
    emit framePairReady(pair);
    recordFrameDelivered(pendingProxyFrame_, pendingFrameA_);

    waitingForPair_ = false;
    requestedProxyFrame_ = -1;
    if (state_ == PlaybackState::Seeking) {
        setState(PlaybackState::Paused);
    }
}

void PlaybackEngine::onProxyFrame(int64_t frameIndex, const VideoFrame& frame, uint64_t generationId) {
    if (generationId != pendingGeneration_ || generationId != generationId_) return;
    if (frameIndex != pendingProxyFrame_) return;
    if (!waitingForPair_ || !frame.isValid()) return;

    int halfWidth = frame.image.width() / 2;
    if (halfWidth <= 0) return;

    FramePair pair;
    pair.left.image = frame.image.copy(0, 0, halfWidth, frame.image.height());
    pair.right.image = frame.image.copy(halfWidth, 0, frame.image.width() - halfWidth, frame.image.height());
    pair.left.pts = frame.pts;
    pair.right.pts = frame.pts;
    pair.left.frameIndex = pendingFrameA_;
    pair.right.frameIndex = pendingFrameB_;
    pair.positionUs = pendingPositionUs_;
    pair.frameIndexA = pendingFrameA_;
    pair.frameIndexB = pendingFrameB_;
    pair.exactLeft = true;
    pair.exactRight = true;
    emit framePairReady(pair);
    recordFrameDelivered(frameIndex, pendingFrameA_);

    waitingForPair_ = false;
    requestedProxyFrame_ = -1;
    if (state_ == PlaybackState::Seeking) {
        setState(PlaybackState::Paused);
    }
}

void PlaybackEngine::resetPerfWindow() {
    perfWindowDisplayed_ = 0;
    perfWindowTickCount_ = 0;
    perfWindowFrameLatencyCount_ = 0;
    perfWindowTickTotalMs_ = 0.0;
    perfWindowTickMaxMs_ = 0.0;
    perfWindowFrameLatencyTotalMs_ = 0.0;
    perfWindowFrameLatencyMaxMs_ = 0.0;
    perfWindowTimer_.restart();
}

void PlaybackEngine::recordTickMetric() {
    if (!perfDiagnosticsEnabled_) return;
    if (lastTickTimer_.isValid()) {
        double intervalMs = static_cast<double>(lastTickTimer_.elapsed());
        perfWindowTickTotalMs_ += intervalMs;
        perfWindowTickMaxMs_ = std::max(perfWindowTickMaxMs_, intervalMs);
        ++perfWindowTickCount_;
    }
    lastTickTimer_.restart();
}

void PlaybackEngine::recordTargetFrame(int64_t proxyFrame, int64_t currentFrame) {
    if (!perfDiagnosticsEnabled_) return;
    perfStats_.currentFrame = currentFrame;
    perfStats_.targetFrame = proxyFrame;
    if (lastTargetProxyFrame_ >= 0) {
        if (proxyFrame == lastTargetProxyFrame_) {
            ++perfStats_.repeatedTargetCount;
        } else if (proxyFrame > lastTargetProxyFrame_ + 1) {
            perfStats_.droppedTargetCount += proxyFrame - lastTargetProxyFrame_ - 1;
        }
    }
    lastTargetProxyFrame_ = proxyFrame;
}

void PlaybackEngine::recordFrameDelivered(int64_t proxyFrame, int64_t displayFrame) {
    displayedProxyFrame_ = proxyFrame;
    if (!perfDiagnosticsEnabled_) return;
    perfStats_.displayedFrame = displayFrame;
    ++perfStats_.displayedFrameCount;
    ++perfWindowDisplayed_;
    if (frameRequestTimer_.isValid()) {
        double latencyMs = static_cast<double>(frameRequestTimer_.elapsed());
        perfWindowFrameLatencyTotalMs_ += latencyMs;
        perfWindowFrameLatencyMaxMs_ = std::max(perfWindowFrameLatencyMaxMs_, latencyMs);
        ++perfWindowFrameLatencyCount_;
    }
    if (seekLatencyPending_ && seekLatencyTimer_.isValid()) {
        perfStats_.seekLatencyMs = static_cast<double>(seekLatencyTimer_.elapsed());
        seekLatencyPending_ = false;
    }
    emitPerfStatsIfDue();
}

void PlaybackEngine::emitPerfStatsIfDue() {
    if (!perfDiagnosticsEnabled_) return;
    if (!perfWindowTimer_.isValid()) {
        resetPerfWindow();
        return;
    }
    qint64 elapsedMs = perfWindowTimer_.elapsed();
    if (elapsedMs < 1000) return;

    perfStats_.displayedFps = elapsedMs > 0
        ? (perfWindowDisplayed_ * 1000.0) / static_cast<double>(elapsedMs)
        : 0.0;
    perfStats_.averageTickIntervalMs = perfWindowTickCount_ > 0
        ? perfWindowTickTotalMs_ / static_cast<double>(perfWindowTickCount_)
        : 0.0;
    perfStats_.maxTickIntervalMs = perfWindowTickMaxMs_;
    perfStats_.averageFrameLatencyMs = perfWindowFrameLatencyCount_ > 0
        ? perfWindowFrameLatencyTotalMs_ / static_cast<double>(perfWindowFrameLatencyCount_)
        : 0.0;
    perfStats_.maxFrameLatencyMs = perfWindowFrameLatencyMaxMs_;
    emit perfStatsUpdated(perfStats_);
    resetPerfWindow();
}

int64_t PlaybackEngine::clampPosition(int64_t positionUs) const {
    int64_t dur = durationUs();
    if (dur <= 0) return 0;
    return std::clamp(positionUs, static_cast<int64_t>(0), dur);
}

int64_t PlaybackEngine::metadataDurationUs(const VideoMetadata& metadata) const {
    if (metadata.duration > 0.0) {
        return static_cast<int64_t>(std::llround(metadata.duration * kUsPerSecond));
    }
    if (metadata.fps > 0.0 && metadata.frameCount > 0) {
        return static_cast<int64_t>(std::llround((metadata.frameCount / metadata.fps) * kUsPerSecond));
    }
    return 0;
}

int64_t PlaybackEngine::usToProxyFrame(int64_t positionUs) const {
    double currentFps = proxyMeta_.fps > 0.0 ? proxyMeta_.fps : fps();
    int64_t frame = static_cast<int64_t>(std::llround((positionUs / static_cast<double>(kUsPerSecond)) * currentFps));
    int64_t maxFrame = std::max<int64_t>(0, proxyMeta_.frameCount - 1);
    return std::clamp(frame, static_cast<int64_t>(0), maxFrame);
}
