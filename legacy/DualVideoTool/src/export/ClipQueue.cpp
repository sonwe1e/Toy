#include "export/ClipQueue.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>

namespace {
constexpr int kSessionVersion = 1;

QString appDataRoot() {
    QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty()) {
        root = QDir::temp().filePath("DualVideoTool");
    }
    return root;
}
}

ClipQueue::ClipQueue() = default;

QString ClipQueue::pendingStatus() {
    return QStringLiteral("待导出");
}

QString ClipQueue::exportingStatus() {
    return QStringLiteral("导出中");
}

QString ClipQueue::exportedStatus() {
    return QStringLiteral("已导出");
}

QString ClipQueue::failedStatus() {
    return QStringLiteral("失败");
}

QStringList ClipQueue::statuses() {
    return {pendingStatus(), exportingStatus(), exportedStatus(), failedStatus()};
}

void ClipQueue::setSources(const QString& pathA, const QString& pathB) {
    pathA_ = QFileInfo(pathA).absoluteFilePath();
    pathB_ = QFileInfo(pathB).absoluteFilePath();
    sourceA_ = snapshotFor(pathA_);
    sourceB_ = snapshotFor(pathB_);
}

void ClipQueue::clearInMemory() {
    items_.clear();
}

ClipQueue::LoadResult ClipQueue::load(QString* message) {
    items_.clear();
    if (pathA_.isEmpty() || pathB_.isEmpty()) {
        if (message) *message = QStringLiteral("尚未加载视频 A/B。");
        return LoadResult::NoSources;
    }

    QString path = sessionFilePath();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        if (message) *message = QStringLiteral("没有可恢复的片段队列。");
        return LoadResult::NoSession;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (message) *message = QStringLiteral("无法读取片段队列: %1").arg(path);
        return LoadResult::ReadError;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (message) *message = QStringLiteral("片段队列 JSON 无效: %1").arg(parseError.errorString());
        return LoadResult::ReadError;
    }

    QJsonObject root = doc.object();
    QJsonObject sources = root.value(QStringLiteral("sources")).toObject();
    SourceSnapshot savedA = sourceFromJson(sources.value(QStringLiteral("a")).toObject());
    SourceSnapshot savedB = sourceFromJson(sources.value(QStringLiteral("b")).toObject());
    if (!sourceMatches(savedA, sourceA_) || !sourceMatches(savedB, sourceB_)) {
        if (message) {
            *message = QStringLiteral("已找到片段队列，但源文件路径、大小或修改时间不匹配。请重新加载对应的视频后再恢复。");
        }
        return LoadResult::SourceMismatch;
    }

    QVector<ClipItem> loaded;
    QJsonArray clips = root.value(QStringLiteral("clips")).toArray();
    loaded.reserve(clips.size());
    for (const QJsonValue& value : clips) {
        if (value.isObject()) {
            ClipItem item = fromJson(value.toObject());
            if (!item.id.isEmpty()) {
                loaded.push_back(item);
            }
        }
    }

    items_ = loaded;
    if (message) *message = QStringLiteral("已恢复 %1 个片段。").arg(items_.size());
    return LoadResult::Loaded;
}

bool ClipQueue::save(QString* errorMessage) const {
    if (pathA_.isEmpty() || pathB_.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("尚未加载视频 A/B。");
        return false;
    }

    QString path = sessionFilePath();
    QDir dir(QFileInfo(path).absolutePath());
    if (!dir.exists() && !dir.mkpath(".")) {
        if (errorMessage) *errorMessage = QStringLiteral("无法创建片段队列目录: %1").arg(dir.absolutePath());
        return false;
    }

    QJsonObject sources;
    sources.insert(QStringLiteral("a"), sourceToJson(sourceA_));
    sources.insert(QStringLiteral("b"), sourceToJson(sourceB_));

    QJsonArray clips;
    for (const ClipItem& item : items_) {
        clips.push_back(toJson(item));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kSessionVersion);
    root.insert(QStringLiteral("sessionKey"), sessionKey());
    root.insert(QStringLiteral("sources"), sources);
    root.insert(QStringLiteral("updatedAt"), nowIso());
    root.insert(QStringLiteral("clips"), clips);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法写入片段队列: %1").arg(path);
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

const QVector<ClipItem>& ClipQueue::items() const {
    return items_;
}

int ClipQueue::size() const {
    return items_.size();
}

bool ClipQueue::isEmpty() const {
    return items_.isEmpty();
}

QString ClipQueue::sessionFilePath() const {
    QString key = sessionKey();
    if (key.isEmpty()) return {};
    return QDir(appDataRoot()).filePath(QStringLiteral("clip_sessions/%1.json").arg(key));
}

QString ClipQueue::sessionKey() const {
    if (pathA_.isEmpty() || pathB_.isEmpty()) return {};
    QString keySource = QStringList{pathA_, pathB_}.join('|');
    QByteArray hash = QCryptographicHash::hash(keySource.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(hash.left(24));
}

ClipItem ClipQueue::addClip(int64_t inUs, int64_t outUs, int64_t inFrame, int64_t outFrame) {
    ClipItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.name = QStringLiteral("clip_%1").arg(items_.size() + 1, 3, 10, QChar('0'));
    item.inUs = inUs;
    item.outUs = outUs;
    item.inFrame = inFrame;
    item.outFrame = outFrame;
    item.status = pendingStatus();
    item.createdAt = nowIso();
    item.updatedAt = item.createdAt;
    items_.push_back(item);
    return item;
}

bool ClipQueue::removeAt(int row) {
    if (row < 0 || row >= items_.size()) return false;
    items_.removeAt(row);
    return true;
}

int ClipQueue::clearExported() {
    int removed = 0;
    for (int i = items_.size() - 1; i >= 0; --i) {
        if (items_.at(i).status == exportedStatus()) {
            items_.removeAt(i);
            ++removed;
        }
    }
    return removed;
}

ClipItem* ClipQueue::findById(const QString& id) {
    for (ClipItem& item : items_) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

const ClipItem* ClipQueue::findById(const QString& id) const {
    for (const ClipItem& item : items_) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

ClipQueue::SourceSnapshot ClipQueue::snapshotFor(const QString& path) {
    QFileInfo info(path);
    SourceSnapshot snapshot;
    snapshot.path = info.absoluteFilePath();
    snapshot.size = info.exists() ? info.size() : -1;
    snapshot.modifiedMs = info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1;
    return snapshot;
}

QString ClipQueue::nowIso() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

ClipItem ClipQueue::fromJson(const QJsonObject& object) {
    ClipItem item;
    item.id = object.value(QStringLiteral("id")).toString();
    item.name = object.value(QStringLiteral("name")).toString();
    item.inUs = static_cast<int64_t>(object.value(QStringLiteral("inUs")).toDouble());
    item.outUs = static_cast<int64_t>(object.value(QStringLiteral("outUs")).toDouble());
    item.inFrame = static_cast<int64_t>(object.value(QStringLiteral("inFrame")).toDouble());
    item.outFrame = static_cast<int64_t>(object.value(QStringLiteral("outFrame")).toDouble());
    item.note = object.value(QStringLiteral("note")).toString();
    item.status = object.value(QStringLiteral("status")).toString(pendingStatus());
    item.outputPath = object.value(QStringLiteral("outputPath")).toString();
    item.outputPathA = object.value(QStringLiteral("outputPathA")).toString();
    item.outputPathB = object.value(QStringLiteral("outputPathB")).toString();
    item.errorMessage = object.value(QStringLiteral("errorMessage")).toString();
    item.createdAt = object.value(QStringLiteral("createdAt")).toString();
    item.updatedAt = object.value(QStringLiteral("updatedAt")).toString();
    if (item.name.isEmpty()) {
        item.name = QStringLiteral("clip");
    }
    if (item.status.isEmpty()) {
        item.status = pendingStatus();
    }
    return item;
}

QJsonObject ClipQueue::toJson(const ClipItem& item) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), item.id);
    object.insert(QStringLiteral("name"), item.name);
    object.insert(QStringLiteral("inUs"), static_cast<double>(item.inUs));
    object.insert(QStringLiteral("outUs"), static_cast<double>(item.outUs));
    object.insert(QStringLiteral("inFrame"), static_cast<double>(item.inFrame));
    object.insert(QStringLiteral("outFrame"), static_cast<double>(item.outFrame));
    object.insert(QStringLiteral("note"), item.note);
    object.insert(QStringLiteral("status"), item.status);
    object.insert(QStringLiteral("outputPath"), item.outputPath);
    object.insert(QStringLiteral("outputPathA"), item.outputPathA);
    object.insert(QStringLiteral("outputPathB"), item.outputPathB);
    object.insert(QStringLiteral("errorMessage"), item.errorMessage);
    object.insert(QStringLiteral("createdAt"), item.createdAt);
    object.insert(QStringLiteral("updatedAt"), item.updatedAt);
    return object;
}

QJsonObject ClipQueue::sourceToJson(const SourceSnapshot& source) {
    QJsonObject object;
    object.insert(QStringLiteral("path"), source.path);
    object.insert(QStringLiteral("size"), static_cast<double>(source.size));
    object.insert(QStringLiteral("modifiedMs"), static_cast<double>(source.modifiedMs));
    return object;
}

ClipQueue::SourceSnapshot ClipQueue::sourceFromJson(const QJsonObject& object) {
    SourceSnapshot source;
    source.path = QFileInfo(object.value(QStringLiteral("path")).toString()).absoluteFilePath();
    source.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
    source.modifiedMs = static_cast<qint64>(object.value(QStringLiteral("modifiedMs")).toDouble());
    return source;
}

bool ClipQueue::sourceMatches(const SourceSnapshot& expected, const SourceSnapshot& actual) {
    return expected.path == actual.path &&
           expected.size == actual.size &&
           expected.modifiedMs == actual.modifiedMs;
}
