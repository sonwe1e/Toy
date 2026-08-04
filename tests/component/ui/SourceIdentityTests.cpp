#include "dvs/ui/SourceIdentity.h"
#include "dvs/ui/SourceListModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QModelIndex>
#include <QTemporaryDir>
#include <QUrl>

#include <gtest/gtest.h>
#include <vector>

namespace dvs::ui {
namespace {

TEST(SourceIdentityTests, ComposeMatchesLiveCanonicalIdentity) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("compose.mp4"));
    QFile file{path};
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("compose-data"), 12);
    file.close();

    const QFileInfo info{path};
    QString normalized = info.canonicalFilePath();
    if (normalized.isEmpty()) {
        normalized = info.absoluteFilePath();
    }
    const QString composed = composeSourceIdentity(
        normalized, info.size(), info.lastModified().toMSecsSinceEpoch());
    EXPECT_EQ(composed, canonicalSourceIdentity(QUrl::fromLocalFile(path)));
}

TEST(SourceIdentityTests, ChangedOnDiskRolePublishesStateThroughModel) {
    SourceListModel model;
    std::vector<SourceListRow> rows{{
        .sourceId = 0U,
        .sourceIdentity = QStringLiteral("id-a"),
        .filename = QStringLiteral("a.mp4"),
        .changedOnDisk = false,
    }};
    model.setRows(rows);
    EXPECT_EQ(model.data(model.index(0, 0), SourceListModel::ChangedOnDiskRole).toBool(), false);

    QList<int> changedRoles;
    QObject::connect(&model,
                     &QAbstractItemModel::dataChanged,
                     [&changedRoles](const QModelIndex&,
                                     const QModelIndex&,
                                     const QList<int>& roles) { changedRoles = roles; });
    rows.front().changedOnDisk = true;
    model.setRows(rows);

    EXPECT_TRUE(changedRoles.contains(SourceListModel::ChangedOnDiskRole));
    EXPECT_EQ(model.data(model.index(0, 0), SourceListModel::ChangedOnDiskRole).toBool(), true);
}

TEST(SourceIdentityTests, IncludesLocalFileRevisionAndNormalizesRemoteUrls) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("source.mp4"));
    QFile file{path};
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write("first"), 5);
    file.close();

    const QUrl local = QUrl::fromLocalFile(path);
    const QString initial = canonicalSourceIdentity(local);
    EXPECT_FALSE(initial.isEmpty());

    ASSERT_TRUE(file.open(QIODevice::Append));
    ASSERT_EQ(file.write("-revision"), 9);
    file.close();
    EXPECT_NE(canonicalSourceIdentity(local), initial);

    const QUrl remote{QStringLiteral("https://EXAMPLE.invalid/a%20video.mp4")};
    EXPECT_EQ(canonicalSourceIdentity(remote), remote.toString(QUrl::FullyEncoded).toCaseFolded());
}

TEST(SourceIdentityTests, SourceListModelPublishesIdentityRoleChangesWithoutResettingRows) {
    SourceListModel model;
    std::vector<SourceListRow> rows{{
        .sourceId = 0U,
        .sourceIdentity = QStringLiteral("source-a-v1"),
        .filename = QStringLiteral("a.mp4"),
    }};
    model.setRows(rows);
    EXPECT_EQ(model.data(model.index(0, 0), SourceListModel::SourceIdentityRole).toString(),
              QStringLiteral("source-a-v1"));

    QList<int> changedRoles;
    QObject::connect(&model,
                     &QAbstractItemModel::dataChanged,
                     [&changedRoles](const QModelIndex&,
                                     const QModelIndex&,
                                     const QList<int>& roles) { changedRoles = roles; });
    rows.front().sourceIdentity = QStringLiteral("source-a-v2");
    model.setRows(rows);

    EXPECT_TRUE(changedRoles.contains(SourceListModel::SourceIdentityRole));
    EXPECT_EQ(model.data(model.index(0, 0), SourceListModel::SourceIdentityRole).toString(),
              QStringLiteral("source-a-v2"));
}

} // namespace
} // namespace dvs::ui
