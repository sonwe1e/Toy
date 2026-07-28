#pragma once

#include "video/VideoDecoder.h"
#include "video/VideoMetadata.h"
#include "export/ClipQueue.h"

#include <QObject>
#include <QString>
#include <QThread>
#include <QVector>
#include <cstdint>

enum class BatchExportMode {
    SideBySide,
    SeparateAB
};

QString batchExportModeId(BatchExportMode mode);
QString batchExportModeLabel(BatchExportMode mode);
BatchExportMode batchExportModeFromId(const QString& id);

struct ExportJob {
    QString inputPathA;
    QString inputPathB;
    QString outputPathA;
    QString outputPathB;
    QString logDir;
    int64_t startFrame = 0;
    int64_t endFrame = 0;
    double fps = 30.0;
};

struct BatchExportJob {
    QString inputPathA;
    QString inputPathB;
    QString outputDir;
    QString logDir;
    double fps = 30.0;
    BatchExportMode mode = BatchExportMode::SideBySide;
    QVector<ClipItem> clips;
};

struct BatchExportSummary {
    QString manifestPath;
    int exportedCount = 0;
    int failedCount = 0;
};

Q_DECLARE_METATYPE(BatchExportSummary)

class ClipExporter : public QObject {
    Q_OBJECT

public:
    explicit ClipExporter(QObject* parent = nullptr);

    // Export both clips. Runs entirely in the worker thread that owns this object.
    void exportBothClips(const ExportJob& job);
    void exportBatchClips(const BatchExportJob& job);
    void exportBatchSideBySide(const BatchExportJob& job);

signals:
    void progressChanged(int current, int total);
    void finished(QString outputA, QString outputB);
    void failed(QString errorMessage);
    void logMessage(QString message);
    void clipStatusChanged(QString clipId,
                           QString status,
                           QString outputPath,
                           QString outputPathA,
                           QString outputPathB,
                           QString errorMessage);
    void batchFinished(BatchExportSummary summary);
    void batchFailed(QString errorMessage);

private:
    bool exportOneClip(VideoDecoder& decoder,
                       const QString& outputPath,
                       int64_t startFrame,
                       int64_t endFrame,
                       double fps,
                       int progressOffset,
                       int totalProgress,
                       QStringList& logLines,
                       QString& errorMessage);
    bool exportOneSideBySideClip(VideoDecoder& decoderA,
                                 VideoDecoder& decoderB,
                                 const ClipItem& clip,
                                 const QString& outputPath,
                                 double fps,
                                 int progressOffset,
                                 int totalProgress,
                                 QStringList& logLines,
                                 QString& errorMessage);
    bool writeManifest(const BatchExportJob& job,
                       const QVector<ClipItem>& results,
                       const QString& manifestPath,
                       QString& errorMessage);
    void writeLog(const QString& logPath, const QString& content);
};
