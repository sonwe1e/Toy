#include "smoke/SmokeTest.h"

#include "export/ClipExporter.h"
#include "playback/PlaybackEngine.h"
#include "video/VideoDecoder.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <algorithm>
#include <cstdio>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
void attachConsoleForSmoke() {
#ifdef _WIN32
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* ignored = nullptr;
        freopen_s(&ignored, "CONOUT$", "w", stdout);
        freopen_s(&ignored, "CONOUT$", "w", stderr);
    }
#endif
}

void printLine(const char* level, const QString& message) {
    QByteArray utf8 = message.toUtf8();
    std::fprintf(stderr, "[%s] %s\n", level, utf8.constData());
    std::fflush(stderr);
}

bool hasArg(const QStringList& args, const QString& name) {
    return args.contains(name);
}

QString optionValue(const QStringList& args, const QString& name, const QString& fallback = {}) {
    int index = args.indexOf(name);
    if (index < 0 || index + 1 >= args.size()) {
        return fallback;
    }
    return args.at(index + 1);
}

int optionInt(const QStringList& args, const QString& name, int fallback) {
    bool ok = false;
    int value = optionValue(args, name).toInt(&ok);
    return ok ? value : fallback;
}

bool waitUntil(const std::function<bool()>& condition, int timeoutMs, const QString& label) {
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (timer.elapsed() >= timeoutMs) {
            printLine("FAIL", QString("Timed out waiting for %1 after %2 ms").arg(label).arg(timeoutMs));
            return false;
        }
        QThread::msleep(10);
    }
    return true;
}

bool openSources(PlaybackEngine& engine,
                 const QString& pathA,
                 const QString& pathB,
                 QStringList& errors,
                 int timeoutMs) {
    bool openADone = false;
    bool openBDone = false;
    bool openAOk = false;
    bool openBOk = false;

    QObject::connect(&engine, &PlaybackEngine::openFinishedA, &engine, [&](bool ok) {
        openADone = true;
        openAOk = ok;
    });
    QObject::connect(&engine, &PlaybackEngine::openFinishedB, &engine, [&](bool ok) {
        openBDone = true;
        openBOk = ok;
    });
    QObject::connect(&engine, &PlaybackEngine::errorOccurred, &engine, [&](const QString& message) {
        errors << message;
        printLine("APP", message);
    });

    engine.openVideoA(pathA);
    engine.openVideoB(pathB);

    if (!waitUntil([&]() { return openADone && openBDone; }, timeoutMs, "source open")) {
        return false;
    }

    if (!openAOk || !openBOk || !engine.sourcesReady()) {
        printLine("FAIL", "Source open failed: " + errors.join(" | "));
        return false;
    }

    return true;
}

bool validateExport(const QString& path, int expectedFrames, QString& error) {
    QFileInfo info(path);
    if (!info.exists() || info.size() <= 0) {
        error = "Export output missing or empty: " + path;
        return false;
    }

    VideoDecoder decoder;
    QString decoderError;
    QObject::connect(&decoder, &VideoDecoder::error, &decoder, [&](const QString& message) {
        decoderError = message;
    });

    if (!decoder.open(path)) {
        error = "Export output is not decodable: " + path + " | " + decoderError;
        return false;
    }

    if (!decoder.decodeFrame(0).has_value()) {
        error = "Export output cannot decode first frame: " + path;
        return false;
    }

    int lastFrame = std::max(0, expectedFrames - 1);
    if (!decoder.decodeFrame(lastFrame).has_value()) {
        error = QString("Export output cannot decode expected last frame %1: %2").arg(lastFrame).arg(path);
        return false;
    }

    return true;
}

bool validateBatchManifest(const QString& path, int expectedClips, BatchExportMode expectedMode, QString& error) {
    QFileInfo info(path);
    if (!info.exists() || info.size() <= 0) {
        error = "Batch manifest missing or empty: " + path;
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = "Cannot read batch manifest: " + path;
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        error = "Batch manifest is not valid JSON: " + parseError.errorString();
        return false;
    }

    QJsonObject root = doc.object();
    QString expectedType = expectedMode == BatchExportMode::SeparateAB
        ? QStringLiteral("separate_ab_batch_export")
        : QStringLiteral("side_by_side_batch_export");
    if (root.value("type").toString() != expectedType) {
        error = "Batch manifest type mismatch: " + root.value("type").toString();
        return false;
    }
    if (root.value("exportMode").toString() != batchExportModeId(expectedMode)) {
        error = "Batch manifest exportMode mismatch: " + root.value("exportMode").toString();
        return false;
    }
    QJsonArray clips = root.value("clips").toArray();
    if (clips.size() != expectedClips) {
        error = QString("Batch manifest clip count mismatch: expected %1 got %2").arg(expectedClips).arg(clips.size());
        return false;
    }
    for (const QJsonValue& value : clips) {
        QJsonObject clip = value.toObject();
        if (clip.value("status").toString() != ClipQueue::exportedStatus()) {
            error = "Batch manifest contains a non-exported clip: " + clip.value("name").toString();
            return false;
        }
        if (expectedMode == BatchExportMode::SeparateAB) {
            QString outputPathA = clip.value("outputPathA").toString();
            QString outputPathB = clip.value("outputPathB").toString();
            if (outputPathA.isEmpty() || !QFileInfo::exists(outputPathA)) {
                error = "Batch manifest outputPathA is missing: " + outputPathA;
                return false;
            }
            if (outputPathB.isEmpty() || !QFileInfo::exists(outputPathB)) {
                error = "Batch manifest outputPathB is missing: " + outputPathB;
                return false;
            }
        } else {
            QString outputPath = clip.value("outputPath").toString();
            if (outputPath.isEmpty() || !QFileInfo::exists(outputPath)) {
                error = "Batch manifest output path is missing: " + outputPath;
                return false;
            }
        }
    }
    return true;
}

int runMainSmoke(const QStringList& args) {
    QString pathA = optionValue(args, "--video-a");
    QString pathB = optionValue(args, "--video-b");
    QString outputDirPath = optionValue(args, "--output-dir", QDir::current().filePath("tmp/smoke_output"));
    QString cacheDirPath = optionValue(args, "--cache-dir", QDir(outputDirPath).filePath("proxy_cache"));
    QString ffmpegPath = optionValue(args, "--ffmpeg");
    int timeoutMs = optionInt(args, "--timeout-ms", 120000);
    int proxyHeight = optionInt(args, "--proxy-height", 720);
    int exportStart = optionInt(args, "--export-start", 1);
    int exportEnd = optionInt(args, "--export-end", exportStart + 8);

    if (pathA.isEmpty() || pathB.isEmpty()) {
        printLine("FAIL", "--video-a and --video-b are required");
        return 2;
    }
    if (!QFileInfo::exists(pathA) || !QFileInfo::exists(pathB)) {
        printLine("FAIL", "Input videos must exist");
        return 2;
    }
    if (!ffmpegPath.isEmpty()) {
        qputenv("DUALVIDEOTOOL_FFMPEG", QFile::encodeName(ffmpegPath));
    }

    QDir outputDir(outputDirPath);
    if (!outputDir.exists() && !outputDir.mkpath(".")) {
        printLine("FAIL", "Cannot create smoke output directory: " + outputDir.absolutePath());
        return 2;
    }
    QDir cacheDir(cacheDirPath);
    if (!cacheDir.exists() && !cacheDir.mkpath(".")) {
        printLine("FAIL", "Cannot create smoke proxy cache directory: " + cacheDir.absolutePath());
        return 2;
    }

    PlaybackEngine engine;
    engine.setPerfDiagnosticsEnabled(true);
    ProxySettings settings;
    settings.autoGenerate = false;
    settings.proxyPlaybackOnly = true;
    settings.cacheDir = cacheDir.absolutePath();
    settings.maxHeight = proxyHeight;
    settings.crf = optionInt(args, "--crf", 24);
    settings.preset = optionValue(args, "--preset", "veryfast");
    engine.setProxySettings(settings);

    QStringList errors;
    if (!openSources(engine, pathA, pathB, errors, timeoutMs)) {
        return 1;
    }

    int framePairCount = 0;
    int positionChangedCount = 0;
    int64_t lastPositionUs = 0;
    PlaybackPerfStats lastPerfStats;
    QObject::connect(&engine, &PlaybackEngine::framePairReady, &engine, [&](const FramePair& pair) {
        if (pair.isValid()) {
            ++framePairCount;
        }
    });
    QObject::connect(&engine, &PlaybackEngine::positionChanged, &engine, [&](int64_t positionUs) {
        ++positionChangedCount;
        lastPositionUs = positionUs;
    });
    QObject::connect(&engine, &PlaybackEngine::perfStatsUpdated, &engine, [&](PlaybackPerfStats stats) {
        lastPerfStats = stats;
        printLine("PERF", QString("fps=%1 target=%2 displayed=%3 dropped=%4 repeated=%5 latency=%6/%7ms")
                              .arg(stats.displayedFps, 0, 'f', 1)
                              .arg(stats.targetFrame)
                              .arg(stats.displayedFrame)
                              .arg(stats.droppedTargetCount)
                              .arg(stats.repeatedTargetCount)
                              .arg(stats.averageFrameLatencyMs, 0, 'f', 1)
                              .arg(stats.maxFrameLatencyMs, 0, 'f', 1));
    });

    bool proxyDone = false;
    bool proxyOk = false;
    QString proxyPath;
    QObject::connect(&engine, &PlaybackEngine::proxyLogMessage, &engine, [&](const QString& message) {
        printLine("PROXY", message);
    });
    QObject::connect(&engine, &PlaybackEngine::proxyReady, &engine, [&](const QString& path) {
        proxyDone = true;
        proxyOk = true;
        proxyPath = path;
    });
    QObject::connect(&engine, &PlaybackEngine::proxyFailed, &engine, [&](const QString& message) {
        proxyDone = true;
        proxyOk = false;
        errors << message;
    });

    printLine("INFO", "Building proxy through PlaybackEngine");
    engine.buildProxy(true);
    if (!waitUntil([&]() { return proxyDone; }, timeoutMs, "proxy build/open")) {
        return 1;
    }
    if (!proxyOk || proxyPath.isEmpty()) {
        printLine("FAIL", "Proxy failed: " + errors.join(" | "));
        return 1;
    }
    QFileInfo proxyInfo(proxyPath);
    if (!proxyInfo.exists() || proxyInfo.size() <= 0) {
        printLine("FAIL", "Proxy output missing or empty: " + proxyPath);
        return 1;
    }
    if (!waitUntil([&]() { return framePairCount >= 1; }, timeoutMs, "initial proxy frame")) {
        return 1;
    }

    int beforeSeekPairs = framePairCount;
    int beforeSeekPositionEvents = positionChangedCount;
    int64_t seekTarget = engine.durationUs() / 2;
    engine.seekTo(seekTarget);
    if (positionChangedCount <= beforeSeekPositionEvents || lastPositionUs != seekTarget) {
        printLine("FAIL", "Seek did not emit immediate position feedback");
        return 1;
    }
    if (!waitUntil([&]() { return framePairCount > beforeSeekPairs; }, timeoutMs, "seek frame")) {
        return 1;
    }

    int beforeStepPairs = framePairCount;
    engine.stepFrame(1);
    if (!waitUntil([&]() { return framePairCount > beforeStepPairs; }, timeoutMs, "step frame")) {
        return 1;
    }

    int64_t beforePlayPosition = engine.positionUs();
    int beforePlayPairs = framePairCount;
    engine.play();
    if (!waitUntil([&]() {
            return engine.positionUs() > beforePlayPosition && framePairCount > beforePlayPairs;
        }, timeoutMs, "continuous playback progress")) {
        return 1;
    }
    lastPerfStats = engine.perfStats();
    if (lastPerfStats.displayedFrameCount <= 0 || lastPerfStats.targetFrame < 0) {
        printLine("FAIL", "Playback performance stats did not record displayed/target frames");
        return 1;
    }
    engine.pause();
    PlaybackState pausedState = engine.state();
    int64_t pausedPosition = engine.positionUs();
    QElapsedTimer pauseTimer;
    pauseTimer.start();
    while (pauseTimer.elapsed() < 250) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
    if (pausedState == PlaybackState::Playing || engine.positionUs() != pausedPosition) {
        printLine("FAIL", "Pause did not freeze playback position");
        return 1;
    }

    int64_t frameCount = engine.frameCount();
    if (frameCount <= 0) {
        printLine("FAIL", "Invalid source frame count");
        return 1;
    }
    exportStart = std::clamp(exportStart, 0, static_cast<int>(frameCount - 1));
    exportEnd = std::clamp(exportEnd, exportStart, static_cast<int>(frameCount - 1));

    ExportJob job;
    job.inputPathA = pathA;
    job.inputPathB = pathB;
    job.startFrame = exportStart;
    job.endFrame = exportEnd;
    job.fps = engine.fps();
    job.logDir = outputDir.filePath("logs");
    job.outputPathA = outputDir.filePath(
        QString("smoke_a_%1_%2.mp4").arg(exportStart, 6, 10, QChar('0')).arg(exportEnd, 6, 10, QChar('0')));
    job.outputPathB = outputDir.filePath(
        QString("smoke_b_%1_%2.mp4").arg(exportStart, 6, 10, QChar('0')).arg(exportEnd, 6, 10, QChar('0')));

    if (job.inputPathA == proxyPath || job.inputPathB == proxyPath) {
        printLine("FAIL", "Export job is using proxy path as an input");
        return 1;
    }

    bool exportDone = false;
    bool exportOk = false;
    QString exportError;
    ClipExporter exporter;
    QObject::connect(&exporter, &ClipExporter::logMessage, &exporter, [&](const QString& message) {
        printLine("EXPORT", message);
    });
    QObject::connect(&exporter, &ClipExporter::failed, &exporter, [&](const QString& message) {
        exportDone = true;
        exportOk = false;
        exportError = message;
    });
    QObject::connect(&exporter, &ClipExporter::finished, &exporter, [&](const QString&, const QString&) {
        exportDone = true;
        exportOk = true;
    });

    exporter.exportBothClips(job);
    if (!exportDone || !exportOk) {
        printLine("FAIL", "Export failed: " + exportError);
        return 1;
    }

    QString validationError;
    int expectedFrames = static_cast<int>(job.endFrame - job.startFrame + 1);
    if (!validateExport(job.outputPathA, expectedFrames, validationError) ||
        !validateExport(job.outputPathB, expectedFrames, validationError)) {
        printLine("FAIL", validationError);
        return 1;
    }

    printLine("PASS", "Proxy playback, seek, pause, step, and original-source export smoke test passed");
    printLine("PASS", "Proxy: " + proxyPath);
    printLine("PASS", "Export A: " + job.outputPathA);
    printLine("PASS", "Export B: " + job.outputPathB);
    return 0;
}

int runSingleBatchExportSmoke(const QString& pathA,
                              const QString& pathB,
                              const QString& outputDirPath,
                              int batchStart,
                              int clipCount,
                              int clipLength,
                              BatchExportMode mode) {
    QDir outputDir(outputDirPath);
    if (!outputDir.exists() && !outputDir.mkpath(".")) {
        printLine("FAIL", "Cannot create batch smoke output directory: " + outputDir.absolutePath());
        return 2;
    }

    VideoDecoder decoderA;
    VideoDecoder decoderB;
    QString decoderError;
    QObject::connect(&decoderA, &VideoDecoder::error, &decoderA, [&](const QString& message) {
        decoderError = "A: " + message;
    });
    QObject::connect(&decoderB, &VideoDecoder::error, &decoderB, [&](const QString& message) {
        decoderError = "B: " + message;
    });
    if (!decoderA.open(pathA) || !decoderB.open(pathB)) {
        printLine("FAIL", "Cannot open input videos for batch smoke: " + decoderError);
        return 1;
    }

    VideoMetadata metaA = decoderA.metadata();
    VideoMetadata metaB = decoderB.metadata();
    int64_t frameCount = std::min(metaA.frameCount, metaB.frameCount);
    int64_t requiredFrames = static_cast<int64_t>(batchStart) + static_cast<int64_t>(clipCount) * clipLength;
    if (frameCount < requiredFrames) {
        printLine("FAIL", QString("Input videos are too short for batch smoke: need %1 frames, got %2")
                              .arg(requiredFrames)
                              .arg(frameCount));
        return 2;
    }

    BatchExportJob job;
    job.inputPathA = pathA;
    job.inputPathB = pathB;
    job.outputDir = outputDir.absolutePath();
    job.logDir = outputDir.filePath("logs");
    job.fps = metaA.fps > 0.0 ? metaA.fps : 30.0;
    job.mode = mode;

    for (int i = 0; i < clipCount; ++i) {
        int64_t start = batchStart + static_cast<int64_t>(i) * clipLength;
        int64_t end = start + clipLength - 1;
        ClipItem item;
        item.id = QString("smoke_clip_%1").arg(i + 1, 3, 10, QChar('0'));
        item.name = QString("clip_%1").arg(i + 1, 3, 10, QChar('0'));
        item.inFrame = start;
        item.outFrame = end;
        item.inUs = static_cast<int64_t>((start / job.fps) * 1000000.0);
        item.outUs = static_cast<int64_t>((end / job.fps) * 1000000.0);
        item.note = QString("batch smoke clip %1").arg(i + 1);
        item.status = ClipQueue::pendingStatus();
        item.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        item.updatedAt = item.createdAt;
        job.clips.push_back(item);
    }

    bool batchDone = false;
    bool batchOk = false;
    QString batchError;
    BatchExportSummary summary;
    QHash<QString, ClipItem> statusById;
    ClipExporter exporter;
    QObject::connect(&exporter, &ClipExporter::logMessage, &exporter, [&](const QString& message) {
        printLine("BATCH", QString("[%1] %2").arg(batchExportModeId(mode), message));
    });
    QObject::connect(&exporter, &ClipExporter::clipStatusChanged, &exporter,
        [&](const QString& clipId,
            const QString& status,
            const QString& outputPath,
            const QString& outputPathA,
            const QString& outputPathB,
            const QString& errorMessage) {
            ClipItem item;
            item.id = clipId;
            item.status = status;
            item.outputPath = outputPath;
            item.outputPathA = outputPathA;
            item.outputPathB = outputPathB;
            item.errorMessage = errorMessage;
            statusById.insert(clipId, item);
        });
    QObject::connect(&exporter, &ClipExporter::batchFailed, &exporter, [&](const QString& message) {
        batchDone = true;
        batchOk = false;
        batchError = message;
    });
    QObject::connect(&exporter, &ClipExporter::batchFinished, &exporter, [&](const BatchExportSummary& result) {
        batchDone = true;
        batchOk = true;
        summary = result;
    });

    exporter.exportBatchClips(job);
    if (!batchDone || !batchOk) {
        printLine("FAIL", QString("Batch export failed for %1: %2").arg(batchExportModeId(mode), batchError));
        return 1;
    }
    if (summary.exportedCount != clipCount || summary.failedCount != 0) {
        printLine("FAIL", QString("Batch export summary mismatch for %1: exported %2 failed %3")
                              .arg(batchExportModeId(mode))
                              .arg(summary.exportedCount)
                              .arg(summary.failedCount));
        return 1;
    }

    QString validationError;
    for (const ClipItem& sourceClip : job.clips) {
        ClipItem result = statusById.value(sourceClip.id);
        if (result.status != ClipQueue::exportedStatus()) {
            printLine("FAIL", "Clip did not reach exported status: " + sourceClip.id + " | " + result.errorMessage);
            return 1;
        }
        int expectedFrames = static_cast<int>(sourceClip.outFrame - sourceClip.inFrame + 1);
        if (mode == BatchExportMode::SeparateAB) {
            if (result.outputPathA == pathA || result.outputPathA == pathB ||
                result.outputPathB == pathA || result.outputPathB == pathB) {
                printLine("FAIL", "Separate batch export output path points at an input video");
                return 1;
            }
            if (result.outputPathA == result.outputPathB) {
                printLine("FAIL", "Separate batch export produced identical A/B output paths");
                return 1;
            }
            if (!validateExport(result.outputPathA, expectedFrames, validationError) ||
                !validateExport(result.outputPathB, expectedFrames, validationError)) {
                printLine("FAIL", validationError);
                return 1;
            }
        } else {
            if (result.outputPath == pathA || result.outputPath == pathB) {
                printLine("FAIL", "Batch export output path points at an input video");
                return 1;
            }
            if (!validateExport(result.outputPath, expectedFrames, validationError)) {
                printLine("FAIL", validationError);
                return 1;
            }
        }
    }

    if (!validateBatchManifest(summary.manifestPath, clipCount, mode, validationError)) {
        printLine("FAIL", validationError);
        return 1;
    }

    printLine("PASS", QString("Batch %1 original-source export smoke test passed").arg(batchExportModeId(mode)));
    printLine("PASS", "Manifest: " + summary.manifestPath);
    return 0;
}

int runBatchExportSmoke(const QStringList& args) {
    QString pathA = optionValue(args, "--video-a");
    QString pathB = optionValue(args, "--video-b");
    QString outputDirPath = optionValue(args, "--output-dir", QDir::current().filePath("tmp/smoke_batch_output"));
    QString modeArg = optionValue(args, "--batch-mode", "both").trimmed().toLower();
    int batchStart = optionInt(args, "--batch-start", 0);
    int clipCount = optionInt(args, "--batch-clips", 3);
    int clipLength = optionInt(args, "--batch-length", 3);

    if (pathA.isEmpty() || pathB.isEmpty()) {
        printLine("FAIL", "--video-a and --video-b are required");
        return 2;
    }
    if (!QFileInfo::exists(pathA) || !QFileInfo::exists(pathB)) {
        printLine("FAIL", "Input videos must exist");
        return 2;
    }
    if (clipCount <= 0 || clipLength <= 0) {
        printLine("FAIL", "--batch-clips and --batch-length must be positive");
        return 2;
    }

    QVector<BatchExportMode> modes;
    if (modeArg == "both") {
        modes.push_back(BatchExportMode::SideBySide);
        modes.push_back(BatchExportMode::SeparateAB);
    } else if (modeArg == batchExportModeId(BatchExportMode::SideBySide)) {
        modes.push_back(BatchExportMode::SideBySide);
    } else if (modeArg == batchExportModeId(BatchExportMode::SeparateAB)) {
        modes.push_back(BatchExportMode::SeparateAB);
    } else {
        printLine("FAIL", "Unknown --batch-mode. Expected side_by_side, separate_ab, or both");
        return 2;
    }

    QDir baseOutputDir(outputDirPath);
    for (BatchExportMode mode : modes) {
        QString modeOutputDir = modes.size() > 1
            ? baseOutputDir.filePath(batchExportModeId(mode))
            : outputDirPath;
        int result = runSingleBatchExportSmoke(pathA, pathB, modeOutputDir,
                                               batchStart, clipCount, clipLength, mode);
        if (result != 0) {
            return result;
        }
    }
    return 0;
}

int runMissingInputFailure(const QStringList& args) {
    QString missingPath = optionValue(args, "--missing-path", QDir::current().filePath("tmp/does_not_exist.mp4"));
    int timeoutMs = optionInt(args, "--timeout-ms", 30000);

    PlaybackEngine engine;
    ProxySettings settings;
    settings.autoGenerate = false;
    engine.setProxySettings(settings);

    bool openDone = false;
    bool openOk = true;
    QStringList errors;
    QObject::connect(&engine, &PlaybackEngine::openFinishedA, &engine, [&](bool ok) {
        openDone = true;
        openOk = ok;
    });
    QObject::connect(&engine, &PlaybackEngine::errorOccurred, &engine, [&](const QString& message) {
        errors << message;
        printLine("APP", message);
    });

    engine.openVideoA(missingPath);
    if (!waitUntil([&]() { return openDone; }, timeoutMs, "missing input failure")) {
        return 1;
    }
    QString combined = errors.join(" | ");
    if (openOk || !combined.contains(QStringLiteral("无法打开视频文件"))) {
        printLine("FAIL", "Missing-input diagnostic was not clear: " + combined);
        return 1;
    }
    printLine("PASS", "Missing input failure produced a clear Chinese diagnostic");
    return 0;
}

int runProxyFailure(const QStringList& args, bool missingFfmpeg) {
    QString pathA = optionValue(args, "--video-a");
    QString pathB = optionValue(args, "--video-b");
    QString outputDirPath = optionValue(args, "--output-dir", QDir::current().filePath("tmp/smoke_failure"));
    int timeoutMs = optionInt(args, "--timeout-ms", 60000);

    if (pathA.isEmpty() || pathB.isEmpty()) {
        printLine("FAIL", "--video-a and --video-b are required");
        return 2;
    }

    QDir outputDir(outputDirPath);
    if (!outputDir.exists() && !outputDir.mkpath(".")) {
        printLine("FAIL", "Cannot create failure-test output directory: " + outputDir.absolutePath());
        return 2;
    }

    QFile cacheFile(outputDir.filePath("cache_path_is_a_file"));
    if (!missingFfmpeg) {
        if (!cacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            printLine("FAIL", "Cannot create bad-cache test file: " + cacheFile.fileName());
            return 2;
        }
        cacheFile.write("not a directory");
        cacheFile.close();
    }

    if (missingFfmpeg) {
        qputenv("DUALVIDEOTOOL_FFMPEG", QFile::encodeName(outputDir.filePath("missing_ffmpeg.exe")));
    }

    PlaybackEngine engine;
    ProxySettings settings;
    settings.autoGenerate = false;
    settings.proxyPlaybackOnly = true;
    settings.cacheDir = missingFfmpeg ? outputDir.filePath("cache") : cacheFile.fileName();
    settings.maxHeight = 240;
    engine.setProxySettings(settings);

    QStringList errors;
    if (!openSources(engine, pathA, pathB, errors, timeoutMs)) {
        return 1;
    }

    bool proxyDone = false;
    QString failureMessage;
    QObject::connect(&engine, &PlaybackEngine::proxyFailed, &engine, [&](const QString& message) {
        proxyDone = true;
        failureMessage = message;
        printLine("APP", message);
    });

    engine.buildProxy(true);
    if (!waitUntil([&]() { return proxyDone; }, timeoutMs, "expected proxy failure")) {
        return 1;
    }

    if (missingFfmpeg) {
        if (!failureMessage.contains(QStringLiteral("ffmpeg.exe"))) {
            printLine("FAIL", "Missing-ffmpeg diagnostic was not clear: " + failureMessage);
            return 1;
        }
        printLine("PASS", "Missing ffmpeg failure produced a clear Chinese diagnostic");
        return 0;
    }

    if (!failureMessage.contains(QStringLiteral("代理缓存"))) {
        printLine("FAIL", "Bad-cache diagnostic was not clear: " + failureMessage);
        return 1;
    }
    printLine("PASS", "Bad cache path failure produced a clear Chinese diagnostic");
    return 0;
}

int runFailureSmoke(const QStringList& args) {
    QString failureCase = optionValue(args, "--case");
    if (failureCase == "missing-input") {
        return runMissingInputFailure(args);
    }
    if (failureCase == "missing-ffmpeg") {
        return runProxyFailure(args, true);
    }
    if (failureCase == "bad-cache") {
        return runProxyFailure(args, false);
    }

    printLine("FAIL", "Unknown --case. Expected missing-input, missing-ffmpeg, or bad-cache");
    return 2;
}
}

namespace SmokeTest {
int run(const QStringList& arguments) {
    attachConsoleForSmoke();

    if (hasArg(arguments, "--smoke-failure-test")) {
        return runFailureSmoke(arguments);
    }
    if (hasArg(arguments, "--smoke-batch-export")) {
        return runBatchExportSmoke(arguments);
    }
    return runMainSmoke(arguments);
}
}
