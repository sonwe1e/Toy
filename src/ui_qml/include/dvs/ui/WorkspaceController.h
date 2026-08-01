#pragma once

#include "dvs/application/WorkspaceCoordinator.h"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

namespace dvs::ui {

class ReviewPreferencesController;

class WorkspaceController final : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY stateChanged)
    Q_PROPERTY(QString errorTechnicalDetail READ errorTechnicalDetail NOTIFY stateChanged)
    Q_PROPERTY(QVariantList sourceDiagnostics READ sourceDiagnostics NOTIFY stateChanged)

public:
    explicit WorkspaceController(application::WorkspaceCoordinator& workspace,
                                 ReviewPreferencesController& preferences,
                                 QObject* parent = nullptr);
    ~WorkspaceController() override;

    WorkspaceController(const WorkspaceController&) = delete;
    WorkspaceController& operator=(const WorkspaceController&) = delete;

    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] QString displayName() const;
    [[nodiscard]] QString errorTechnicalDetail() const;
    [[nodiscard]] QVariantList sourceDiagnostics() const;

    Q_INVOKABLE bool closeReview();
    Q_INVOKABLE void refreshProjection();
    Q_INVOKABLE void stop() noexcept;

Q_SIGNALS:
    void stateChanged();

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::ui
