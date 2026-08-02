#include "dvs/ui/ReviewShellController.h"

#include "dvs/ui/ReviewController.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace dvs::ui {
namespace {

constexpr std::size_t kMaximumQueuedStartupRequests = 9U;

} // namespace

ReviewShellController::ReviewShellController(ReviewController& review, QObject* const parent)
    : QObject(parent), review_(review) {
    QObject::connect(&review_, &ReviewController::stateChanged, this, [this] {
        synchronizeActiveSources();
        if (!review_.busy() && !reviewIntents_.empty()) {
            QMetaObject::invokeMethod(this, [this] { drainIntentQueue(); }, Qt::QueuedConnection);
        }
    });
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

int ReviewShellController::queuedIntentCount() const noexcept {
    return static_cast<int>(reviewIntents_.size());
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

qint64 ReviewShellController::inFrame() const noexcept {
    return inFrame_;
}

qint64 ReviewShellController::outFrame() const noexcept {
    return outFrame_;
}

double ReviewShellController::inMediaTime() const noexcept {
    return inMediaTime_;
}

double ReviewShellController::outMediaTime() const noexcept {
    return outMediaTime_;
}

bool ReviewShellController::rangePlaybackActive() const noexcept {
    return rangePlaybackActive_;
}

bool ReviewShellController::rangeStartPending() const noexcept {
    return rangeStartPending_;
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
    return submitOrQueue(ReviewIntent{
        .kind = preserveDisplayedTime ? ReviewIntentKind::ReplaceSources
                                      : ReviewIntentKind::OpenSources,
        .sources = stagedSources_,
        .referenceIndex = stagedReferenceIndex_,
    });
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
    return submitOrQueue(ReviewIntent{
        .kind = ReviewIntentKind::ReplaceSources,
        .sources = std::move(replacement),
        .referenceIndex = stagedReferenceIndex_,
    });
}

bool ReviewShellController::changeReference(const int sourceIndex) {
    if (sourceIndex < 0 || sourceIndex >= activeSources_.size() ||
        sourceIndex == canonicalSourceIndex_) {
        return false;
    }
    openIntent_ = ChangeReference;
    Q_EMIT stateChanged();
    return submitOrQueue(ReviewIntent{
        .kind = ReviewIntentKind::ChangeReference,
        .referenceIndex = sourceIndex,
    });
}

bool ReviewShellController::closeSources() {
    return submitOrQueue(ReviewIntent{.kind = ReviewIntentKind::CloseSources});
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

bool ReviewShellController::setRangeIn(const qint64 frame, const double mediaTime) {
    if (frame < 0) {
        return false;
    }
    inFrame_ = frame;
    inMediaTime_ = mediaTime;
    if (outFrame_ >= 0 && outFrame_ < inFrame_) {
        outFrame_ = -1;
        outMediaTime_ = -1.0;
        rangePlaybackActive_ = false;
        rangeStartPending_ = false;
    }
    Q_EMIT stateChanged();
    return true;
}

bool ReviewShellController::setRangeOut(const qint64 frame, const double mediaTime) {
    if (frame < 0) {
        return false;
    }
    outFrame_ = frame;
    outMediaTime_ = mediaTime;
    if (inFrame_ >= 0 && outFrame_ < inFrame_) {
        inFrame_ = -1;
        inMediaTime_ = -1.0;
        rangePlaybackActive_ = false;
        rangeStartPending_ = false;
    }
    Q_EMIT stateChanged();
    return true;
}

void ReviewShellController::remapRange(const qint64 inFrame, const qint64 outFrame) {
    const qint64 nextIn = inMediaTime_ >= 0.0 ? inFrame : -1;
    const qint64 nextOut = outMediaTime_ >= 0.0 ? outFrame : -1;
    if (inFrame_ == nextIn && outFrame_ == nextOut) {
        return;
    }
    inFrame_ = nextIn;
    outFrame_ = nextOut;
    if (inFrame_ < 0 || outFrame_ < inFrame_) {
        rangePlaybackActive_ = false;
        rangeStartPending_ = false;
    }
    Q_EMIT stateChanged();
}

void ReviewShellController::clearRange() {
    if (inFrame_ < 0 && outFrame_ < 0 && inMediaTime_ < 0.0 && outMediaTime_ < 0.0 &&
        !rangePlaybackActive_ && !rangeStartPending_) {
        return;
    }
    inFrame_ = -1;
    outFrame_ = -1;
    inMediaTime_ = -1.0;
    outMediaTime_ = -1.0;
    rangePlaybackActive_ = false;
    rangeStartPending_ = false;
    Q_EMIT stateChanged();
}

bool ReviewShellController::setRangePlaybackState(const bool active, const bool startPending) {
    if (active && (inFrame_ < 0 || outFrame_ < inFrame_)) {
        return false;
    }
    const bool nextPending = active && startPending;
    if (rangePlaybackActive_ == active && rangeStartPending_ == nextPending) {
        return true;
    }
    rangePlaybackActive_ = active;
    rangeStartPending_ = nextPending;
    Q_EMIT stateChanged();
    return true;
}

void ReviewShellController::setRangeStartPending(const bool pending) {
    const bool next = rangePlaybackActive_ && pending;
    if (rangeStartPending_ == next) {
        return;
    }
    rangeStartPending_ = next;
    Q_EMIT stateChanged();
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
    if (!review_.busy()) {
        stagedSources_ = activeSources_;
        stagedReferenceIndex_ = activeSources_.isEmpty() ? 0 : std::max(0, canonicalSourceIndex_);
    }
    Q_EMIT stateChanged();
}

bool ReviewShellController::submitOrQueue(ReviewIntent intent) {
    if (review_.busy() || !reviewIntents_.empty()) {
        enqueueIntent(std::move(intent));
        return true;
    }
    if (submitIntent(intent)) {
        return true;
    }
    Q_EMIT intentRejected(
        QStringLiteral("%1 could not be started.").arg(intentDescription(intent.kind)));
    return false;
}

bool ReviewShellController::submitIntent(const ReviewIntent& intent) {
    switch (intent.kind) {
    case ReviewIntentKind::OpenSources:
        return review_.openSources(intent.sources, intent.referenceIndex);
    case ReviewIntentKind::ReplaceSources:
        return review_.reopenSources(intent.sources, intent.referenceIndex);
    case ReviewIntentKind::ChangeReference:
        return review_.changeReference(intent.referenceIndex);
    case ReviewIntentKind::CloseSources:
        return review_.closeSources();
    }
    return false;
}

void ReviewShellController::enqueueIntent(ReviewIntent intent) {
    if (intent.kind == ReviewIntentKind::OpenSources ||
        intent.kind == ReviewIntentKind::ReplaceSources) {
        const auto existing = std::find_if(
            reviewIntents_.rbegin(), reviewIntents_.rend(), [](const ReviewIntent& queued) {
                return queued.kind == ReviewIntentKind::OpenSources ||
                       queued.kind == ReviewIntentKind::ReplaceSources;
            });
        if (existing != reviewIntents_.rend()) {
            *existing = std::move(intent);
            Q_EMIT stateChanged();
            Q_EMIT intentQueued(QStringLiteral("The newer open request replaced the queued one."));
            return;
        }
    }
    reviewIntents_.push_back(std::move(intent));
    Q_EMIT stateChanged();
    Q_EMIT intentQueued(
        QStringLiteral("%1 queued.").arg(intentDescription(reviewIntents_.back().kind)));
}

void ReviewShellController::drainIntentQueue() {
    if (review_.busy() || reviewIntents_.empty()) {
        return;
    }
    ReviewIntent intent = std::move(reviewIntents_.front());
    reviewIntents_.pop_front();
    Q_EMIT stateChanged();
    if (!submitIntent(intent)) {
        Q_EMIT intentRejected(
            QStringLiteral("%1 could not be started.").arg(intentDescription(intent.kind)));
        if (!review_.busy() && !reviewIntents_.empty()) {
            QMetaObject::invokeMethod(this, [this] { drainIntentQueue(); }, Qt::QueuedConnection);
        }
    }
}

QString ReviewShellController::intentDescription(const ReviewIntentKind kind) {
    switch (kind) {
    case ReviewIntentKind::OpenSources:
        return QStringLiteral("Open videos");
    case ReviewIntentKind::ReplaceSources:
        return QStringLiteral("Update videos");
    case ReviewIntentKind::ChangeReference:
        return QStringLiteral("Change reference");
    case ReviewIntentKind::CloseSources:
        return QStringLiteral("Close videos");
    }
    return QStringLiteral("Request");
}

} // namespace dvs::ui
