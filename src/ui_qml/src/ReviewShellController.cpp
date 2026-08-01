#include "dvs/ui/ReviewShellController.h"

#include "dvs/ui/ReviewController.h"

#include <algorithm>
#include <utility>

namespace dvs::ui {
namespace {

constexpr std::size_t kMaximumQueuedStartupRequests = 9U;

} // namespace

ReviewShellController::ReviewShellController(ReviewController& review, QObject* const parent)
    : QObject(parent), review_(review) {
    QObject::connect(
        &review_, &ReviewController::stateChanged, this, [this] { synchronizeActiveSources(); });
    synchronizeActiveSources();
}

QVariantList ReviewShellController::activeSources() const {
    return activeSources_;
}

QVariantList ReviewShellController::stagedSources() const {
    return stagedSources_;
}

int ReviewShellController::canonicalSourceIndex() const noexcept {
    return canonicalSourceIndex_;
}

qulonglong ReviewShellController::activeGeneration() const noexcept {
    return activeGeneration_;
}

int ReviewShellController::stagedReferenceIndex() const noexcept {
    return stagedReferenceIndex_;
}

int ReviewShellController::queuedStartupRequestCount() const noexcept {
    return static_cast<int>(startupRequests_.size());
}

bool ReviewShellController::startupRequestActive() const noexcept {
    return startupRequestActive_;
}

bool ReviewShellController::chromeVisible() const noexcept {
    return chromeVisible_;
}

bool ReviewShellController::inspectorVisible() const noexcept {
    return inspectorVisible_;
}

bool ReviewShellController::hasPendingAction() const noexcept {
    return !pendingAction_.isEmpty();
}

QVariantMap ReviewShellController::pendingAction() const {
    return pendingAction_;
}

int ReviewShellController::openIntent() const noexcept {
    return openIntent_;
}

void ReviewShellController::setStagedReferenceIndex(const int sourceIndex) {
    if (sourceIndex < 0 || sourceIndex >= stagedSources_.size() ||
        stagedReferenceIndex_ == sourceIndex) {
        return;
    }
    stagedReferenceIndex_ = sourceIndex;
    Q_EMIT stateChanged();
}

void ReviewShellController::setChromeVisible(const bool visible) {
    if (chromeVisible_ == visible) {
        return;
    }
    chromeVisible_ = visible;
    if (!chromeVisible_) {
        inspectorVisible_ = false;
    }
    Q_EMIT stateChanged();
}

void ReviewShellController::setInspectorVisible(const bool visible) {
    const bool next = chromeVisible_ && visible;
    if (inspectorVisible_ == next) {
        return;
    }
    inspectorVisible_ = next;
    Q_EMIT stateChanged();
}

bool ReviewShellController::stageSources(const QVariantList& sources, const int referenceIndex) {
    if (sources.isEmpty() || sources.size() > 3 || referenceIndex < 0 ||
        referenceIndex >= sources.size()) {
        return false;
    }
    stagedSources_ = sources;
    stagedReferenceIndex_ = referenceIndex;
    Q_EMIT stateChanged();
    return true;
}

void ReviewShellController::clearStagedSources() {
    if (stagedSources_.isEmpty() && stagedReferenceIndex_ == 0) {
        return;
    }
    stagedSources_.clear();
    stagedReferenceIndex_ = 0;
    Q_EMIT stateChanged();
}

bool ReviewShellController::moveStagedSource(const int fromIndex, const int toIndex) {
    if (fromIndex < 0 || toIndex < 0 || fromIndex >= stagedSources_.size() ||
        toIndex >= stagedSources_.size() || fromIndex == toIndex) {
        return false;
    }
    stagedSources_.move(fromIndex, toIndex);
    if (stagedReferenceIndex_ == fromIndex) {
        stagedReferenceIndex_ = toIndex;
    } else if (fromIndex < stagedReferenceIndex_ && toIndex >= stagedReferenceIndex_) {
        --stagedReferenceIndex_;
    } else if (fromIndex > stagedReferenceIndex_ && toIndex <= stagedReferenceIndex_) {
        ++stagedReferenceIndex_;
    }
    Q_EMIT stateChanged();
    return true;
}

bool ReviewShellController::openStagedSources(const bool preserveDisplayedTime) {
    if (stagedSources_.isEmpty()) {
        return false;
    }
    openIntent_ = preserveDisplayedTime ? ReplaceSources : NewReview;
    Q_EMIT stateChanged();
    return preserveDisplayedTime ? review_.reopenSources(stagedSources_, stagedReferenceIndex_)
                                 : review_.openSources(stagedSources_, stagedReferenceIndex_);
}

bool ReviewShellController::removeActiveSource(const int sourceIndex) {
    if (sourceIndex < 0 || sourceIndex >= activeSources_.size() || activeSources_.size() <= 1) {
        return false;
    }
    QVariantList replacement = activeSources_;
    replacement.removeAt(sourceIndex);
    int reference = canonicalSourceIndex_;
    if (reference == sourceIndex) {
        reference = 0;
    } else if (reference > sourceIndex) {
        --reference;
    }
    if (!stageSources(replacement, std::max(0, reference))) {
        return false;
    }
    openIntent_ = ReplaceSources;
    Q_EMIT stateChanged();
    return review_.reopenSources(replacement, stagedReferenceIndex_);
}

bool ReviewShellController::changeReference(const int sourceIndex) {
    openIntent_ = ChangeReference;
    Q_EMIT stateChanged();
    return review_.changeReference(sourceIndex);
}

bool ReviewShellController::beginPendingAction(const QVariantMap& action) {
    if (action.isEmpty() || !pendingAction_.isEmpty()) {
        return false;
    }
    pendingAction_ = action;
    Q_EMIT stateChanged();
    return true;
}

QVariantMap ReviewShellController::takePendingAction() {
    if (pendingAction_.isEmpty()) {
        return {};
    }
    QVariantMap action = std::move(pendingAction_);
    pendingAction_.clear();
    Q_EMIT stateChanged();
    return action;
}

void ReviewShellController::cancelPendingAction() {
    if (pendingAction_.isEmpty()) {
        return;
    }
    pendingAction_.clear();
    Q_EMIT stateChanged();
}

bool ReviewShellController::enqueueStartupRequest(const int kind, const QVariantList& files) {
    if (kind <= 0 || files.isEmpty() ||
        startupRequests_.size() + static_cast<std::size_t>(startupRequestActive_) >=
            kMaximumQueuedStartupRequests) {
        return false;
    }
    startupRequests_.push_back(StartupRequest{.kind = kind, .files = files});
    Q_EMIT stateChanged();
    Q_EMIT startupRequestAvailable();
    return true;
}

QVariantMap ReviewShellController::takeNextStartupRequest() {
    if (startupRequestActive_ || startupRequests_.empty()) {
        return {};
    }
    StartupRequest request = std::move(startupRequests_.front());
    startupRequests_.pop_front();
    startupRequestActive_ = true;
    Q_EMIT stateChanged();
    return QVariantMap{
        {QStringLiteral("kind"), request.kind},
        {QStringLiteral("urls"), request.files},
    };
}

void ReviewShellController::completeStartupRequest() {
    if (!startupRequestActive_) {
        return;
    }
    startupRequestActive_ = false;
    Q_EMIT stateChanged();
    if (!startupRequests_.empty()) {
        Q_EMIT startupRequestAvailable();
    }
}

void ReviewShellController::synchronizeActiveSources() {
    // The review controller publishes loading snapshots while an open is in flight. Those
    // snapshots describe the candidate provider state, not a committed review. Keep the shell's
    // Active Sources stable until the command terminal arrives; a success then adopts the new
    // validated set, while a failure continues to expose the restored prior set.
    if (review_.busy()) {
        return;
    }
    const QVariantList nextSources = review_.activeSources();
    const int nextCanonical = review_.canonicalSourceIndex();
    if (activeSources_ == nextSources && canonicalSourceIndex_ == nextCanonical) {
        return;
    }
    activeSources_ = nextSources;
    canonicalSourceIndex_ = nextCanonical;
    ++activeGeneration_;
    if (!review_.busy() && !activeSources_.isEmpty()) {
        stagedSources_ = activeSources_;
        stagedReferenceIndex_ = std::max(0, canonicalSourceIndex_);
    }
    Q_EMIT stateChanged();
}

} // namespace dvs::ui
