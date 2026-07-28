#include "export/ClipExporter.h"
#include "video/VideoEncoder.h"
#include "video/VideoFrame.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>

namespace {
int evenDimension(int value) {
    if (value < 2) return 2;
    return (value % 2 == 0) ? value : value + 1;
}

QString safeFilePart(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) return {};
    value.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\s]+")), QStringLiteral("_"));
    value.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    value.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    return value.left(80);
}

QString uniqueMp4Path(const QDir& outputDir,
                      const QString& baseName,
                      int64_t inFrame,
                      int64_t outFrame,
                      const QString& suffix) {
    QString stem = QString("%1_f%2_f%3_%4")
        .arg(baseName)
        .arg(inFrame, 6, 10, QChar('0'))
        .arg(outFrame, 6, 10, QChar('0'))
        .arg(suffix);

    QString path = outputDir.filePath(stem + ".mp4");
    for (int copyIndex = 2; QFileInfo::exists(path); ++copyIndex) {
        path = outputDir.filePath(QString("%1_%2.mp4").arg(stem).arg(copyIndex, 2, 10, QChar('0')));
    }
    return path;
}

QJsonObject clipToManifestJson(const ClipItem& clip) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), clip.id);
    object.insert(QStringLiteral("name"), clip.name);
    object.insert(QStringLiteral("inUs"), static_cast<double>(clip.inUs));
    object.insert(QStringLiteral("outUs"), static_cast<double>(clip.outUs));
    object.insert(QStringLiteral("inFrame"), static_cast<double>(clip.inFrame));
    object.insert(QStringLiteral("outFrame"), static_cast<double>(clip.outFrame));
    object.insert(QStringLiteral("note"), clip.note);
    object.insert(QStringLiteral("status"), clip.status);
    object.insert(QStringLiteral("outputPath"), clip.outputPath);
    object.insert(QStringLiteral("outputPathA"), clip.outputPathA);
    object.insert(QStringLiteral("outputPathB"), clip.outputPathB);
    object.insert(QStringLiteral("errorMessage"), clip.errorMessage);
    object.insert(QStringLiteral("createdAt"), clip.createdAt);
    object.insert(QStringLiteral("updatedAt"), clip.updatedAt);
    return object;
}
}

QString batchExportModeId(BatchExportMode mode) {
    switch (mode) {
        case BatchExportMode::SideBySide: return QStringLiteral("side_by_side");
        case BatchExportMode::SeparateAB: return QStringLiteral("separate_ab");
    }
    return QStringLiteral("side_by_side");
}

QString batchExportModeLabel(BatchExportMode mode) {
    switch (mode) {
        case BatchExportMode::SideBySide: return QStringLiteral("左右拼接 MP4");
        case BatchExportMode::SeparateAB: return QStringLiteral("分别导出 A/B MP4");
    }
    return QStringLiteral("左右拼接 MP4");
}

BatchExportMode batchExportModeFromId(const QString& id) {
    if (id == QStringLiteral("separate_ab")) {
        return BatchExportMode::SeparateAB;
    }
    return BatchExportMode::SideBySide;
}

ClipExporter::ClipExporter(QObject* parent) : QObject(parent) {
    qRegisterMetaType<BatchExportSummary>("BatchExportSummary");
}

void ClipExporter::writeLog(const QString& logPath, const QString& content) {
    QFile f(logPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << content;
        f.close();
    }
}

bool ClipExporter::exportOneClip(VideoDecoder& decoder,
                                  const QString& outputPath,
                                  int64_t startFrame,
                                  int64_t endFrame,
                                  double fps,
                                  int progressOffset,
                                  int totalProgress,
                                  QStringList& logLines,
                                  QString& errorMessage) {
    VideoMetadata meta = decoder.metadata();
    logLines << QString("Resolution: %1x%2").arg(meta.width).arg(meta.height);
    logLines << QString("Codec: %1").arg(QString::fromStdString(meta.codecName));

    VideoEncoder encoder;
    if (!encoder.open(outputPath, meta.width, meta.height, fps)) {
        logLines << "ERROR: Failed to open encoder";
        errorMessage = QString("无法创建输出文件: %1").arg(outputPath);
        return false;
    }

    logLines << "Encoder opened successfully";

    for (int64_t i = startFrame; i <= endFrame; i++) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            logLines << "CANCELLED by user";
            encoder.close();
            errorMessage = "导出已取消";
            return false;
        }

        auto frame = decoder.decodeFrame(i);
        if (!frame.has_value()) {
            logLines << QString("ERROR: Failed to decode frame %1").arg(i);
            errorMessage = QString("无法解码帧 %1").arg(i);
            return false;
        }

        if (!encoder.writeFrame(frame->image)) {
            logLines << QString("ERROR: Failed to encode frame %1").arg(i);
            errorMessage = QString("无法编码帧 %1").arg(i);
            return false;
        }

        emit progressChanged(progressOffset + static_cast<int>(i - startFrame + 1), totalProgress);
    }

    encoder.close();
    logLines << "Export completed successfully";
    return true;
}

bool ClipExporter::exportOneSideBySideClip(VideoDecoder& decoderA,
                                            VideoDecoder& decoderB,
                                            const ClipItem& clip,
                                            const QString& outputPath,
                                            double fps,
                                            int progressOffset,
                                            int totalProgress,
                                            QStringList& logLines,
                                            QString& errorMessage) {
    VideoMetadata metaA = decoderA.metadata();
    VideoMetadata metaB = decoderB.metadata();
    int outputWidth = evenDimension(metaA.width + metaB.width);
    int outputHeight = evenDimension(std::max(metaA.height, metaB.height));
    int leftY = (outputHeight - metaA.height) / 2;
    int rightY = (outputHeight - metaB.height) / 2;

    logLines << QString("Clip: %1").arg(clip.name);
    logLines << QString("Frame range: %1 - %2").arg(clip.inFrame).arg(clip.outFrame);
    logLines << QString("Canvas: %1x%2").arg(outputWidth).arg(outputHeight);
    logLines << QString("Source A: %1x%2").arg(metaA.width).arg(metaA.height);
    logLines << QString("Source B: %1x%2").arg(metaB.width).arg(metaB.height);

    VideoEncoder encoder;
    if (!encoder.open(outputPath, outputWidth, outputHeight, fps)) {
        logLines << "ERROR: Failed to open side-by-side encoder";
        errorMessage = QString("无法创建输出文件: %1").arg(outputPath);
        return false;
    }

    for (int64_t i = clip.inFrame; i <= clip.outFrame; ++i) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            logLines << "CANCELLED by user";
            encoder.close();
            errorMessage = "导出已取消";
            return false;
        }

        auto frameA = decoderA.decodeFrame(i);
        auto frameB = decoderB.decodeFrame(i);
        if (!frameA.has_value()) {
            logLines << QString("ERROR: Failed to decode video A frame %1").arg(i);
            errorMessage = QString("无法解码视频 A 第 %1 帧").arg(i);
            return false;
        }
        if (!frameB.has_value()) {
            logLines << QString("ERROR: Failed to decode video B frame %1").arg(i);
            errorMessage = QString("无法解码视频 B 第 %1 帧").arg(i);
            return false;
        }

        QImage canvas(outputWidth, outputHeight, QImage::Format_RGB888);
        canvas.fill(Qt::black);
        QPainter painter(&canvas);
        painter.drawImage(0, leftY, frameA->image.convertToFormat(QImage::Format_RGB888));
        painter.drawImage(metaA.width, rightY, frameB->image.convertToFormat(QImage::Format_RGB888));
        painter.end();

        if (!encoder.writeFrame(canvas)) {
            logLines << QString("ERROR: Failed to encode side-by-side frame %1").arg(i);
            errorMessage = QString("无法编码第 %1 帧").arg(i);
            return false;
        }

        emit progressChanged(progressOffset + static_cast<int>(i - clip.inFrame + 1), totalProgress);
    }

    encoder.close();
    logLines << "Side-by-side export completed successfully";
    return true;
}

bool ClipExporter::writeManifest(const BatchExportJob& job,
                                  const QVector<ClipItem>& results,
                                  const QString& manifestPath,
                                  QString& errorMessage) {
    QJsonObject sources;
    sources.insert(QStringLiteral("videoA"), QFileInfo(job.inputPathA).absoluteFilePath());
    sources.insert(QStringLiteral("videoB"), QFileInfo(job.inputPathB).absoluteFilePath());
    sources.insert(QStringLiteral("fps"), job.fps);

    QJsonArray clips;
    for (const ClipItem& item : results) {
        clips.push_back(clipToManifestJson(item));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("type"),
                job.mode == BatchExportMode::SeparateAB
                    ? QStringLiteral("separate_ab_batch_export")
                    : QStringLiteral("side_by_side_batch_export"));
    root.insert(QStringLiteral("exportMode"), batchExportModeId(job.mode));
    root.insert(QStringLiteral("exportModeLabel"), batchExportModeLabel(job.mode));
    root.insert(QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("sources"), sources);
    root.insert(QStringLiteral("clips"), clips);

    QFile file(manifestPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorMessage = QString("无法写入导出清单: %1").arg(manifestPath);
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

void ClipExporter::exportBothClips(const ExportJob& job) {
    QString timestamp = QDateTime::currentDateTime().toString("yyyy_MM_dd_hhmmss");
    QDir logDir(job.logDir);
    if (!logDir.exists()) {
        logDir.mkpath(".");
    }

    auto makeLogHeader = [&](const QString& inputPath, const QString& outputPath) {
        QStringList lines;
        lines << QString("Export started: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
        lines << QString("Input: %1").arg(inputPath);
        lines << QString("Output: %1").arg(outputPath);
        lines << QString("Frame range: %1 - %2").arg(job.startFrame).arg(job.endFrame);
        lines << QString("FPS: %1").arg(job.fps);
        return lines;
    };

    QString logPathA = logDir.filePath(QString("export_a_%1.log").arg(timestamp));
    QString logPathB = logDir.filePath(QString("export_b_%1.log").arg(timestamp));
    QStringList logA = makeLogHeader(job.inputPathA, job.outputPathA);
    QStringList logB = makeLogHeader(job.inputPathB, job.outputPathB);

    auto fail = [&](const QString& message) {
        logA << QString("ERROR: %1").arg(message);
        logB << QString("ERROR: %1").arg(message);
        writeLog(logPathA, logA.join("\n"));
        writeLog(logPathB, logB.join("\n"));
        emit logMessage("导出失败: " + message);
        emit failed(message);
    };

    if (!QFileInfo::exists(job.inputPathA)) {
        fail("视频 A 文件不存在: " + job.inputPathA);
        return;
    }
    if (!QFileInfo::exists(job.inputPathB)) {
        fail("视频 B 文件不存在: " + job.inputPathB);
        return;
    }

    VideoDecoder decoderA;
    VideoDecoder decoderB;
    if (!decoderA.open(job.inputPathA)) {
        fail("无法打开视频 A: " + job.inputPathA);
        return;
    }
    if (!decoderB.open(job.inputPathB)) {
        fail("无法打开视频 B: " + job.inputPathB);
        return;
    }

    int clipFrames = static_cast<int>(job.endFrame - job.startFrame + 1);
    int totalProgress = clipFrames * 2;

    QString errorMessage;
    emit logMessage("正在导出视频 A...");
    bool okA = exportOneClip(decoderA, job.outputPathA, job.startFrame, job.endFrame,
                             job.fps, 0, totalProgress, logA, errorMessage);
    writeLog(logPathA, logA.join("\n"));
    if (!okA) {
        fail(errorMessage);
        return;
    }

    emit logMessage("正在导出视频 B...");
    bool okB = exportOneClip(decoderB, job.outputPathB, job.startFrame, job.endFrame,
                             job.fps, clipFrames, totalProgress, logB, errorMessage);
    writeLog(logPathB, logB.join("\n"));
    if (!okB) {
        fail(errorMessage);
        return;
    }

    emit logMessage("导出完成");
    emit finished(job.outputPathA, job.outputPathB);
}

void ClipExporter::exportBatchSideBySide(const BatchExportJob& job) {
    BatchExportJob sideBySideJob = job;
    sideBySideJob.mode = BatchExportMode::SideBySide;
    exportBatchClips(sideBySideJob);
}

void ClipExporter::exportBatchClips(const BatchExportJob& job) {
    QDir outputDir(job.outputDir);
    if (job.clips.isEmpty()) {
        emit batchFailed("片段队列为空。");
        return;
    }
    if (!outputDir.exists() && !outputDir.mkpath(".")) {
        emit batchFailed("无法创建输出目录: " + outputDir.absolutePath());
        return;
    }

    QString writeTestPath = outputDir.filePath(".dvt_batch_write_test");
    QFile writeTest(writeTestPath);
    if (!writeTest.open(QIODevice::WriteOnly)) {
        emit batchFailed("输出目录不可写: " + outputDir.absolutePath());
        return;
    }
    writeTest.close();
    QFile::remove(writeTestPath);

    QDir logDir(job.logDir.isEmpty() ? outputDir.filePath("logs") : job.logDir);
    if (!logDir.exists() && !logDir.mkpath(".")) {
        emit batchFailed("无法创建日志目录: " + logDir.absolutePath());
        return;
    }

    if (!QFileInfo::exists(job.inputPathA)) {
        emit batchFailed("视频 A 文件不存在: " + job.inputPathA);
        return;
    }
    if (!QFileInfo::exists(job.inputPathB)) {
        emit batchFailed("视频 B 文件不存在: " + job.inputPathB);
        return;
    }

    VideoDecoder decoderA;
    VideoDecoder decoderB;
    if (!decoderA.open(job.inputPathA)) {
        emit batchFailed("无法打开视频 A: " + job.inputPathA);
        return;
    }
    if (!decoderB.open(job.inputPathB)) {
        emit batchFailed("无法打开视频 B: " + job.inputPathB);
        return;
    }

    int totalProgress = 0;
    for (const ClipItem& clip : job.clips) {
        if (clip.outFrame >= clip.inFrame) {
            int clipFrames = static_cast<int>(clip.outFrame - clip.inFrame + 1);
            totalProgress += job.mode == BatchExportMode::SeparateAB ? clipFrames * 2 : clipFrames;
        }
    }
    totalProgress = std::max(1, totalProgress);

    QVector<ClipItem> results = job.clips;
    int progressOffset = 0;
    int exportedCount = 0;
    int failedCount = 0;
    QString timestamp = QDateTime::currentDateTime().toString("yyyy_MM_dd_hhmmss");

    for (int i = 0; i < results.size(); ++i) {
        ClipItem& clip = results[i];
        clip.status = ClipQueue::exportingStatus();
        clip.errorMessage.clear();
        clip.updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        emit clipStatusChanged(clip.id, clip.status, clip.outputPath,
                               clip.outputPathA, clip.outputPathB, clip.errorMessage);
        emit logMessage(QString("正在导出片段 %1/%2 [%3]: %4")
                            .arg(i + 1)
                            .arg(results.size())
                            .arg(batchExportModeLabel(job.mode))
                            .arg(clip.name));

        QString baseName = safeFilePart(clip.name);
        if (baseName.isEmpty()) {
            baseName = QString("clip_%1").arg(i + 1, 3, 10, QChar('0'));
        }

        QString outputPath;
        QString outputPathA;
        QString outputPathB;
        if (job.mode == BatchExportMode::SeparateAB) {
            outputPathA = uniqueMp4Path(outputDir, baseName, clip.inFrame, clip.outFrame, QStringLiteral("a"));
            outputPathB = uniqueMp4Path(outputDir, baseName, clip.inFrame, clip.outFrame, QStringLiteral("b"));
            clip.outputPath.clear();
            clip.outputPathA = outputPathA;
            clip.outputPathB = outputPathB;
        } else {
            outputPath = uniqueMp4Path(outputDir, baseName, clip.inFrame, clip.outFrame, QStringLiteral("sbs"));
            clip.outputPath = outputPath;
            clip.outputPathA.clear();
            clip.outputPathB.clear();
        }

        QStringList logLines;
        logLines << QString("Batch export started: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
        logLines << QString("Mode: %1").arg(batchExportModeId(job.mode));
        logLines << QString("Input A: %1").arg(job.inputPathA);
        logLines << QString("Input B: %1").arg(job.inputPathB);
        if (job.mode == BatchExportMode::SeparateAB) {
            logLines << QString("Output A: %1").arg(outputPathA);
            logLines << QString("Output B: %1").arg(outputPathB);
        } else {
            logLines << QString("Output: %1").arg(outputPath);
        }
        logLines << QString("Note: %1").arg(clip.note);

        QString errorMessage;
        bool ok = false;
        int clipFrames = static_cast<int>(std::max<int64_t>(0, clip.outFrame - clip.inFrame + 1));
        if (clip.outFrame < clip.inFrame) {
            errorMessage = "片段帧范围无效。";
            logLines << "ERROR: Invalid frame range";
        } else if (QThread::currentThread()->isInterruptionRequested()) {
            errorMessage = "导出已取消";
            logLines << "CANCELLED by user";
        } else if (job.mode == BatchExportMode::SeparateAB) {
            logLines << "Exporting video A";
            bool okA = exportOneClip(decoderA, outputPathA, clip.inFrame, clip.outFrame, job.fps,
                                     progressOffset, totalProgress, logLines, errorMessage);
            if (okA) {
                logLines << "Exporting video B";
                ok = exportOneClip(decoderB, outputPathB, clip.inFrame, clip.outFrame, job.fps,
                                   progressOffset + clipFrames, totalProgress, logLines, errorMessage);
            }
        } else {
            ok = exportOneSideBySideClip(decoderA, decoderB, clip, outputPath, job.fps,
                                         progressOffset, totalProgress, logLines, errorMessage);
        }

        progressOffset += job.mode == BatchExportMode::SeparateAB ? clipFrames * 2 : clipFrames;
        QString logPath = logDir.filePath(QString("batch_clip_%1_%2.log")
                                              .arg(i + 1, 3, 10, QChar('0'))
                                              .arg(timestamp));
        writeLog(logPath, logLines.join('\n'));

        if (ok) {
            clip.status = ClipQueue::exportedStatus();
            clip.errorMessage.clear();
            ++exportedCount;
        } else {
            clip.status = ClipQueue::failedStatus();
            clip.errorMessage = errorMessage;
            ++failedCount;
        }
        clip.updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        emit clipStatusChanged(clip.id, clip.status, clip.outputPath,
                               clip.outputPathA, clip.outputPathB, clip.errorMessage);

        if (QThread::currentThread()->isInterruptionRequested()) {
            for (int j = i + 1; j < results.size(); ++j) {
                results[j].status = ClipQueue::failedStatus();
                results[j].errorMessage = "导出已取消";
                results[j].updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
                emit clipStatusChanged(results[j].id, results[j].status, results[j].outputPath,
                                       results[j].outputPathA, results[j].outputPathB, results[j].errorMessage);
                ++failedCount;
            }
            break;
        }
    }

    QString manifestPath = outputDir.filePath("export_manifest.json");
    QString manifestError;
    if (!writeManifest(job, results, manifestPath, manifestError)) {
        emit batchFailed(manifestError);
        return;
    }

    BatchExportSummary summary;
    summary.manifestPath = manifestPath;
    summary.exportedCount = exportedCount;
    summary.failedCount = failedCount;
    emit logMessage(QString("批量导出完成: 成功 %1，失败 %2").arg(exportedCount).arg(failedCount));
    emit batchFinished(summary);
}
