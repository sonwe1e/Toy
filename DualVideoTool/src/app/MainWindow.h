#pragma once

#include "video/VideoMetadata.h"
#include "playback/PlaybackEngine.h"
#include "ui/VideoView.h"
#include "ui/TimelineWidget.h"
#include "ui/ControlBar.h"
#include "app/ShortcutManager.h"
#include "export/ClipExporter.h"
#include "export/ClipQueue.h"

#include <QMainWindow>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QElapsedTimer>
#include <memory>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void openVideoA();
    void openVideoB();
    void onFramePairReady(FramePair pair);
    void onPositionChanged(int64_t positionUs);
    void onEngineStateChanged(PlaybackState state);
    void onSpeedChanged(double speed);
    void onSeekRequested(int64_t frame);
    void onPreviewRequested(int64_t frame);
    void onClipChanged(ClipRange range);
    void onExportClip();
    void onAddClipToQueue();
    void onDeleteSelectedClip();
    void onJumpToSelectedClip();
    void onClearExportedClips();
    void onBatchExportQueue();
    void onQueueItemChanged(QTableWidgetItem* item);
    void onBuildProxy();
    void showSettings();
    void showUsageHelp();
    void onProxyStatusChanged(ProxyStatus status, const QString& message);
    void onProxyLogMessage(const QString& message);
    void onPerfStatsUpdated(PlaybackPerfStats stats);

private:
    void setupUi();
    void setupMenus();
    void setupConnections();
    void loadSettings();
    void saveSettings() const;
    void updateStatusPanel();
    void validateAndEnablePlayback();
    QString exportPreflight(ExportJob& job);
    QString queuePreflight(bool requireClips) const;
    void restoreClipQueueForCurrentSources();
    void saveClipQueue();
    void refreshClipQueueTable();
    void updateClipQueueControls();
    int selectedClipQueueRow() const;
    QString selectedClipId() const;
    void updateQueuedClipStatus(const QString& clipId,
                                const QString& status,
                                const QString& outputPath,
                                const QString& outputPathA,
                                const QString& outputPathB,
                                const QString& errorMessage);
    QString stateText(PlaybackState state) const;
    QString proxyStatusText(ProxyStatus status) const;
    QString formatTimeUs(int64_t positionUs) const;

    // Track open state
    bool videoAReady_ = false;
    bool videoBReady_ = false;
    QString videoPathA_;
    QString videoPathB_;
    VideoMetadata metaA_;
    VideoMetadata metaB_;
    ProxyStatus proxyStatus_ = ProxyStatus::Idle;
    QString proxyStatusMessage_;
    ProxySettings proxySettings_;
    ClipQueue clipQueue_;
    QString clipQueueSourceKey_;
    bool updatingClipQueueTable_ = false;

    // Components
    std::unique_ptr<PlaybackEngine> engine_;
    std::unique_ptr<ShortcutManager> shortcuts_;

    // UI
    VideoView* viewA_ = nullptr;
    VideoView* viewB_ = nullptr;
    TimelineWidget* timelineWidget_ = nullptr;
    ControlBar* controlBar_ = nullptr;
    QLabel* metadataLabelA_ = nullptr;
    QLabel* metadataLabelB_ = nullptr;
    QLabel* modeLabel_ = nullptr;
    QLabel* proxyStatusLabel_ = nullptr;
    QLabel* timeStatusLabel_ = nullptr;
    QLabel* clipStatusLabel_ = nullptr;
    QLabel* clipQueueSummaryLabel_ = nullptr;
    QTableWidget* clipQueueTable_ = nullptr;
    QPushButton* addClipQueueBtn_ = nullptr;
    QPushButton* deleteClipQueueBtn_ = nullptr;
    QPushButton* jumpClipQueueBtn_ = nullptr;
    QPushButton* clearExportedQueueBtn_ = nullptr;
    QComboBox* batchExportModeCombo_ = nullptr;
    QPushButton* batchExportQueueBtn_ = nullptr;
    QPlainTextEdit* logPanel_ = nullptr;

    bool playbackEnabled_ = false;

    // Debug perf flag
    bool debugPerf_ = false;
    QElapsedTimer timelineUiThrottle_;
    int64_t deferredTimelineFrame_ = -1;
};
