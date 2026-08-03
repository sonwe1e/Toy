#pragma once

#include <QAbstractListModel>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace dvs::ui {

struct SourceListRow final {
    std::uint32_t sourceId = 0U;
    QString sourceIdentity;
    int role = 0;
    QString filename;
    QString errorKey;
    std::optional<qint64> currentSourceFrame;
    int matchKind = 0;
    qreal confidence = 1.0;
    bool missing = true;
    int missingReason = -1;
    qint64 manualOffset = 0;

    [[nodiscard]] bool operator==(const SourceListRow&) const = default;
};

class SourceListModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        SourceIdRole = Qt::UserRole + 1,
        SourceIdentityRole,
        RoleRole,
        FilenameRole,
        ErrorRole,
        CurrentSourceFrameRole,
        MatchKindRole,
        ConfidenceRole,
        MissingRole,
        MissingReasonRole,
        ManualOffsetRole,
    };
    Q_ENUM(Role)

    explicit SourceListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setRows(std::vector<SourceListRow> rows);

private:
    std::vector<SourceListRow> rows_;
};

} // namespace dvs::ui
