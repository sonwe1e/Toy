#pragma once

#include "video/VideoMetadata.h"

#include <QObject>
#include <QProcess>
#include <QString>

enum class ProxyStatus {
    Idle,
    PreparingProxy,
    ProxyReady,
    ProxyFailed
};

Q_DECLARE_METATYPE(ProxyStatus)

struct ProxySettings {
    int maxHeight = 720;
    int crf = 24;
    QString preset = "veryfast";
    QString cacheDir;
    bool autoGenerate = true;
    bool proxyPlaybackOnly = true;
};

Q_DECLARE_METATYPE(ProxySettings)

class ProxyBuilder : public QObject {
    Q_OBJECT

public:
    explicit ProxyBuilder(QObject* parent = nullptr);
    ~ProxyBuilder() override;

    static QString defaultCacheDir();
    static QString ffmpegExecutable();
    static QString proxyPathFor(const QString& pathA,
                                const QString& pathB,
                                const VideoMetadata& metaA,
                                const VideoMetadata& metaB,
                                const ProxySettings& settings);

    bool isRunning() const;
    void build(const QString& pathA,
               const QString& pathB,
               const VideoMetadata& metaA,
               const VideoMetadata& metaB,
               const QString& outputPath,
               const ProxySettings& settings);
    void cancel();

signals:
    void started(QString commandLine);
    void logMessage(QString message);
    void finished(QString outputPath);
    void failed(QString message);

private:
    void handleReadyRead();
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleError(QProcess::ProcessError error);
    void failOnce(const QString& message);

    QProcess* process_ = nullptr;
    QString outputPath_;
    QString logBuffer_;
    bool completed_ = false;
};
