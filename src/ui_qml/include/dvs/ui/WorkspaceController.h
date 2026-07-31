#pragma once

#include "dvs/application/WorkspaceCoordinator.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace dvs::ui {

class ReviewPreferencesController;

class WorkspaceController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY stateChanged)
    Q_PROPERTY(bool hasProject READ hasProject NOTIFY stateChanged)
    Q_PROPERTY(bool canSave READ canSave NOTIFY stateChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY stateChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY stateChanged)
    Q_PROPERTY(QString errorTechnicalDetail READ errorTechnicalDetail NOTIFY stateChanged)
    Q_PROPERTY(bool relinkRequired READ relinkRequired NOTIFY stateChanged)
    Q_PROPERTY(int nextRelinkSourceId READ nextRelinkSourceId NOTIFY stateChanged)
    Q_PROPERTY(QVariantList sourceDiagnostics READ sourceDiagnostics NOTIFY stateChanged)
    Q_PROPERTY(
        QVariantMap restoredPresentationState READ restoredPresentationState NOTIFY stateChanged)
    Q_PROPERTY(qulonglong restoredViewSerial READ restoredViewSerial NOTIFY stateChanged)

public:
    explicit WorkspaceController(application::WorkspaceCoordinator& workspace,
                                 ReviewPreferencesController& preferences,
                                 QObject* parent = nullptr);
    ~WorkspaceController() override;

    WorkspaceController(const WorkspaceController&) = delete;
    WorkspaceController& operator=(const WorkspaceController&) = delete;

    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;
    [[nodiscard]] bool hasProject() const noexcept;
    [[nodiscard]] bool canSave() const noexcept;
    [[nodiscard]] QString projectPath() const;
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] QString errorTechnicalDetail() const;
    [[nodiscard]] bool relinkRequired() const noexcept;
    [[nodiscard]] int nextRelinkSourceId() const noexcept;
    [[nodiscard]] QVariantList sourceDiagnostics() const;
    [[nodiscard]] QVariantMap restoredPresentationState() const;
    [[nodiscard]] qulonglong restoredViewSerial() const noexcept;

    Q_INVOKABLE bool openProject(const QUrl& projectFile);
    Q_INVOKABLE bool closeReview();
    Q_INVOKABLE bool save();
    Q_INVOKABLE bool saveAs(const QUrl& projectFile);
    Q_INVOKABLE bool relinkSource(int sourceId, const QUrl& sourceFile);
    Q_INVOKABLE bool updatePresentationState(const QVariantMap& state);
    Q_INVOKABLE void refreshProjection();
    Q_INVOKABLE void stop() noexcept;

Q_SIGNALS:
    void stateChanged();

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::ui
