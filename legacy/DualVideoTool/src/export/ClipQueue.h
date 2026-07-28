#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>

struct ClipItem {
    QString id;
    QString name;
    int64_t inUs = 0;
    int64_t outUs = 0;
    int64_t inFrame = 0;
    int64_t outFrame = 0;
    QString note;
    QString status;
    QString outputPath;
    QString outputPathA;
    QString outputPathB;
    QString errorMessage;
    QString createdAt;
    QString updatedAt;
};

class ClipQueue {
public:
    enum class LoadResult {
        NoSources,
        NoSession,
        Loaded,
        SourceMismatch,
        ReadError
    };

    ClipQueue();

    static QString pendingStatus();
    static QString exportingStatus();
    static QString exportedStatus();
    static QString failedStatus();
    static QStringList statuses();

    void setSources(const QString& pathA, const QString& pathB);
    void clearInMemory();

    LoadResult load(QString* message = nullptr);
    bool save(QString* errorMessage = nullptr) const;

    const QVector<ClipItem>& items() const;
    int size() const;
    bool isEmpty() const;
    QString sessionFilePath() const;
    QString sessionKey() const;

    ClipItem addClip(int64_t inUs, int64_t outUs, int64_t inFrame, int64_t outFrame);
    bool removeAt(int row);
    int clearExported();
    ClipItem* findById(const QString& id);
    const ClipItem* findById(const QString& id) const;

private:
    struct SourceSnapshot {
        QString path;
        qint64 size = 0;
        qint64 modifiedMs = 0;
    };

    static SourceSnapshot snapshotFor(const QString& path);
    static QString nowIso();
    static ClipItem fromJson(const QJsonObject& object);
    static QJsonObject toJson(const ClipItem& item);
    static QJsonObject sourceToJson(const SourceSnapshot& source);
    static SourceSnapshot sourceFromJson(const QJsonObject& object);
    static bool sourceMatches(const SourceSnapshot& expected, const SourceSnapshot& actual);

    QString pathA_;
    QString pathB_;
    SourceSnapshot sourceA_;
    SourceSnapshot sourceB_;
    QVector<ClipItem> items_;
};
