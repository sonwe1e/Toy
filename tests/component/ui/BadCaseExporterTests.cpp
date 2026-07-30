#include "BadCaseExporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace dvs::ui::internal {
namespace {

void ensureCoreApplication() {
    if (QCoreApplication::instance() != nullptr) {
        return;
    }
    static int argumentCount = 1;
    static char applicationName[] = "BadCaseExporterTests";
    static char* arguments[] = {applicationName, nullptr};
    static QCoreApplication application{argumentCount, arguments};
}

[[nodiscard]] application::SessionSnapshot evidence() {
    application::SessionSnapshot snapshot;
    snapshot.sessionEpoch = domain::SessionEpoch{2U};
    snapshot.playbackGeneration = domain::PlaybackGeneration{3U};
    snapshot.displayedFrame = domain::FrameId{42};
    snapshot.requestedFrame = domain::FrameId{42};
    snapshot.alignmentRevision = 4U;
    snapshot.sources = {
        application::SessionSourceView{
            .sourceId = 0U,
            .role = domain::ComparisonRole::kReference,
            .displayName = "reference.mp4",
        },
        application::SessionSourceView{
            .sourceId = 1U,
            .role = domain::ComparisonRole::kPrediction,
            .displayName = "prediction.mp4",
        },
    };
    snapshot.presentedSources = {
        application::PresentedSourceState{
            .sourceId = 0U,
            .sourceFrameId = domain::FrameId{42},
            .matchKind = application::FrameMatchKind::ExactIndex,
        },
        application::PresentedSourceState{
            .sourceId = 1U,
            .sourceFrameId = domain::FrameId{43},
            .matchKind = application::FrameMatchKind::GlobalOffset,
            .alignmentConfidence = 0.9F,
        },
    };
    return snapshot;
}

TEST(BadCaseExporterTests, PublishesImageAndIdentityMetadataAsOneDirectory) {
    ensureCoreApplication();
    QTemporaryDir parent;
    ASSERT_TRUE(parent.isValid());
    QImage image{64, 32, QImage::Format_RGBA8888};
    image.fill(Qt::red);

    const BadCaseExportResult result = exportBadCaseEvidence(image, evidence(), parent.path());

    ASSERT_TRUE(result.succeeded()) << result.error.toStdString();
    EXPECT_TRUE(QFile::exists(QDir{result.folder}.filePath(QStringLiteral("comparison.bmp"))));
    QFile metadata{QDir{result.folder}.filePath(QStringLiteral("evidence.json"))};
    ASSERT_TRUE(metadata.open(QIODevice::ReadOnly));
    const QJsonObject document = QJsonDocument::fromJson(metadata.readAll()).object();
    EXPECT_EQ(document.value(QStringLiteral("product")).toString(), QStringLiteral("VCStation"));
    EXPECT_EQ(document.value(QStringLiteral("canonical_frame")).toInteger(), 42);
    EXPECT_EQ(document.value(QStringLiteral("alignment_revision")).toInteger(), 4);
    const QJsonArray sources = document.value(QStringLiteral("sources")).toArray();
    ASSERT_EQ(sources.size(), 2);
    EXPECT_EQ(sources[1].toObject().value(QStringLiteral("source_frame")).toInteger(), 43);
    EXPECT_EQ(sources[1].toObject().value(QStringLiteral("match_kind")).toString(),
              QStringLiteral("global-offset"));

    const QStringList partials = QDir{parent.path()}.entryList(
        {QStringLiteral(".vcstation-*.tmp")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    EXPECT_TRUE(partials.isEmpty());
}

TEST(BadCaseExporterTests, RejectsIncompleteEvidenceWithoutPublishingFiles) {
    ensureCoreApplication();
    QTemporaryDir parent;
    ASSERT_TRUE(parent.isValid());

    const BadCaseExportResult result = exportBadCaseEvidence(QImage{}, evidence(), parent.path());

    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(result.error.isEmpty());
    EXPECT_TRUE(QDir{parent.path()}.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
}

} // namespace
} // namespace dvs::ui::internal
