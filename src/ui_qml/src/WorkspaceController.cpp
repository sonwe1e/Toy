#include "dvs/ui/WorkspaceController.h"

#include "dvs/ui/ReviewPreferencesController.h"

#include <QVariantMap>

#include <memory>
#include <utility>

namespace dvs::ui {

class WorkspaceController::Impl final {
public:
    Impl(WorkspaceController& owner,
         application::WorkspaceCoordinator& workspace,
         ReviewPreferencesController& preferences)
        : owner_(owner), workspace_(workspace), preferences_(preferences) {
        refresh();
    }

    [[nodiscard]] bool busy() const noexcept {
        return snapshot_ && snapshot_->busy;
    }

    [[nodiscard]] QString displayName() const {
        return snapshot_ ? QString::fromStdString(snapshot_->displayName) : QString{};
    }

    [[nodiscard]] QString errorTechnicalDetail() const {
        return snapshot_ && snapshot_->lastError.has_value()
                   ? QString::fromStdString(snapshot_->lastError->technicalDetail)
                   : QString{};
    }

    [[nodiscard]] QVariantList sourceDiagnostics() const {
        QVariantList result;
        if (!snapshot_) {
            return result;
        }
        for (const application::SourceRevalidationDiagnostic& diagnostic :
             snapshot_->sourceDiagnostics) {
            if (!diagnostic.error.has_value()) {
                continue;
            }
            QVariantMap item;
            item.insert(QStringLiteral("sourceId"), static_cast<qulonglong>(diagnostic.sourceId));
            item.insert(QStringLiteral("detail"),
                        QString::fromStdString(diagnostic.error->technicalDetail));
            result.push_back(std::move(item));
        }
        return result;
    }

    [[nodiscard]] bool closeReview() {
        return workspace_.closeReview() == application::PortSubmitResult::Accepted;
    }

    void stop() noexcept {
        stopped_ = true;
    }

    void refreshProjection() {
        refresh();
    }

private:
    void refresh() {
        if (stopped_) {
            return;
        }
        const std::shared_ptr<const application::WorkspaceSnapshot> next = workspace_.snapshot();
        if (!next || next == snapshot_) {
            return;
        }
        snapshot_ = next;
        Q_EMIT owner_.stateChanged();
    }

    WorkspaceController& owner_;
    application::WorkspaceCoordinator& workspace_;
    ReviewPreferencesController& preferences_;
    std::shared_ptr<const application::WorkspaceSnapshot> snapshot_;
    bool stopped_ = false;
};

WorkspaceController::WorkspaceController(application::WorkspaceCoordinator& workspace,
                                         ReviewPreferencesController& preferences,
                                         QObject* const parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, workspace, preferences)) {}

WorkspaceController::~WorkspaceController() = default;

bool WorkspaceController::busy() const noexcept {
    return impl_->busy();
}

QString WorkspaceController::displayName() const {
    return impl_->displayName();
}

QString WorkspaceController::errorTechnicalDetail() const {
    return impl_->errorTechnicalDetail();
}

QVariantList WorkspaceController::sourceDiagnostics() const {
    return impl_->sourceDiagnostics();
}

bool WorkspaceController::closeReview() {
    return impl_->closeReview();
}

void WorkspaceController::refreshProjection() {
    impl_->refreshProjection();
}

void WorkspaceController::stop() noexcept {
    impl_->stop();
}

} // namespace dvs::ui
