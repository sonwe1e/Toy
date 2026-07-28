#include "app/MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QDebug>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextCursor>
#include <QVBoxLayout>
#include <cmath>

namespace {
enum ClipQueueColumn {
    QueueColumnIndex = 0,
    QueueColumnInFrame,
    QueueColumnOutFrame,
    QueueColumnDuration,
    QueueColumnName,
    QueueColumnNote,
    QueueColumnStatus,
    QueueColumnResult,
    QueueColumnCount
};

QString formatMetadataLabel(const QString& name, const VideoMetadata& metadata) {
    return QString("%1: %2x%3 | %4 fps | %5 帧 | %6")
        .arg(name)
        .arg(metadata.width)
        .arg(metadata.height)
        .arg(metadata.fps, 0, 'f', 2)
        .arg(metadata.frameCount)
        .arg(QString::fromStdString(metadata.codecName));
}

QTableWidgetItem* makeReadOnlyItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QString durationText(int64_t inFrame, int64_t outFrame, double fps) {
    int64_t frames = std::max<int64_t>(0, outFrame - inFrame + 1);
    double seconds = fps > 0.0 ? frames / fps : 0.0;
    return QString("%1 帧 / %2s").arg(frames).arg(seconds, 0, 'f', 2);
}

QString queueResultText(const ClipItem& clip) {
    if (!clip.errorMessage.isEmpty()) {
        return clip.errorMessage;
    }
    if (!clip.outputPath.isEmpty()) {
        return clip.outputPath;
    }
    if (!clip.outputPathA.isEmpty() || !clip.outputPathB.isEmpty()) {
        return QString("A: %1\nB: %2").arg(clip.outputPathA, clip.outputPathB);
    }
    return {};
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , engine_(std::make_unique<PlaybackEngine>()) {
    debugPerf_ = QCoreApplication::arguments().contains("--debug-perf");
    loadSettings();
    engine_->setProxySettings(proxySettings_);
    engine_->setPerfDiagnosticsEnabled(debugPerf_);
    timelineUiThrottle_.start();

    setupUi();
    setupMenus();
    setupConnections();
    setWindowTitle("双视频代理播放器 v1.8");
    setWindowIcon(QIcon(":/assets/app_icon.png"));
    resize(1280, 720);
    updateStatusPanel();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* metaLayout = new QHBoxLayout();
    metaLayout->setContentsMargins(8, 4, 8, 4);
    metadataLabelA_ = new QLabel("视频 A: 未加载");
    metadataLabelB_ = new QLabel("视频 B: 未加载");
    metaLayout->addWidget(metadataLabelA_);
    metaLayout->addStretch();
    metaLayout->addWidget(metadataLabelB_);
    mainLayout->addLayout(metaLayout);

    auto* videoLayout = new QHBoxLayout();
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->setSpacing(0);
    viewA_ = new VideoView("选择视频 A (Ctrl+A)");
    viewB_ = new VideoView("选择视频 B (Ctrl+B)");
    videoLayout->addWidget(viewA_, 1);
    videoLayout->addWidget(viewB_, 1);
    mainLayout->addLayout(videoLayout, 1);

    timelineWidget_ = new TimelineWidget();
    mainLayout->addWidget(timelineWidget_);

    controlBar_ = new ControlBar();
    mainLayout->addWidget(controlBar_);

    auto* clipQueueGroup = new QGroupBox("片段队列");
    auto* clipQueueLayout = new QVBoxLayout(clipQueueGroup);
    auto* clipQueueToolbar = new QHBoxLayout();
    clipQueueSummaryLabel_ = new QLabel("0 个片段");
    addClipQueueBtn_ = new QPushButton("加入队列");
    deleteClipQueueBtn_ = new QPushButton("删除");
    jumpClipQueueBtn_ = new QPushButton("跳转片段");
    clearExportedQueueBtn_ = new QPushButton("清空已导出");
    batchExportModeCombo_ = new QComboBox();
    batchExportModeCombo_->addItem(batchExportModeLabel(BatchExportMode::SideBySide),
                                   batchExportModeId(BatchExportMode::SideBySide));
    batchExportModeCombo_->addItem(batchExportModeLabel(BatchExportMode::SeparateAB),
                                   batchExportModeId(BatchExportMode::SeparateAB));
    batchExportModeCombo_->setToolTip("选择队列批量导出的输出方式");
    batchExportQueueBtn_ = new QPushButton("批量导出");
    clipQueueToolbar->addWidget(clipQueueSummaryLabel_);
    clipQueueToolbar->addStretch();
    clipQueueToolbar->addWidget(addClipQueueBtn_);
    clipQueueToolbar->addWidget(jumpClipQueueBtn_);
    clipQueueToolbar->addWidget(deleteClipQueueBtn_);
    clipQueueToolbar->addWidget(clearExportedQueueBtn_);
    clipQueueToolbar->addWidget(batchExportModeCombo_);
    clipQueueToolbar->addWidget(batchExportQueueBtn_);
    clipQueueLayout->addLayout(clipQueueToolbar);

    clipQueueTable_ = new QTableWidget(0, QueueColumnCount, this);
    clipQueueTable_->setHorizontalHeaderLabels({
        "#", "入点帧", "出点帧", "时长", "名称", "备注", "状态", "导出结果"
    });
    clipQueueTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    clipQueueTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    clipQueueTable_->setAlternatingRowColors(true);
    clipQueueTable_->verticalHeader()->setVisible(false);
    clipQueueTable_->horizontalHeader()->setStretchLastSection(true);
    clipQueueTable_->horizontalHeader()->setSectionResizeMode(QueueColumnIndex, QHeaderView::ResizeToContents);
    clipQueueTable_->horizontalHeader()->setSectionResizeMode(QueueColumnInFrame, QHeaderView::ResizeToContents);
    clipQueueTable_->horizontalHeader()->setSectionResizeMode(QueueColumnOutFrame, QHeaderView::ResizeToContents);
    clipQueueTable_->horizontalHeader()->setSectionResizeMode(QueueColumnDuration, QHeaderView::ResizeToContents);
    clipQueueTable_->horizontalHeader()->setSectionResizeMode(QueueColumnName, QHeaderView::Interactive);
    clipQueueTable_->horizontalHeader()->setSectionResizeMode(QueueColumnNote, QHeaderView::Stretch);
    clipQueueTable_->horizontalHeader()->setSectionResizeMode(QueueColumnStatus, QHeaderView::ResizeToContents);
    clipQueueTable_->setColumnWidth(QueueColumnName, 120);
    clipQueueTable_->setMaximumHeight(180);
    clipQueueLayout->addWidget(clipQueueTable_);
    mainLayout->addWidget(clipQueueGroup);

    auto* statusGroup = new QGroupBox("状态 / 日志");
    auto* statusLayout = new QVBoxLayout(statusGroup);
    auto* statusRows = new QHBoxLayout();
    modeLabel_ = new QLabel("模式: 代理播放");
    proxyStatusLabel_ = new QLabel("代理: 未生成");
    timeStatusLabel_ = new QLabel("时间: 00:00.000 / 00:00.000");
    clipStatusLabel_ = new QLabel("片段: 未标记");
    statusRows->addWidget(modeLabel_);
    statusRows->addSpacing(16);
    statusRows->addWidget(proxyStatusLabel_);
    statusRows->addSpacing(16);
    statusRows->addWidget(timeStatusLabel_);
    statusRows->addStretch();
    statusRows->addWidget(clipStatusLabel_);
    statusLayout->addLayout(statusRows);
    logPanel_ = new QPlainTextEdit();
    logPanel_->setReadOnly(true);
    logPanel_->setMaximumBlockCount(400);
    logPanel_->setMaximumHeight(96);
    logPanel_->setPlaceholderText("代理生成和导出日志会显示在这里");
    statusLayout->addWidget(logPanel_);
    mainLayout->addWidget(statusGroup);

    statusBar()->showMessage("就绪 — 请先选择两个视频文件");
    shortcuts_ = std::make_unique<ShortcutManager>(this, this);
    controlBar_->setProxyReady(false);
    controlBar_->setProxyBusy(false);
    refreshClipQueueTable();
    updateClipQueueControls();
}

void MainWindow::setupMenus() {
    auto* fileMenu = menuBar()->addMenu("文件(&F)");
    fileMenu->addAction("打开视频 A(&A)...", QKeySequence("Ctrl+A"), this, &MainWindow::openVideoA);
    fileMenu->addAction("打开视频 B(&B)...", QKeySequence("Ctrl+B"), this, &MainWindow::openVideoB);
    fileMenu->addAction("生成/重建代理(&R)", QKeySequence("Ctrl+R"), this, &MainWindow::onBuildProxy);
    fileMenu->addAction("设置(&S)...", this, &MainWindow::showSettings);
    fileMenu->addSeparator();
    fileMenu->addAction("退出(&Q)", QKeySequence::Quit, this, &QWidget::close);

    auto* playbackMenu = menuBar()->addMenu("播放(&P)");
    playbackMenu->addAction("播放/暂停", QKeySequence("Space"), engine_.get(), &PlaybackEngine::togglePlay);
    playbackMenu->addSeparator();
    playbackMenu->addAction("上一帧", QKeySequence("Left"), [this]() { engine_->stepFrame(-1); });
    playbackMenu->addAction("下一帧", QKeySequence("Right"), [this]() { engine_->stepFrame(1); });
    playbackMenu->addAction("前进 10 帧", QKeySequence("Up"), [this]() { engine_->stepFrame(10); });
    playbackMenu->addAction("后退 10 帧", QKeySequence("Down"), [this]() { engine_->stepFrame(-10); });
    playbackMenu->addAction("首帧", QKeySequence("Home"), [this]() { engine_->seekTo(0); });
    playbackMenu->addAction("末帧", QKeySequence("End"), [this]() { engine_->seekTo(engine_->durationUs()); });

    auto* clipMenu = menuBar()->addMenu("片段(&C)");
    clipMenu->addAction("标记入点", QKeySequence("I"), [this]() {
        if (playbackEnabled_) engine_->setClipIn(engine_->positionUs());
    });
    clipMenu->addAction("标记出点", QKeySequence("O"), [this]() {
        if (playbackEnabled_) engine_->setClipOut(engine_->positionUs());
    });
    clipMenu->addAction("跳转到入点", QKeySequence("Shift+I"), [this]() {
        auto range = engine_->clipRange();
        if (range.inUs) engine_->seekTo(*range.inUs);
    });
    clipMenu->addAction("跳转到出点", QKeySequence("Shift+O"), [this]() {
        auto range = engine_->clipRange();
        if (range.outUs) engine_->seekTo(*range.outUs);
    });
    clipMenu->addAction("清除入点", QKeySequence("Alt+I"), engine_.get(), &PlaybackEngine::clearClipIn);
    clipMenu->addAction("清除出点", QKeySequence("Alt+O"), engine_.get(), &PlaybackEngine::clearClipOut);
    clipMenu->addSeparator();
    clipMenu->addAction("加入队列", this, &MainWindow::onAddClipToQueue);
    clipMenu->addAction("跳转选中片段", this, &MainWindow::onJumpToSelectedClip);
    clipMenu->addAction("批量导出队列", this, &MainWindow::onBatchExportQueue);
    clipMenu->addSeparator();
    clipMenu->addAction("导出片段", QKeySequence("E"), this, &MainWindow::onExportClip);

    auto* helpMenu = menuBar()->addMenu("帮助(&H)");
    helpMenu->addAction("使用说明", this, &MainWindow::showUsageHelp);
    helpMenu->addAction("关于", [this]() {
        QMessageBox::about(this, "关于",
            "双视频代理播放器 v1.8.0\n\n"
            "用于 VFI 视频帧插值对比的双视频观看工具。\n"
            "打开两路原始视频后生成左右拼接代理，播放时只解码代理视频。\n"
            "入点、出点和导出仍映射回原始视频 A/B。\n\n"
            "技术栈: C++20 + Qt 6 + FFmpeg");
    });
}

void MainWindow::setupConnections() {
    connect(engine_.get(), &PlaybackEngine::metadataReadyA, this, [this](VideoMetadata metadata) {
        metaA_ = metadata;
        metadataLabelA_->setText(formatMetadataLabel("视频 A", metadata));
    });
    connect(engine_.get(), &PlaybackEngine::metadataReadyB, this, [this](VideoMetadata metadata) {
        metaB_ = metadata;
        metadataLabelB_->setText(formatMetadataLabel("视频 B", metadata));
    });
    connect(engine_.get(), &PlaybackEngine::openFinishedA, this, [this](bool ok) {
        videoAReady_ = ok;
        if (ok) {
            viewA_->setLabel("视频 A 已加载");
            statusBar()->showMessage("视频 A 已加载，等待视频 B 或代理生成", 3000);
        }
        validateAndEnablePlayback();
    });
    connect(engine_.get(), &PlaybackEngine::openFinishedB, this, [this](bool ok) {
        videoBReady_ = ok;
        if (ok) {
            viewB_->setLabel("视频 B 已加载");
            statusBar()->showMessage("视频 B 已加载，等待视频 A 或代理生成", 3000);
        }
        validateAndEnablePlayback();
    });
    connect(engine_.get(), &PlaybackEngine::errorOccurred, this, [this](const QString& message) {
        statusBar()->showMessage(message, 5000);
    });
    connect(engine_.get(), &PlaybackEngine::framePairReady, this, &MainWindow::onFramePairReady);
    connect(engine_.get(), &PlaybackEngine::positionChanged, this, &MainWindow::onPositionChanged);
    connect(engine_.get(), &PlaybackEngine::stateChanged, this, &MainWindow::onEngineStateChanged);
    connect(engine_.get(), &PlaybackEngine::playingChanged, controlBar_, &ControlBar::setPlaying);
    connect(engine_.get(), &PlaybackEngine::speedChanged, this, &MainWindow::onSpeedChanged);
    connect(engine_.get(), &PlaybackEngine::clipChanged, this, &MainWindow::onClipChanged);
    connect(engine_.get(), &PlaybackEngine::proxyStatusChanged, this, &MainWindow::onProxyStatusChanged);
    connect(engine_.get(), &PlaybackEngine::proxyLogMessage, this, &MainWindow::onProxyLogMessage);
    if (debugPerf_) {
        connect(engine_.get(), &PlaybackEngine::perfStatsUpdated, this, &MainWindow::onPerfStatsUpdated);
    }
    connect(engine_.get(), &PlaybackEngine::proxyReady, this, [this](const QString& proxyPath) {
        playbackEnabled_ = true;
        controlBar_->setProxyReady(true);
        controlBar_->setProxyBusy(false);
        viewA_->setLabel("代理 A 画面");
        viewB_->setLabel("代理 B 画面");
        statusBar()->showMessage("代理已就绪: " + proxyPath, 5000);
        updateStatusPanel();
    });
    connect(engine_.get(), &PlaybackEngine::proxyFailed, this, [this](const QString& message) {
        playbackEnabled_ = false;
        controlBar_->setProxyReady(false);
        controlBar_->setProxyBusy(false);
        QMessageBox::warning(this, "代理生成失败", message);
        updateStatusPanel();
    });

    connect(timelineWidget_, &TimelineWidget::seekRequested, this, &MainWindow::onSeekRequested);
    connect(timelineWidget_, &TimelineWidget::previewRequested, this, &MainWindow::onPreviewRequested);

    connect(controlBar_, &ControlBar::openVideoAClicked, this, &MainWindow::openVideoA);
    connect(controlBar_, &ControlBar::openVideoBClicked, this, &MainWindow::openVideoB);
    connect(controlBar_, &ControlBar::buildProxyClicked, this, &MainWindow::onBuildProxy);
    connect(controlBar_, &ControlBar::previousFrameClicked, this, [this]() { engine_->stepFrame(-1); });
    connect(controlBar_, &ControlBar::nextFrameClicked, this, [this]() { engine_->stepFrame(1); });
    connect(controlBar_, &ControlBar::playPauseClicked, engine_.get(), &PlaybackEngine::togglePlay);
    connect(controlBar_, &ControlBar::speedChanged, engine_.get(), &PlaybackEngine::setSpeed);
    connect(controlBar_, &ControlBar::markInClicked, this, [this]() {
        if (playbackEnabled_) engine_->setClipIn(engine_->positionUs());
    });
    connect(controlBar_, &ControlBar::markOutClicked, this, [this]() {
        if (playbackEnabled_) engine_->setClipOut(engine_->positionUs());
    });
    connect(controlBar_, &ControlBar::addClipToQueueClicked, this, &MainWindow::onAddClipToQueue);
    connect(controlBar_, &ControlBar::exportClipClicked, this, &MainWindow::onExportClip);
    connect(controlBar_, &ControlBar::batchExportQueueClicked, this, &MainWindow::onBatchExportQueue);

    connect(addClipQueueBtn_, &QPushButton::clicked, this, &MainWindow::onAddClipToQueue);
    connect(deleteClipQueueBtn_, &QPushButton::clicked, this, &MainWindow::onDeleteSelectedClip);
    connect(jumpClipQueueBtn_, &QPushButton::clicked, this, &MainWindow::onJumpToSelectedClip);
    connect(clearExportedQueueBtn_, &QPushButton::clicked, this, &MainWindow::onClearExportedClips);
    connect(batchExportQueueBtn_, &QPushButton::clicked, this, &MainWindow::onBatchExportQueue);
    connect(clipQueueTable_, &QTableWidget::itemChanged, this, &MainWindow::onQueueItemChanged);
    connect(clipQueueTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        updateClipQueueControls();
    });

    connect(shortcuts_.get(), &ShortcutManager::playPause, engine_.get(), &PlaybackEngine::togglePlay);
    connect(shortcuts_.get(), &ShortcutManager::stepForward1, [this]() { engine_->stepFrame(1); });
    connect(shortcuts_.get(), &ShortcutManager::stepBackward1, [this]() { engine_->stepFrame(-1); });
    connect(shortcuts_.get(), &ShortcutManager::stepForward10, [this]() { engine_->stepFrame(10); });
    connect(shortcuts_.get(), &ShortcutManager::stepBackward10, [this]() { engine_->stepFrame(-10); });
    connect(shortcuts_.get(), &ShortcutManager::stepForward50, [this]() { engine_->stepFrame(50); });
    connect(shortcuts_.get(), &ShortcutManager::stepBackward50, [this]() { engine_->stepFrame(-50); });
    connect(shortcuts_.get(), &ShortcutManager::goToFirst, [this]() { engine_->seekTo(0); });
    connect(shortcuts_.get(), &ShortcutManager::goToEnd, [this]() { engine_->seekTo(engine_->durationUs()); });
    connect(shortcuts_.get(), &ShortcutManager::markIn, [this]() {
        if (playbackEnabled_) engine_->setClipIn(engine_->positionUs());
    });
    connect(shortcuts_.get(), &ShortcutManager::markOut, [this]() {
        if (playbackEnabled_) engine_->setClipOut(engine_->positionUs());
    });
    connect(shortcuts_.get(), &ShortcutManager::jumpToIn, [this]() {
        auto range = engine_->clipRange();
        if (range.inUs) engine_->seekTo(*range.inUs);
    });
    connect(shortcuts_.get(), &ShortcutManager::jumpToOut, [this]() {
        auto range = engine_->clipRange();
        if (range.outUs) engine_->seekTo(*range.outUs);
    });
    connect(shortcuts_.get(), &ShortcutManager::clearIn, engine_.get(), &PlaybackEngine::clearClipIn);
    connect(shortcuts_.get(), &ShortcutManager::clearOut, engine_.get(), &PlaybackEngine::clearClipOut);
    connect(shortcuts_.get(), &ShortcutManager::exportClip, this, &MainWindow::onExportClip);
    connect(shortcuts_.get(), &ShortcutManager::setSpeed025, [this]() { engine_->setSpeed(0.25); });
    connect(shortcuts_.get(), &ShortcutManager::setSpeed05, [this]() { engine_->setSpeed(0.5); });
    connect(shortcuts_.get(), &ShortcutManager::setSpeed1, [this]() { engine_->setSpeed(1.0); });
    connect(shortcuts_.get(), &ShortcutManager::setSpeed2, [this]() { engine_->setSpeed(2.0); });
    connect(shortcuts_.get(), &ShortcutManager::setSpeed4, [this]() { engine_->setSpeed(4.0); });
}

void MainWindow::openVideoA() {
    QString path = QFileDialog::getOpenFileName(
        this, "选择视频 A", QString(),
        "视频文件 (*.mp4 *.mkv *.avi *.mov *.webm *.flv *.ts);;所有文件 (*)");
    if (path.isEmpty()) return;

    videoAReady_ = false;
    playbackEnabled_ = false;
    videoPathA_ = path;
    clipQueue_.clearInMemory();
    clipQueueSourceKey_.clear();
    viewA_->clear();
    viewB_->clear();
    viewA_->setLabel("正在加载视频 A...");
    metadataLabelA_->setText("视频 A: 加载中...");
    statusBar()->showMessage("正在打开视频 A...");
    controlBar_->setProxyReady(false);
    refreshClipQueueTable();
    updateStatusPanel();
    engine_->openVideoA(path);
}

void MainWindow::openVideoB() {
    QString path = QFileDialog::getOpenFileName(
        this, "选择视频 B", QString(),
        "视频文件 (*.mp4 *.mkv *.avi *.mov *.webm *.flv *.ts);;所有文件 (*)");
    if (path.isEmpty()) return;

    videoBReady_ = false;
    playbackEnabled_ = false;
    videoPathB_ = path;
    clipQueue_.clearInMemory();
    clipQueueSourceKey_.clear();
    viewA_->clear();
    viewB_->clear();
    viewB_->setLabel("正在加载视频 B...");
    metadataLabelB_->setText("视频 B: 加载中...");
    statusBar()->showMessage("正在打开视频 B...");
    controlBar_->setProxyReady(false);
    refreshClipQueueTable();
    updateStatusPanel();
    engine_->openVideoB(path);
}

void MainWindow::loadSettings() {
    QSettings settings("DualVideoTool", "DualVideoTool");
    proxySettings_.maxHeight = settings.value("proxy/maxHeight", 720).toInt();
    proxySettings_.crf = settings.value("proxy/crf", 24).toInt();
    proxySettings_.preset = settings.value("proxy/preset", "veryfast").toString();
    proxySettings_.cacheDir = settings.value("proxy/cacheDir", ProxyBuilder::defaultCacheDir()).toString();
    proxySettings_.autoGenerate = settings.value("proxy/autoGenerate", true).toBool();
    proxySettings_.proxyPlaybackOnly = settings.value("proxy/proxyPlaybackOnly", true).toBool();
}

void MainWindow::saveSettings() const {
    QSettings settings("DualVideoTool", "DualVideoTool");
    settings.setValue("proxy/maxHeight", proxySettings_.maxHeight);
    settings.setValue("proxy/crf", proxySettings_.crf);
    settings.setValue("proxy/preset", proxySettings_.preset);
    settings.setValue("proxy/cacheDir", proxySettings_.cacheDir);
    settings.setValue("proxy/autoGenerate", proxySettings_.autoGenerate);
    settings.setValue("proxy/proxyPlaybackOnly", proxySettings_.proxyPlaybackOnly);
}

void MainWindow::updateStatusPanel() {
    if (!modeLabel_ || !proxyStatusLabel_ || !timeStatusLabel_ || !clipStatusLabel_) return;

    modeLabel_->setText("模式: 代理播放");
    QString proxyText = "代理: " + proxyStatusText(proxyStatus_);
    if (!proxyStatusMessage_.isEmpty()) {
        proxyText += " - " + proxyStatusMessage_;
    }
    proxyStatusLabel_->setText(proxyText);
    timeStatusLabel_->setText(QString("时间: %1 / %2")
        .arg(formatTimeUs(engine_ ? engine_->positionUs() : 0))
        .arg(formatTimeUs(engine_ ? engine_->durationUs() : 0)));

    ClipRange range = engine_ ? engine_->clipRange() : ClipRange{};
    if (range.inUs && range.outUs) {
        clipStatusLabel_->setText(QString("片段: %1 - %2")
            .arg(formatTimeUs(*range.inUs))
            .arg(formatTimeUs(*range.outUs)));
    } else if (range.inUs) {
        clipStatusLabel_->setText("片段: 入点 " + formatTimeUs(*range.inUs));
    } else if (range.outUs) {
        clipStatusLabel_->setText("片段: 出点 " + formatTimeUs(*range.outUs));
    } else {
        clipStatusLabel_->setText("片段: 未标记");
    }
}

void MainWindow::validateAndEnablePlayback() {
    if (!videoAReady_ || !videoBReady_ || !engine_->sourcesReady()) return;
    restoreClipQueueForCurrentSources();

    QStringList warnings;
    if (metaA_.width != metaB_.width || metaA_.height != metaB_.height) {
        warnings << QString("分辨率不同 (A: %1x%2, B: %3x%4)")
                     .arg(metaA_.width).arg(metaA_.height).arg(metaB_.width).arg(metaB_.height);
    }
    if (std::abs(metaA_.fps - metaB_.fps) > 0.01) {
        warnings << QString("帧率不同 (A: %1 fps, B: %2 fps)")
                     .arg(metaA_.fps, 0, 'f', 2).arg(metaB_.fps, 0, 'f', 2);
    }

    if (!engine_->isReady()) {
        playbackEnabled_ = false;
        timelineWidget_->setTotalFrames(engine_->frameCount());
        timelineWidget_->setFps(engine_->fps());
        QString ready = QString("原始视频已加载 — %1 帧 @ %2 fps，等待代理生成")
            .arg(engine_->frameCount())
            .arg(engine_->fps(), 0, 'f', 2);
        if (!warnings.isEmpty()) {
            ready += "；警告: " + warnings.join("; ");
        }
        statusBar()->showMessage(ready, warnings.isEmpty() ? 5000 : 8000);
        updateStatusPanel();
        updateClipQueueControls();
        return;
    }

    playbackEnabled_ = true;
    controlBar_->setProxyReady(true);
    timelineWidget_->setTotalFrames(engine_->frameCount());
    timelineWidget_->setFps(engine_->fps());
    timelineWidget_->setCurrentFrame(0);

    QString ready = QString("就绪 — %1 帧 @ %2 fps").arg(engine_->frameCount()).arg(engine_->fps(), 0, 'f', 2);
    if (!warnings.isEmpty()) {
        ready += "；警告: " + warnings.join("; ");
    }
    statusBar()->showMessage(ready, warnings.isEmpty() ? 0 : 8000);
    updateStatusPanel();
    updateClipQueueControls();
}

void MainWindow::onFramePairReady(FramePair pair) {
    if (!pair.isValid()) return;
    viewA_->displayFrame(pair.left);
    viewB_->displayFrame(pair.right);
    bool throttleUi = engine_ && engine_->isPlaying() && timelineUiThrottle_.elapsed() < 100;
    if (throttleUi) {
        deferredTimelineFrame_ = pair.frameIndexA;
        return;
    }
    timelineWidget_->setCurrentFrame(pair.frameIndexA);
    deferredTimelineFrame_ = -1;
    timelineUiThrottle_.restart();
    updateStatusPanel();
}

void MainWindow::onPositionChanged(int64_t positionUs) {
    if (!playbackEnabled_) return;
    int64_t frame = engine_->usToFrameA(positionUs);
    bool throttleUi = engine_->isPlaying() && timelineUiThrottle_.elapsed() < 100;
    if (throttleUi) {
        deferredTimelineFrame_ = frame;
        return;
    }
    timelineWidget_->setCurrentFrame(deferredTimelineFrame_ >= 0 ? deferredTimelineFrame_ : frame);
    deferredTimelineFrame_ = -1;
    timelineUiThrottle_.restart();
    updateStatusPanel();
}

void MainWindow::onEngineStateChanged(PlaybackState state) {
    statusBar()->showMessage(stateText(state), 1000);
}

void MainWindow::onSpeedChanged(double speed) {
    timelineWidget_->setSpeed(speed);
    statusBar()->showMessage(QString("播放速度: %1x").arg(speed, 0, 'f', speed < 1.0 ? 2 : 1), 2000);
}

void MainWindow::onSeekRequested(int64_t frame) {
    engine_->seekTo(engine_->frameToUs(frame));
}

void MainWindow::onPreviewRequested(int64_t frame) {
    engine_->previewAt(engine_->frameToUs(frame));
}

void MainWindow::onClipChanged(ClipRange range) {
    std::optional<int64_t> inFrame;
    std::optional<int64_t> outFrame;
    if (range.inUs) inFrame = engine_->usToFrameA(*range.inUs);
    if (range.outUs) outFrame = engine_->usToFrameA(*range.outUs);
    timelineWidget_->setInMarker(inFrame);
    timelineWidget_->setOutMarker(outFrame);
    updateStatusPanel();
}

QString MainWindow::queuePreflight(bool requireClips) const {
    if (!playbackEnabled_ || !engine_ || !engine_->isReady()) return "视频未加载";
    if (videoPathA_.isEmpty() || !QFileInfo::exists(videoPathA_)) {
        return "视频 A 文件不存在: " + videoPathA_;
    }
    if (videoPathB_.isEmpty() || !QFileInfo::exists(videoPathB_)) {
        return "视频 B 文件不存在: " + videoPathB_;
    }
    if (requireClips && clipQueue_.isEmpty()) return "片段队列为空";
    return {};
}

void MainWindow::restoreClipQueueForCurrentSources() {
    if (!engine_ || !engine_->sourcesReady() || videoPathA_.isEmpty() || videoPathB_.isEmpty()) {
        return;
    }

    clipQueue_.setSources(videoPathA_, videoPathB_);
    QString currentKey = clipQueue_.sessionKey();
    if (!clipQueueSourceKey_.isEmpty() && clipQueueSourceKey_ == currentKey) {
        return;
    }

    clipQueueSourceKey_ = currentKey;
    QString message;
    ClipQueue::LoadResult result = clipQueue_.load(&message);
    refreshClipQueueTable();
    if (result == ClipQueue::LoadResult::Loaded && !message.isEmpty()) {
        statusBar()->showMessage(message, 5000);
    } else if (result == ClipQueue::LoadResult::SourceMismatch ||
               result == ClipQueue::LoadResult::ReadError) {
        statusBar()->showMessage(message, 8000);
        if (logPanel_) logPanel_->appendPlainText(message);
    }
}

void MainWindow::saveClipQueue() {
    QString error;
    if (!clipQueue_.save(&error) && !error.isEmpty()) {
        statusBar()->showMessage(error, 5000);
        if (logPanel_) logPanel_->appendPlainText(error);
    }
}

void MainWindow::refreshClipQueueTable() {
    if (!clipQueueTable_) return;

    updatingClipQueueTable_ = true;
    clipQueueTable_->setRowCount(0);
    const QVector<ClipItem>& items = clipQueue_.items();
    clipQueueTable_->setRowCount(items.size());
    for (int row = 0; row < items.size(); ++row) {
        const ClipItem& clip = items.at(row);
        auto* indexItem = makeReadOnlyItem(QString::number(row + 1));
        indexItem->setData(Qt::UserRole, clip.id);
        clipQueueTable_->setItem(row, QueueColumnIndex, indexItem);
        clipQueueTable_->setItem(row, QueueColumnInFrame, makeReadOnlyItem(QString::number(clip.inFrame)));
        clipQueueTable_->setItem(row, QueueColumnOutFrame, makeReadOnlyItem(QString::number(clip.outFrame)));
        clipQueueTable_->setItem(row, QueueColumnDuration, makeReadOnlyItem(durationText(clip.inFrame, clip.outFrame, engine_ ? engine_->fps() : 30.0)));
        clipQueueTable_->setItem(row, QueueColumnName, new QTableWidgetItem(clip.name));
        clipQueueTable_->setItem(row, QueueColumnNote, new QTableWidgetItem(clip.note));
        auto* statusItem = new QTableWidgetItem(clip.status);
        statusItem->setFlags(statusItem->flags() | Qt::ItemIsEditable);
        clipQueueTable_->setItem(row, QueueColumnStatus, statusItem);
        clipQueueTable_->setItem(row, QueueColumnResult, makeReadOnlyItem(queueResultText(clip)));
    }
    updatingClipQueueTable_ = false;
    updateClipQueueControls();
}

void MainWindow::updateClipQueueControls() {
    int selectedRow = selectedClipQueueRow();
    bool hasSelection = selectedRow >= 0;
    bool hasQueue = !clipQueue_.isEmpty();
    bool ready = playbackEnabled_ && engine_ && engine_->isReady();
    if (clipQueueSummaryLabel_) {
        clipQueueSummaryLabel_->setText(QString("%1 个片段").arg(clipQueue_.size()));
    }
    if (addClipQueueBtn_) addClipQueueBtn_->setEnabled(ready);
    if (deleteClipQueueBtn_) deleteClipQueueBtn_->setEnabled(hasSelection);
    if (jumpClipQueueBtn_) jumpClipQueueBtn_->setEnabled(ready && hasSelection);
    if (clearExportedQueueBtn_) clearExportedQueueBtn_->setEnabled(hasQueue);
    if (batchExportModeCombo_) batchExportModeCombo_->setEnabled(ready && hasQueue);
    if (batchExportQueueBtn_) batchExportQueueBtn_->setEnabled(ready && hasQueue);
}

int MainWindow::selectedClipQueueRow() const {
    if (!clipQueueTable_) return -1;
    QModelIndexList rows = clipQueueTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return -1;
    return rows.first().row();
}

QString MainWindow::selectedClipId() const {
    int row = selectedClipQueueRow();
    if (row < 0 || !clipQueueTable_) return {};
    QTableWidgetItem* idItem = clipQueueTable_->item(row, QueueColumnIndex);
    return idItem ? idItem->data(Qt::UserRole).toString() : QString();
}

void MainWindow::updateQueuedClipStatus(const QString& clipId,
                                        const QString& status,
                                        const QString& outputPath,
                                        const QString& outputPathA,
                                        const QString& outputPathB,
                                        const QString& errorMessage) {
    ClipItem* clip = clipQueue_.findById(clipId);
    if (!clip) return;
    clip->status = status;
    clip->outputPath = outputPath;
    clip->outputPathA = outputPathA;
    clip->outputPathB = outputPathB;
    clip->errorMessage = errorMessage;
    clip->updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    refreshClipQueueTable();
    saveClipQueue();
}

void MainWindow::onAddClipToQueue() {
    QString err = queuePreflight(false);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, "加入队列失败", err);
        return;
    }

    ClipRange range = engine_->clipRange();
    if (!range.isComplete()) {
        QMessageBox::warning(this, "加入队列失败", "请先标记入点和出点 (I/O)");
        return;
    }

    int64_t start = engine_->usToFrameA(*range.inUs);
    int64_t end = engine_->usToFrameA(*range.outUs);
    if (start > end) {
        QMessageBox::warning(this, "加入队列失败", QString("入点 (%1) 大于出点 (%2)").arg(start).arg(end));
        return;
    }

    clipQueue_.setSources(videoPathA_, videoPathB_);
    clipQueueSourceKey_ = clipQueue_.sessionKey();
    ClipItem item = clipQueue_.addClip(*range.inUs, *range.outUs, start, end);
    refreshClipQueueTable();
    saveClipQueue();
    statusBar()->showMessage(QString("已加入队列: %1").arg(item.name), 3000);
}

void MainWindow::onDeleteSelectedClip() {
    int row = selectedClipQueueRow();
    if (row < 0) return;
    if (!clipQueue_.removeAt(row)) return;
    refreshClipQueueTable();
    saveClipQueue();
}

void MainWindow::onJumpToSelectedClip() {
    QString id = selectedClipId();
    const ClipItem* clip = clipQueue_.findById(id);
    if (!clip || !engine_ || !engine_->isReady()) return;
    engine_->pause();
    engine_->seekTo(clip->inUs);
    engine_->setClipIn(clip->inUs);
    engine_->setClipOut(clip->outUs);
}

void MainWindow::onClearExportedClips() {
    int removed = clipQueue_.clearExported();
    if (removed <= 0) {
        statusBar()->showMessage("没有已导出的片段可清空", 3000);
        return;
    }
    refreshClipQueueTable();
    saveClipQueue();
    statusBar()->showMessage(QString("已清空 %1 个已导出片段").arg(removed), 3000);
}

void MainWindow::onQueueItemChanged(QTableWidgetItem* item) {
    if (updatingClipQueueTable_ || !item) return;
    int row = item->row();
    if (row < 0 || row >= clipQueue_.size()) return;
    ClipItem* clip = clipQueue_.findById(clipQueueTable_->item(row, QueueColumnIndex)->data(Qt::UserRole).toString());
    if (!clip) return;

    if (item->column() == QueueColumnName) {
        clip->name = item->text().trimmed().isEmpty()
            ? QString("clip_%1").arg(row + 1, 3, 10, QChar('0'))
            : item->text().trimmed();
    } else if (item->column() == QueueColumnNote) {
        clip->note = item->text();
    } else if (item->column() == QueueColumnStatus) {
        QString value = item->text().trimmed();
        clip->status = value.isEmpty() ? ClipQueue::pendingStatus() : value;
    } else {
        return;
    }
    clip->updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    saveClipQueue();
    refreshClipQueueTable();
}

void MainWindow::onBatchExportQueue() {
    QString err = queuePreflight(true);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, "批量导出失败", err);
        return;
    }

    QString outputDir = QFileDialog::getExistingDirectory(this, "选择批量导出目录");
    if (outputDir.isEmpty()) return;

    engine_->pause();
    QDir dir(outputDir);
    QString testFile = dir.filePath(".dvt_batch_write_test");
    QFile f(testFile);
    if (!dir.exists() || !f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "批量导出失败", "输出目录不可写: " + outputDir);
        return;
    }
    f.close();
    QFile::remove(testFile);

    BatchExportJob job;
    job.inputPathA = videoPathA_;
    job.inputPathB = videoPathB_;
    job.outputDir = outputDir;
    job.logDir = dir.filePath("logs");
    job.fps = engine_->fps();
    job.clips = clipQueue_.items();
    job.mode = batchExportModeFromId(batchExportModeCombo_
        ? batchExportModeCombo_->currentData().toString()
        : QString());

    int total = 0;
    for (const ClipItem& clip : job.clips) {
        int frames = static_cast<int>(std::max<int64_t>(0, clip.outFrame - clip.inFrame + 1));
        total += job.mode == BatchExportMode::SeparateAB ? frames * 2 : frames;
    }
    total = std::max(1, total);

    auto* exportThread = new QThread(this);
    auto* exporter = new ClipExporter();
    exporter->moveToThread(exportThread);

    auto* progressDialog = new QProgressDialog(
        QString("正在批量导出片段队列（%1）...").arg(batchExportModeLabel(job.mode)),
        "取消", 0, total, this);
    progressDialog->setWindowTitle("批量导出片段队列");
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->show();

    connect(exporter, &ClipExporter::progressChanged, this, [progressDialog](int current, int) {
        progressDialog->setValue(current);
    });
    connect(exporter, &ClipExporter::logMessage, this, [this](const QString& msg) {
        if (logPanel_) logPanel_->appendPlainText(msg);
        statusBar()->showMessage(msg, 3000);
    });
    connect(exporter, &ClipExporter::clipStatusChanged, this, &MainWindow::updateQueuedClipStatus);
    connect(progressDialog, &QProgressDialog::canceled, this, [progressDialog, exportThread]() {
        exportThread->requestInterruption();
        progressDialog->setLabelText("正在取消...");
    });
    connect(exporter, &ClipExporter::batchFailed, this,
        [this, progressDialog, exportThread](const QString& message) {
            progressDialog->close();
            progressDialog->deleteLater();
            QMessageBox::warning(this, "批量导出失败", message);
            statusBar()->showMessage("批量导出失败", 5000);
            exportThread->quit();
        });
    connect(exporter, &ClipExporter::batchFinished, this,
        [this, progressDialog, exportThread](const BatchExportSummary& summary) {
            progressDialog->setValue(progressDialog->maximum());
            progressDialog->close();
            progressDialog->deleteLater();
            statusBar()->showMessage(
                QString("批量导出完成: 成功 %1，失败 %2，清单: %3")
                    .arg(summary.exportedCount)
                    .arg(summary.failedCount)
                    .arg(summary.manifestPath),
                10000);
            exportThread->quit();
        });
    connect(exportThread, &QThread::started, exporter, [exporter, job]() {
        exporter->exportBatchClips(job);
    });
    connect(exportThread, &QThread::finished, exporter, &QObject::deleteLater);
    connect(exportThread, &QThread::finished, exportThread, &QObject::deleteLater);
    exportThread->start();
}

void MainWindow::onBuildProxy() {
    if (!videoAReady_ || !videoBReady_ || !engine_->sourcesReady()) {
        QMessageBox::information(this, "生成代理", "请先打开视频 A 和视频 B。");
        return;
    }

    playbackEnabled_ = false;
    controlBar_->setProxyReady(false);
    controlBar_->setProxyBusy(true);
    logPanel_->appendPlainText("手动重建代理...");
    engine_->buildProxy(true);
    updateStatusPanel();
}

void MainWindow::showSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle("设置");
    auto* layout = new QVBoxLayout(&dialog);

    auto* form = new QFormLayout();
    auto* heightSpin = new QSpinBox();
    heightSpin->setRange(240, 2160);
    heightSpin->setSingleStep(120);
    heightSpin->setValue(proxySettings_.maxHeight);
    form->addRow("每路最大高度", heightSpin);

    auto* crfSpin = new QSpinBox();
    crfSpin->setRange(16, 36);
    crfSpin->setValue(proxySettings_.crf);
    form->addRow("代理 CRF", crfSpin);

    auto* presetCombo = new QComboBox();
    for (const QString& preset : {"ultrafast", "superfast", "veryfast", "faster", "fast", "medium"}) {
        presetCombo->addItem(preset);
    }
    int presetIndex = presetCombo->findText(proxySettings_.preset);
    presetCombo->setCurrentIndex(presetIndex >= 0 ? presetIndex : 2);
    form->addRow("编码 preset", presetCombo);

    auto* cacheRow = new QWidget();
    auto* cacheLayout = new QHBoxLayout(cacheRow);
    cacheLayout->setContentsMargins(0, 0, 0, 0);
    auto* cacheEdit = new QLineEdit(proxySettings_.cacheDir);
    auto* browseBtn = new QPushButton("浏览...");
    cacheLayout->addWidget(cacheEdit, 1);
    cacheLayout->addWidget(browseBtn);
    form->addRow("代理缓存目录", cacheRow);
    connect(browseBtn, &QPushButton::clicked, &dialog, [this, cacheEdit]() {
        QString dir = QFileDialog::getExistingDirectory(this, "选择代理缓存目录", cacheEdit->text());
        if (!dir.isEmpty()) {
            cacheEdit->setText(dir);
        }
    });

    auto* autoGenerateCheck = new QCheckBox("打开两路视频后自动生成代理");
    autoGenerateCheck->setChecked(proxySettings_.autoGenerate);
    form->addRow("", autoGenerateCheck);

    auto* proxyOnlyCheck = new QCheckBox("仅使用代理播放；代理失败时不回退双路实时解码");
    proxyOnlyCheck->setChecked(proxySettings_.proxyPlaybackOnly);
    proxyOnlyCheck->setEnabled(false);
    form->addRow("", proxyOnlyCheck);

    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    proxySettings_.maxHeight = heightSpin->value();
    proxySettings_.crf = crfSpin->value();
    proxySettings_.preset = presetCombo->currentText();
    proxySettings_.cacheDir = cacheEdit->text().trimmed();
    proxySettings_.autoGenerate = autoGenerateCheck->isChecked();
    proxySettings_.proxyPlaybackOnly = true;
    engine_->setProxySettings(proxySettings_);
    saveSettings();
    statusBar()->showMessage("设置已保存。新的代理设置会在下次生成代理时生效。", 5000);
}

void MainWindow::showUsageHelp() {
    QMessageBox::information(this, "使用说明",
        "基本流程\n"
        "1. 点击“打开 A”和“打开 B”，选择两路原始视频。\n"
        "2. 程序会自动生成左右拼接的 H.264 代理视频；也可以点击“生成代理”手动重建。\n"
        "3. 代理就绪后使用播放、暂停、上一帧、下一帧和时间线定位。\n"
        "4. 播放到需要的位置后点击“入点”和“出点”。\n"
        "5. 点击“加入队列”保存当前片段，可继续标记并加入多个片段。\n"
        "6. 在片段队列中选择批量导出方式：左右拼接 MP4，或分别导出 A/B MP4。\n"
        "7. 单段“导出”和队列“批量导出”都会从原始视频 A/B 输出，不导出代理视频。\n\n"
        "常见问题\n"
        "- 若提示未找到 ffmpeg.exe，请把 ffmpeg.exe 放到程序目录的 tools 文件夹，或加入 PATH。\n"
        "- 代理缓存默认写入系统缓存目录，可在设置里改到更快或空间更大的磁盘。\n"
        "- V1.8 默认不再使用双路实时解码播放；代理生成失败时请修复原因后重试。");
}

void MainWindow::onProxyStatusChanged(ProxyStatus status, const QString& message) {
    proxyStatus_ = status;
    proxyStatusMessage_ = message;
    bool busy = status == ProxyStatus::PreparingProxy;
    controlBar_->setProxyBusy(busy);
    if (busy) {
        playbackEnabled_ = false;
        controlBar_->setProxyReady(false);
    }
    updateStatusPanel();
    if (!message.isEmpty()) {
        statusBar()->showMessage(message, busy ? 0 : 5000);
    }
}

void MainWindow::onProxyLogMessage(const QString& message) {
    if (!logPanel_) return;
    logPanel_->appendPlainText(message);
    QTextCursor cursor = logPanel_->textCursor();
    cursor.movePosition(QTextCursor::End);
    logPanel_->setTextCursor(cursor);
}

void MainWindow::onPerfStatsUpdated(PlaybackPerfStats stats) {
    QString line = QString("[PERF] fps=%1 target=%2 displayed=%3 dropped=%4 repeated=%5 "
                           "latency(avg/max)=%6/%7ms tick(avg/max)=%8/%9ms seek=%10ms")
        .arg(stats.displayedFps, 0, 'f', 1)
        .arg(stats.targetFrame)
        .arg(stats.displayedFrame)
        .arg(stats.droppedTargetCount)
        .arg(stats.repeatedTargetCount)
        .arg(stats.averageFrameLatencyMs, 0, 'f', 1)
        .arg(stats.maxFrameLatencyMs, 0, 'f', 1)
        .arg(stats.averageTickIntervalMs, 0, 'f', 1)
        .arg(stats.maxTickIntervalMs, 0, 'f', 1)
        .arg(stats.seekLatencyMs, 0, 'f', 1);
    qInfo().noquote() << line;
    if (!logPanel_) return;
    logPanel_->appendPlainText(line);
    QTextCursor cursor = logPanel_->textCursor();
    cursor.movePosition(QTextCursor::End);
    logPanel_->setTextCursor(cursor);
}

QString MainWindow::exportPreflight(ExportJob& job) {
    if (!playbackEnabled_ || !engine_->isReady()) return "视频未加载";

    ClipRange range = engine_->clipRange();
    if (!range.isComplete()) return "请先标记入点和出点 (I/O)";
    if (videoPathA_.isEmpty() || !QFileInfo::exists(videoPathA_)) {
        return "视频 A 文件不存在: " + videoPathA_;
    }
    if (videoPathB_.isEmpty() || !QFileInfo::exists(videoPathB_)) {
        return "视频 B 文件不存在: " + videoPathB_;
    }

    int64_t start = engine_->usToFrameA(*range.inUs);
    int64_t end = engine_->usToFrameA(*range.outUs);
    if (start > end) return QString("入点 (%1) 大于出点 (%2)").arg(start).arg(end);

    QString outputDir = QFileDialog::getExistingDirectory(this, "选择输出目录");
    if (outputDir.isEmpty()) return "已取消";

    QDir dir(outputDir);
    if (!dir.exists()) return "输出目录不存在: " + outputDir;

    QString testFile = dir.filePath(".dvt_write_test");
    QFile f(testFile);
    if (!f.open(QIODevice::WriteOnly)) return "输出目录不可写: " + outputDir;
    f.close();
    QFile::remove(testFile);

    job.inputPathA = videoPathA_;
    job.inputPathB = videoPathB_;
    job.startFrame = start;
    job.endFrame = end;
    job.fps = engine_->fps();
    job.logDir = dir.filePath("logs");
    job.outputPathA = dir.filePath(
        QString("video_a_clip_%1_%2.mp4").arg(start, 6, 10, QChar('0')).arg(end, 6, 10, QChar('0')));
    job.outputPathB = dir.filePath(
        QString("video_b_clip_%1_%2.mp4").arg(start, 6, 10, QChar('0')).arg(end, 6, 10, QChar('0')));

    return {};
}

void MainWindow::onExportClip() {
    ExportJob job;
    QString err = exportPreflight(job);
    if (!err.isEmpty()) {
        if (err != "已取消") {
            QMessageBox::warning(this, "导出失败", err);
        }
        return;
    }

    engine_->pause();

    int total = static_cast<int>(job.endFrame - job.startFrame + 1) * 2;

    auto* exportThread = new QThread(this);
    auto* exporter = new ClipExporter();
    exporter->moveToThread(exportThread);

    auto* progressDialog = new QProgressDialog("正在导出...", "取消", 0, total, this);
    progressDialog->setWindowTitle("导出片段");
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->show();

    connect(exporter, &ClipExporter::progressChanged, this, [progressDialog](int current, int) {
        progressDialog->setValue(current);
    });
    connect(exporter, &ClipExporter::logMessage, this, [this](const QString& msg) {
        statusBar()->showMessage(msg, 3000);
    });
    connect(progressDialog, &QProgressDialog::canceled, this, [progressDialog, exportThread]() {
        exportThread->requestInterruption();
        progressDialog->setLabelText("正在取消...");
    });
    connect(exporter, &ClipExporter::failed, this,
        [this, progressDialog, exportThread](const QString& message) {
            progressDialog->close();
            progressDialog->deleteLater();
            QMessageBox::warning(this, "导出失败", message);
            statusBar()->showMessage("导出失败，请查看日志", 5000);
            exportThread->quit();
        });
    connect(exporter, &ClipExporter::finished, this,
        [this, progressDialog, exportThread](const QString& outputA, const QString& outputB) {
            progressDialog->setValue(progressDialog->maximum());
            progressDialog->close();
            progressDialog->deleteLater();
            statusBar()->showMessage(QString("导出完成: %1, %2").arg(outputA, outputB), 10000);
            exportThread->quit();
        });
    connect(exportThread, &QThread::started, exporter, [exporter, job]() {
        exporter->exportBothClips(job);
    });
    connect(exportThread, &QThread::finished, exporter, &QObject::deleteLater);
    connect(exportThread, &QThread::finished, exportThread, &QObject::deleteLater);

    exportThread->start();
}

QString MainWindow::stateText(PlaybackState state) const {
    switch (state) {
        case PlaybackState::Stopped: return "已停止";
        case PlaybackState::Opening: return "正在打开视频...";
        case PlaybackState::PreparingProxy: return "正在准备代理...";
        case PlaybackState::ProxyReady: return "代理已就绪";
        case PlaybackState::ProxyFailed: return "代理失败";
        case PlaybackState::Paused: return "已暂停";
        case PlaybackState::Playing: return "播放中";
        case PlaybackState::Seeking: return "正在定位...";
        case PlaybackState::Buffering: return "正在缓冲...";
        case PlaybackState::Exporting: return "正在导出...";
    }
    return "就绪";
}

QString MainWindow::proxyStatusText(ProxyStatus status) const {
    switch (status) {
        case ProxyStatus::Idle: return "未生成";
        case ProxyStatus::PreparingProxy: return "生成中";
        case ProxyStatus::ProxyReady: return "已就绪";
        case ProxyStatus::ProxyFailed: return "失败";
    }
    return "未知";
}

QString MainWindow::formatTimeUs(int64_t positionUs) const {
    if (positionUs <= 0) {
        return "00:00.000";
    }

    int64_t totalMs = positionUs / 1000;
    int64_t minutes = totalMs / 60000;
    int64_t seconds = (totalMs / 1000) % 60;
    int64_t millis = totalMs % 1000;
    return QString("%1:%2.%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(millis, 3, 10, QChar('0'));
}
