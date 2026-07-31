#include "dvs/ui/WorkspaceController.h"

#include "dvs/ui/ReviewPreferencesController.h"

#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <utility>

namespace dvs::ui {
namespace {

[[nodiscard]] std::optional<std::filesystem::path> localPath(const QUrl& url) {
    if (!url.isValid() || !url.isLocalFile()) {
        return std::nullopt;
    }
    const QString path = url.toLocalFile();
    if (path.isEmpty()) {
        return std::nullopt;
    }
    return std::filesystem::path{path.toStdWString()};
}

[[nodiscard]] domain::ProjectViewLayout viewLayout(const ReviewPreferencesController::ViewMode mode,
                                                   const std::size_t sourceCount) noexcept {
    if (sourceCount == 1U) {
        return domain::ProjectViewLayout::kSingle;
    }
    switch (mode) {
    case ReviewPreferencesController::ViewMode::ThreeUp:
        return sourceCount == 3U ? domain::ProjectViewLayout::kThreeUp
                                 : domain::ProjectViewLayout::kSideBySide;
    case ReviewPreferencesController::ViewMode::AnalysisGrid:
        return sourceCount == 3U ? domain::ProjectViewLayout::kAnalysisGrid
                                 : domain::ProjectViewLayout::kSideBySide;
    case ReviewPreferencesController::ViewMode::ReferenceFocus:
        return domain::ProjectViewLayout::kReferenceFocus;
    case ReviewPreferencesController::ViewMode::Difference:
        return domain::ProjectViewLayout::kDifference;
    case ReviewPreferencesController::ViewMode::Wipe:
        return domain::ProjectViewLayout::kWipe;
    case ReviewPreferencesController::ViewMode::SideBySide:
        return domain::ProjectViewLayout::kSideBySide;
    }
    return domain::ProjectViewLayout::kSideBySide;
}

[[nodiscard]] domain::ProjectDifferenceMetric
differenceMetric(const ReviewPreferencesController::DifferenceMetric metric) noexcept {
    switch (metric) {
    case ReviewPreferencesController::DifferenceMetric::Luma:
        return domain::ProjectDifferenceMetric::kLuma;
    case ReviewPreferencesController::DifferenceMetric::Chroma:
        return domain::ProjectDifferenceMetric::kChroma;
    case ReviewPreferencesController::DifferenceMetric::Heatmap:
        return domain::ProjectDifferenceMetric::kHeatmap;
    case ReviewPreferencesController::DifferenceMetric::ExactPlanes:
        return domain::ProjectDifferenceMetric::kExactPlanes;
    case ReviewPreferencesController::DifferenceMetric::RgbAbsolute:
        return domain::ProjectDifferenceMetric::kRgbAbsolute;
    }
    return domain::ProjectDifferenceMetric::kRgbAbsolute;
}

[[nodiscard]] std::optional<std::array<domain::SourceId, 2U>>
differenceEdge(const ReviewPreferencesController::DifferenceEdge edge,
               const std::size_t sourceCount) noexcept {
    if (sourceCount < 2U) {
        return std::nullopt;
    }
    if (sourceCount >= 3U) {
        if (edge == ReviewPreferencesController::DifferenceEdge::Edge0And2) {
            return std::array<domain::SourceId, 2U>{0U, 2U};
        }
        if (edge == ReviewPreferencesController::DifferenceEdge::Edge1And2) {
            return std::array<domain::SourceId, 2U>{1U, 2U};
        }
    }
    return std::array<domain::SourceId, 2U>{0U, 1U};
}

[[nodiscard]] domain::ProjectDifferenceFilter
differenceFilter(const ReviewPreferencesController::DifferenceFilter filter) noexcept {
    switch (filter) {
    case ReviewPreferencesController::DifferenceFilter::Nearest:
        return domain::ProjectDifferenceFilter::kNearest;
    case ReviewPreferencesController::DifferenceFilter::Bicubic:
        return domain::ProjectDifferenceFilter::kBicubic;
    case ReviewPreferencesController::DifferenceFilter::Bilinear:
        return domain::ProjectDifferenceFilter::kBilinear;
    }
    return domain::ProjectDifferenceFilter::kBilinear;
}

[[nodiscard]] std::uint8_t
differenceGain(const ReviewPreferencesController::DifferenceGain gain) noexcept {
    return static_cast<std::uint8_t>(1U << static_cast<unsigned int>(gain));
}

void applyViewState(ReviewPreferencesController& preferences,
                    const domain::ProjectViewState& view) {
    if (view.layout != domain::ProjectViewLayout::kSingle) {
        preferences.setViewMode(static_cast<ReviewPreferencesController::ViewMode>(view.layout));
    }
    preferences.setDifferenceMetric(
        static_cast<ReviewPreferencesController::DifferenceMetric>(view.differenceMetric));
    switch (view.gain) {
    case 2U:
        preferences.setDifferenceGain(ReviewPreferencesController::DifferenceGain::Gain2x);
        break;
    case 4U:
        preferences.setDifferenceGain(ReviewPreferencesController::DifferenceGain::Gain4x);
        break;
    case 8U:
        preferences.setDifferenceGain(ReviewPreferencesController::DifferenceGain::Gain8x);
        break;
    case 16U:
        preferences.setDifferenceGain(ReviewPreferencesController::DifferenceGain::Gain16x);
        break;
    default:
        preferences.setDifferenceGain(ReviewPreferencesController::DifferenceGain::Gain1x);
        break;
    }
    preferences.setDifferenceFilter(
        static_cast<ReviewPreferencesController::DifferenceFilter>(view.differenceFilter));
    if (view.differenceEdge.has_value() &&
        *view.differenceEdge == std::array<domain::SourceId, 2U>{0U, 2U}) {
        preferences.setDifferenceEdge(ReviewPreferencesController::DifferenceEdge::Edge0And2);
    } else if (view.differenceEdge.has_value() &&
               *view.differenceEdge == std::array<domain::SourceId, 2U>{1U, 2U}) {
        preferences.setDifferenceEdge(ReviewPreferencesController::DifferenceEdge::Edge1And2);
    } else {
        preferences.setDifferenceEdge(ReviewPreferencesController::DifferenceEdge::Edge0And1);
    }
}

} // namespace

class WorkspaceController::Impl final {
public:
    Impl(WorkspaceController& owner,
         application::WorkspaceCoordinator& workspace,
         ReviewPreferencesController& preferences)
        : owner_(owner), workspace_(workspace), preferences_(preferences) {
        QObject::connect(
            &preferences_, &ReviewPreferencesController::preferencesChanged, &owner_, [this] {
                if (!applyingRestoredView_) {
                    workspace_.markViewDirty();
                }
            });
        refresh();
    }

    [[nodiscard]] bool busy() const noexcept {
        return snapshot_ && snapshot_->busy;
    }

    [[nodiscard]] bool dirty() const noexcept {
        return snapshot_ && snapshot_->dirty;
    }

    [[nodiscard]] bool hasProject() const noexcept {
        return snapshot_ && snapshot_->hasProject;
    }

    [[nodiscard]] bool canSave() const noexcept {
        const auto playback = workspace_.playbackSnapshot();
        return !stopped_ && !busy() && playback &&
               playback->sessionState == domain::SessionState::kReady &&
               playback->displayedFrame.has_value();
    }

    [[nodiscard]] QString projectPath() const {
        return snapshot_ ? QString::fromStdWString(snapshot_->projectPath.wstring()) : QString{};
    }

    [[nodiscard]] QString displayName() const {
        return snapshot_ ? QString::fromStdString(snapshot_->displayName) : QString{};
    }

    [[nodiscard]] QString errorTechnicalDetail() const {
        return snapshot_ && snapshot_->lastError.has_value()
                   ? QString::fromStdString(snapshot_->lastError->technicalDetail)
                   : QString{};
    }

    [[nodiscard]] bool relinkRequired() const noexcept {
        return nextRelinkSourceId() >= 0;
    }

    [[nodiscard]] int nextRelinkSourceId() const noexcept {
        if (!snapshot_) {
            return -1;
        }
        const auto diagnostic =
            std::find_if(snapshot_->sourceDiagnostics.begin(),
                         snapshot_->sourceDiagnostics.end(),
                         [](const application::SourceRevalidationDiagnostic& value) {
                             return value.error.has_value();
                         });
        return diagnostic == snapshot_->sourceDiagnostics.end()
                   ? -1
                   : static_cast<int>(diagnostic->sourceId);
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

    [[nodiscard]] QVariantMap restoredPresentationState() const {
        QVariantMap result;
        result.insert(QStringLiteral("wipePosition"), presentationView_.wipePosition);
        result.insert(QStringLiteral("thresholdEnabled"), presentationView_.thresholdEnabled);
        result.insert(QStringLiteral("threshold"), presentationView_.threshold);
        result.insert(QStringLiteral("centerX"), presentationView_.viewport.centerX);
        result.insert(QStringLiteral("centerY"), presentationView_.viewport.centerY);
        result.insert(QStringLiteral("scale"), presentationView_.viewport.scale);
        result.insert(QStringLiteral("roiEnabled"), presentationView_.roi.has_value());
        if (presentationView_.roi.has_value()) {
            result.insert(QStringLiteral("roiLeft"), presentationView_.roi->left);
            result.insert(QStringLiteral("roiTop"), presentationView_.roi->top);
            result.insert(QStringLiteral("roiRight"), presentationView_.roi->right);
            result.insert(QStringLiteral("roiBottom"), presentationView_.roi->bottom);
        }
        return result;
    }

    [[nodiscard]] qulonglong restoredViewSerial() const noexcept {
        return restoredViewSerial_;
    }

    [[nodiscard]] bool openProject(const QUrl& projectFile) {
        const auto path = localPath(projectFile);
        return path.has_value() &&
               workspace_.openProject(*path) == application::PortSubmitResult::Accepted;
    }

    [[nodiscard]] bool closeReview() {
        return workspace_.closeReview() == application::PortSubmitResult::Accepted;
    }

    [[nodiscard]] bool save() {
        return snapshot_ && snapshot_->hasProject && !snapshot_->projectPath.empty() &&
               saveTo(snapshot_->projectPath);
    }

    [[nodiscard]] bool saveAs(const QUrl& projectFile) {
        const auto path = localPath(projectFile);
        return path.has_value() && saveTo(*path);
    }

    [[nodiscard]] bool relinkSource(const int sourceId, const QUrl& sourceFile) {
        const auto path = localPath(sourceFile);
        return sourceId >= 0 && path.has_value() &&
               workspace_.relinkSource(static_cast<domain::SourceId>(sourceId), *path) ==
                   application::PortSubmitResult::Accepted;
    }

    [[nodiscard]] bool updatePresentationState(const QVariantMap& state) {
        const auto number = [&state](const QString& key, const float fallback) {
            bool valid = false;
            const float value = state.value(key, fallback).toFloat(&valid);
            return valid && std::isfinite(value) ? std::optional<float>{value} : std::nullopt;
        };
        const auto wipe = number(QStringLiteral("wipePosition"), 0.5F);
        const auto threshold = number(QStringLiteral("threshold"), 0.0F);
        const auto centerX = number(QStringLiteral("centerX"), 0.5F);
        const auto centerY = number(QStringLiteral("centerY"), 0.5F);
        const auto scale = number(QStringLiteral("scale"), 1.0F);
        if (!wipe || !threshold || !centerX || !centerY || !scale) {
            return false;
        }
        presentationView_.wipePosition = *wipe;
        presentationView_.thresholdEnabled =
            state.value(QStringLiteral("thresholdEnabled"), false).toBool();
        presentationView_.threshold = *threshold;
        presentationView_.viewport = domain::ProjectViewTransform{
            .centerX = *centerX,
            .centerY = *centerY,
            .scale = *scale,
        };
        if (state.value(QStringLiteral("roiEnabled"), false).toBool()) {
            const auto left = number(QStringLiteral("roiLeft"), 0.0F);
            const auto top = number(QStringLiteral("roiTop"), 0.0F);
            const auto right = number(QStringLiteral("roiRight"), 1.0F);
            const auto bottom = number(QStringLiteral("roiBottom"), 1.0F);
            if (!left || !top || !right || !bottom) {
                return false;
            }
            presentationView_.roi = domain::ProjectNormalizedRect{
                .left = *left,
                .top = *top,
                .right = *right,
                .bottom = *bottom,
            };
        } else {
            presentationView_.roi.reset();
        }
        return true;
    }

    void stop() noexcept {
        stopped_ = true;
    }

    void refreshProjection() {
        refresh();
    }

private:
    [[nodiscard]] bool saveTo(const std::filesystem::path& projectPath) {
        const std::shared_ptr<const application::SessionSnapshot> playback =
            workspace_.playbackSnapshot();
        if (!playback) {
            return false;
        }
        const std::size_t sourceCount = playback->sources.size();
        domain::ProjectViewState view = presentationView_;
        view.layout = viewLayout(preferences_.viewMode(), sourceCount);
        view.differenceEdge = differenceEdge(preferences_.differenceEdge(), sourceCount);
        view.differenceMetric = differenceMetric(preferences_.differenceMetric());
        view.differenceFilter = differenceFilter(preferences_.differenceFilter());
        view.gain = differenceGain(preferences_.differenceGain());
        const std::string displayName = snapshot_ && !snapshot_->displayName.empty()
                                            ? snapshot_->displayName
                                            : projectPath.stem().string();
        return workspace_.saveProject(projectPath, displayName, view) ==
               application::PortSubmitResult::Accepted;
    }

    void refresh() {
        if (stopped_) {
            return;
        }
        const std::shared_ptr<const application::WorkspaceSnapshot> next = workspace_.snapshot();
        if (!next || next == snapshot_) {
            return;
        }
        const bool applyRestoredView =
            next->hasProject && next->restoredViewState.has_value() &&
            (!snapshot_ || !snapshot_->hasProject || next->projectPath != snapshot_->projectPath ||
             next->restoredViewState != snapshot_->restoredViewState);
        snapshot_ = next;
        if (applyRestoredView) {
            presentationView_ = *snapshot_->restoredViewState;
            ++restoredViewSerial_;
            applyingRestoredView_ = true;
            applyViewState(preferences_, *snapshot_->restoredViewState);
            applyingRestoredView_ = false;
        }
        Q_EMIT owner_.stateChanged();
    }

    WorkspaceController& owner_;
    application::WorkspaceCoordinator& workspace_;
    ReviewPreferencesController& preferences_;
    std::shared_ptr<const application::WorkspaceSnapshot> snapshot_;
    domain::ProjectViewState presentationView_;
    qulonglong restoredViewSerial_ = 0U;
    bool stopped_ = false;
    bool applyingRestoredView_ = false;
};

WorkspaceController::WorkspaceController(application::WorkspaceCoordinator& workspace,
                                         ReviewPreferencesController& preferences,
                                         QObject* const parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, workspace, preferences)) {}

WorkspaceController::~WorkspaceController() = default;

bool WorkspaceController::busy() const noexcept {
    return impl_->busy();
}

bool WorkspaceController::dirty() const noexcept {
    return impl_->dirty();
}

bool WorkspaceController::hasProject() const noexcept {
    return impl_->hasProject();
}

bool WorkspaceController::canSave() const noexcept {
    return impl_->canSave();
}

QString WorkspaceController::projectPath() const {
    return impl_->projectPath();
}

QString WorkspaceController::displayName() const {
    return impl_->displayName();
}

QString WorkspaceController::errorTechnicalDetail() const {
    return impl_->errorTechnicalDetail();
}

bool WorkspaceController::relinkRequired() const noexcept {
    return impl_->relinkRequired();
}

int WorkspaceController::nextRelinkSourceId() const noexcept {
    return impl_->nextRelinkSourceId();
}

QVariantList WorkspaceController::sourceDiagnostics() const {
    return impl_->sourceDiagnostics();
}

QVariantMap WorkspaceController::restoredPresentationState() const {
    return impl_->restoredPresentationState();
}

qulonglong WorkspaceController::restoredViewSerial() const noexcept {
    return impl_->restoredViewSerial();
}

bool WorkspaceController::openProject(const QUrl& projectFile) {
    return impl_->openProject(projectFile);
}

bool WorkspaceController::closeReview() {
    return impl_->closeReview();
}

bool WorkspaceController::save() {
    return impl_->save();
}

bool WorkspaceController::saveAs(const QUrl& projectFile) {
    return impl_->saveAs(projectFile);
}

bool WorkspaceController::relinkSource(const int sourceId, const QUrl& sourceFile) {
    return impl_->relinkSource(sourceId, sourceFile);
}

bool WorkspaceController::updatePresentationState(const QVariantMap& state) {
    return impl_->updatePresentationState(state);
}

void WorkspaceController::refreshProjection() {
    impl_->refreshProjection();
}

void WorkspaceController::stop() noexcept {
    impl_->stop();
}

} // namespace dvs::ui
