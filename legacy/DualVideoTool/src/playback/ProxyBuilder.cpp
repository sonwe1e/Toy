#include "playback/ProxyBuilder.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

namespace {
int evenAtLeastTwo(int value) {
    value = std::max(2, value);
    if (value % 2 != 0) {
        --value;
    }
    return std::max(2, value);
}

int scaledWidthForHeight(const VideoMetadata& metadata, int height) {
    if (metadata.width <= 0 || metadata.height <= 0) {
        return evenAtLeastTwo(height * 16 / 9);
    }
    double ratio = static_cast<double>(metadata.width) / static_cast<double>(metadata.height);
    return evenAtLeastTwo(static_cast<int>(std::llround(height * ratio)));
}

QString quoteForLog(const QString& value) {
    QString escaped = value;
    escaped.replace('"', "\\\"");
    return '"' + escaped + '"';
}
}

ProxyBuilder::ProxyBuilder(QObject* parent)
    : QObject(parent) {
}

ProxyBuilder::~ProxyBuilder() {
    cancel();
}

QString ProxyBuilder::defaultCacheDir() {
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheDir.isEmpty()) {
        cacheDir = QDir::temp().filePath("DualVideoToolProxyCache");
    }
    return QDir(cacheDir).filePath("proxies");
}

QString ProxyBuilder::ffmpegExecutable() {
    QByteArray overridePath = qgetenv("DUALVIDEOTOOL_FFMPEG");
    if (!overridePath.isEmpty()) {
        QString path = QString::fromLocal8Bit(overridePath);
        return QFileInfo::exists(path) ? path : QString();
    }

    QDir appDir(QCoreApplication::applicationDirPath());
    QString bundled = appDir.filePath("tools/ffmpeg.exe");
    if (QFileInfo::exists(bundled)) {
        return bundled;
    }

    if (qEnvironmentVariableIsSet("DUALVIDEOTOOL_DISABLE_PATH_FFMPEG")) {
        return {};
    }

    QString pathExe = QStandardPaths::findExecutable("ffmpeg.exe");
    if (!pathExe.isEmpty()) {
        return pathExe;
    }

    return QStandardPaths::findExecutable("ffmpeg");
}

QString ProxyBuilder::proxyPathFor(const QString& pathA,
                                   const QString& pathB,
                                   const VideoMetadata& metaA,
                                   const VideoMetadata& metaB,
                                   const ProxySettings& settings) {
    QString cacheDir = settings.cacheDir.isEmpty() ? defaultCacheDir() : settings.cacheDir;
    QFileInfo infoA(pathA);
    QFileInfo infoB(pathB);

    QString key = QStringList{
        infoA.absoluteFilePath(),
        QString::number(infoA.size()),
        QString::number(infoA.lastModified().toMSecsSinceEpoch()),
        infoB.absoluteFilePath(),
        QString::number(infoB.size()),
        QString::number(infoB.lastModified().toMSecsSinceEpoch()),
        QString::number(metaA.width),
        QString::number(metaA.height),
        QString::number(metaA.fps, 'f', 6),
        QString::number(metaB.width),
        QString::number(metaB.height),
        QString::number(metaB.fps, 'f', 6),
        QString::number(settings.maxHeight),
        QString::number(settings.crf),
        settings.preset
    }.join('|');

    QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(cacheDir).filePath(QString("proxy_%1.mp4").arg(QString::fromLatin1(hash.left(24))));
}

bool ProxyBuilder::isRunning() const {
    return process_ && process_->state() != QProcess::NotRunning;
}

void ProxyBuilder::build(const QString& pathA,
                         const QString& pathB,
                         const VideoMetadata& metaA,
                         const VideoMetadata& metaB,
                         const QString& outputPath,
                         const ProxySettings& settings) {
    cancel();
    completed_ = false;
    outputPath_ = outputPath;
    logBuffer_.clear();

    QString ffmpeg = ffmpegExecutable();
    if (ffmpeg.isEmpty()) {
        failOnce("未找到 ffmpeg.exe。请将 ffmpeg.exe 放到程序目录的 tools 文件夹，或把 ffmpeg 加入 PATH。");
        return;
    }
    if (!QFileInfo::exists(pathA)) {
        failOnce("视频 A 文件不存在: " + pathA);
        return;
    }
    if (!QFileInfo::exists(pathB)) {
        failOnce("视频 B 文件不存在: " + pathB);
        return;
    }

    QDir outDir(QFileInfo(outputPath).absolutePath());
    if (!outDir.exists() && !outDir.mkpath(".")) {
        failOnce("无法创建代理缓存目录: " + outDir.absolutePath());
        return;
    }

    QString writeTestPath = outDir.filePath(".proxy_write_test");
    QFile writeTest(writeTestPath);
    if (!writeTest.open(QIODevice::WriteOnly)) {
        failOnce("代理缓存目录不可写: " + outDir.absolutePath());
        return;
    }
    writeTest.close();
    QFile::remove(writeTestPath);

    int targetHeight = settings.maxHeight > 0 ? settings.maxHeight : 720;
    if (metaA.height > 0 && metaB.height > 0) {
        targetHeight = std::min(targetHeight, std::min(metaA.height, metaB.height));
    }
    targetHeight = evenAtLeastTwo(targetHeight);
    int widthA = scaledWidthForHeight(metaA, targetHeight);
    int widthB = scaledWidthForHeight(metaB, targetHeight);
    int halfWidth = evenAtLeastTwo(std::max(widthA, widthB));
    double fps = metaA.fps > 0.0 ? metaA.fps : 30.0;

    QString filter = QString(
        "[0:v]scale=%1:%2:flags=bicubic,pad=%3:%2:(ow-iw)/2:(oh-ih)/2:black,setsar=1[left];"
        "[1:v]scale=%4:%2:flags=bicubic,pad=%3:%2:(ow-iw)/2:(oh-ih)/2:black,setsar=1[right];"
        "[left][right]hstack=inputs=2:shortest=1,fps=%5[v]")
        .arg(widthA)
        .arg(targetHeight)
        .arg(halfWidth)
        .arg(widthB)
        .arg(fps, 0, 'f', 3);

    QStringList args{
        "-y",
        "-hide_banner",
        "-i", pathA,
        "-i", pathB,
        "-filter_complex", filter,
        "-map", "[v]",
        "-an",
        "-c:v", "libx264",
        "-preset", settings.preset.isEmpty() ? "veryfast" : settings.preset,
        "-crf", QString::number(std::clamp(settings.crf, 16, 36)),
        "-pix_fmt", "yuv420p",
        "-movflags", "+faststart",
        "-shortest",
        outputPath
    };

    QStringList quotedArgs;
    quotedArgs.reserve(args.size());
    for (const QString& arg : args) {
        quotedArgs << quoteForLog(arg);
    }
    emit started(quoteForLog(ffmpeg) + " " + quotedArgs.join(' '));

    process_ = new QProcess(this);
    process_->setProgram(ffmpeg);
    process_->setArguments(args);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QDir appDir(QCoreApplication::applicationDirPath());
    QStringList pathParts{
        appDir.absolutePath(),
        appDir.filePath("tools"),
        env.value("PATH")
    };
    env.insert("PATH", pathParts.join(QDir::listSeparator()));
    process_->setProcessEnvironment(env);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    connect(process_, &QProcess::readyReadStandardOutput, this, &ProxyBuilder::handleReadyRead);
    connect(process_, &QProcess::readyReadStandardError, this, &ProxyBuilder::handleReadyRead);
    connect(process_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ProxyBuilder::handleFinished);
    connect(process_, &QProcess::errorOccurred, this, &ProxyBuilder::handleError);
    process_->start();
}

void ProxyBuilder::cancel() {
    if (!process_) return;

    if (process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(3000);
    }
    process_->deleteLater();
    process_ = nullptr;
}

void ProxyBuilder::handleReadyRead() {
    if (!process_) return;

    QByteArray data = process_->readAll();
    if (data.isEmpty()) return;

    logBuffer_ += QString::fromLocal8Bit(data);
    QStringList lines = logBuffer_.split(QRegularExpression("[\r\n]+"));
    if (!logBuffer_.endsWith('\n') && !logBuffer_.endsWith('\r')) {
        logBuffer_ = lines.takeLast();
    } else {
        logBuffer_.clear();
    }

    for (const QString& rawLine : lines) {
        QString line = rawLine.trimmed();
        if (!line.isEmpty()) {
            emit logMessage(line);
        }
    }
}

void ProxyBuilder::handleFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    if (!process_) return;
    handleReadyRead();

    QProcess* doneProcess = process_;
    process_ = nullptr;
    doneProcess->deleteLater();

    if (completed_) return;
    completed_ = true;

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        emit failed(QString("代理生成失败，ffmpeg 退出码: %1").arg(exitCode));
        return;
    }

    QFileInfo outInfo(outputPath_);
    if (!outInfo.exists() || outInfo.size() <= 0) {
        emit failed("代理生成失败：输出文件为空或不存在。");
        return;
    }

    emit finished(outputPath_);
}

void ProxyBuilder::handleError(QProcess::ProcessError error) {
    Q_UNUSED(error)
    if (completed_) return;
    QString message = process_ ? process_->errorString() : QString("未知错误");
    failOnce("无法启动 ffmpeg: " + message);
}

void ProxyBuilder::failOnce(const QString& message) {
    if (completed_) return;
    completed_ = true;
    emit failed(message);
}
