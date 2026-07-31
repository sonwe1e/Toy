#include "BadCaseExporter.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace dvs::ui::internal {
namespace {

[[nodiscard]] QString matchKindName(const application::FrameMatchKind kind) {
    switch (kind) {
    case application::FrameMatchKind::ExactIndex:
        return QStringLiteral("exact-index");
    case application::FrameMatchKind::GlobalOffset:
        return QStringLiteral("global-offset");
    case application::FrameMatchKind::AutoAligned:
        return QStringLiteral("auto-aligned");
    case application::FrameMatchKind::ManualAnchor:
        return QStringLiteral("manual-anchor");
    case application::FrameMatchKind::Missing:
        return QStringLiteral("missing");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] bool writeFileAtomically(const QString& path, const QByteArray& content) {
    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

} // namespace

BadCaseExportResult exportBadCaseEvidence(const QImage& image,
                                          const application::SessionSnapshot& evidence,
                                          const QString& parentPath) {
    if (image.isNull() || !evidence.displayedFrame.has_value() || !QDir{parentPath}.exists()) {
        return {.error = QStringLiteral("The Bad Case evidence is incomplete.")};
    }

    QDir parent{parentPath};
    const QString unique = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    const QString suffix =
        QStringLiteral("frame-%1-%2-%3")
            .arg(evidence.displayedFrame->value())
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")))
            .arg(unique);
    const QString finalName = QStringLiteral("VCStation-badcase-%1").arg(suffix);
    const QString temporaryName = QStringLiteral(".vcstation-%1.tmp").arg(unique);
    if (!parent.mkdir(temporaryName)) {
        return {.error = QStringLiteral("The temporary evidence directory could not be created.")};
    }
    const QString temporaryPath = parent.filePath(temporaryName);
    const auto fail = [&temporaryPath](QString error) {
        static_cast<void>(QDir{temporaryPath}.removeRecursively());
        return BadCaseExportResult{.error = std::move(error)};
    };

    QByteArray bitmap;
    QBuffer bitmapBuffer{&bitmap};
    const QString imagePath = QDir{temporaryPath}.filePath(QStringLiteral("comparison.bmp"));
    if (!bitmapBuffer.open(QIODevice::WriteOnly) || !image.save(&bitmapBuffer, "BMP") ||
        !writeFileAtomically(imagePath, bitmap)) {
        return fail(QStringLiteral("The comparison image could not be written."));
    }

    QJsonArray sources;
    for (const application::SessionSourceView& source : evidence.sources) {
        const auto presented =
            std::find_if(evidence.presentedSources.begin(),
                         evidence.presentedSources.end(),
                         [&source](const application::PresentedSourceState& value) {
                             return value.sourceId == source.sourceId;
                         });
        QJsonObject item{
            {QStringLiteral("source_id"), static_cast<qint64>(source.sourceId)},
            {QStringLiteral("display_name"), QString::fromStdString(source.displayName)},
            {QStringLiteral("role"), static_cast<int>(source.role)},
        };
        if (presented != evidence.presentedSources.end()) {
            item.insert(QStringLiteral("source_frame"),
                        presented->sourceFrameId.has_value()
                            ? QJsonValue{presented->sourceFrameId->value()}
                            : QJsonValue{QJsonValue::Null});
            item.insert(QStringLiteral("match_kind"), matchKindName(presented->matchKind));
            item.insert(QStringLiteral("alignment_confidence"), presented->alignmentConfidence);
        }
        sources.push_back(std::move(item));
    }
    const QJsonObject document{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("product"), QStringLiteral("VCStation")},
        {QStringLiteral("exported_utc"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("canonical_frame"), evidence.displayedFrame->value()},
        {QStringLiteral("requested_frame"),
         evidence.requestedFrame.has_value() ? QJsonValue{evidence.requestedFrame->value()}
                                             : QJsonValue{QJsonValue::Null}},
        {QStringLiteral("session_epoch"), static_cast<qint64>(evidence.sessionEpoch.value())},
        {QStringLiteral("playback_generation"),
         static_cast<qint64>(evidence.playbackGeneration.value())},
        {QStringLiteral("alignment_revision"), static_cast<qint64>(evidence.alignmentRevision)},
        {QStringLiteral("sources"), sources},
    };
    const QString metadataPath = QDir{temporaryPath}.filePath(QStringLiteral("evidence.json"));
    if (!writeFileAtomically(metadataPath,
                             QJsonDocument{document}.toJson(QJsonDocument::Indented))) {
        return fail(QStringLiteral("The evidence metadata could not be written."));
    }
    if (!parent.rename(temporaryName, finalName)) {
        return fail(QStringLiteral("The evidence directory could not be published."));
    }
    return {.folder = parent.filePath(finalName)};
}

} // namespace dvs::ui::internal
