#include "dvs/ui/ReviewController.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dvs::ui {
namespace {

constexpr int kProjectionIntervalMilliseconds = 16;

struct LocalFileCandidate final {
    std::filesystem::path path;
    QString filename;
};

struct LocalFileValidation final {
    std::optional<LocalFileCandidate> candidate;
    QString errorKey;
};

[[nodiscard]] LocalFileValidation validateLocalFile(const QUrl& url) {
    if (!url.isValid() || !url.isLocalFile() || url.toLocalFile().isEmpty()) {
        return LocalFileValidation{.errorKey = QStringLiteral("invalid-argument")};
    }

    const QFileInfo input{url.toLocalFile()};
    const QString canonicalPath = input.canonicalFilePath();
    if (!input.isFile() || canonicalPath.isEmpty()) {
        return LocalFileValidation{.errorKey = QStringLiteral("source-missing")};
    }

    const QFileInfo canonical{canonicalPath};
    return LocalFileValidation{
        .candidate =
            LocalFileCandidate{
                .path = std::filesystem::path{canonicalPath.toStdWString()},
                .filename = canonical.fileName(),
            },
    };
}

[[nodiscard]] ReviewController::ReviewDisplayState
mapDisplayState(const domain::SessionState state) noexcept {
    switch (state) {
    case domain::SessionState::kEmpty:
        return ReviewController::ReviewDisplayState::Empty;
    case domain::SessionState::kLoading:
        return ReviewController::ReviewDisplayState::Loading;
    case domain::SessionState::kReady:
        return ReviewController::ReviewDisplayState::Ready;
    case domain::SessionState::kInvalid:
        return ReviewController::ReviewDisplayState::Invalid;
    case domain::SessionState::kError:
        return ReviewController::ReviewDisplayState::Error;
    }
    return ReviewController::ReviewDisplayState::Error;
}

struct ReviewView final {
    QString sourceAFilename;
    QString sourceBFilename;
    ReviewController::ReviewDisplayState displayState = ReviewController::ReviewDisplayState::Empty;
    bool busy = false;
    bool playing = false;
    bool graphicsReady = false;
    qint64 currentFrame = -1;
    qulonglong totalFrames = 0U;
    QString sourceAErrorKey;
    QString sourceBErrorKey;
    QString pairErrorKey;
    bool canOpen = false;
    bool canFirst = false;
    bool canPrevious = false;
    bool canNext = false;
    bool canLast = false;
    bool canPlay = false;
    bool canPause = false;

    [[nodiscard]] bool operator==(const ReviewView&) const = default;
};

} // namespace

class ReviewController::Impl final {
public:
    Impl(ReviewController& owner, Dependencies dependencies)
        : owner_(owner), dependencies_(std::move(dependencies)) {
        if (!dependencies_.submit || !dependencies_.snapshot ||
            !dependencies_.takeCompletedCommands) {
            throw std::invalid_argument{"Review controller dependencies must not be empty."};
        }

        QObject::connect(&projectionTimer_, &QTimer::timeout, &owner_, [this] { refresh(); });
        projectionTimer_.setInterval(kProjectionIntervalMilliseconds);
        refresh();
        if (!stopped_) {
            projectionTimer_.start();
        }
    }

    ~Impl() {
        projectionTimer_.stop();
    }

    [[nodiscard]] const ReviewView& view() const noexcept {
        return view_;
    }

    [[nodiscard]] bool openPair(const QUrl& sourceA, const QUrl& sourceB) {
        if (!onOwnerThread() || stopped_) {
            return false;
        }
        refresh();
        if (stopped_ || pendingCommand_.has_value()) {
            return false;
        }

        LocalFileValidation validatedA;
        LocalFileValidation validatedB;
        try {
            validatedA = validateLocalFile(sourceA);
            validatedB = validateLocalFile(sourceB);
        } catch (...) {
            validatedA = LocalFileValidation{.errorKey = QStringLiteral("invalid-argument")};
            validatedB = LocalFileValidation{.errorKey = QStringLiteral("invalid-argument")};
        }

        sourceA_ = std::move(validatedA.candidate);
        sourceB_ = std::move(validatedB.candidate);
        localSourceAErrorKey_ = std::move(validatedA.errorKey);
        localSourceBErrorKey_ = std::move(validatedB.errorKey);
        publishProjection();

        if (!sourceA_.has_value() || !sourceB_.has_value() || !view_.canOpen) {
            return false;
        }
        const std::optional<application::CommandContext> context = allocateCommandContext();
        if (!context.has_value()) {
            failClosed();
            return false;
        }
        return dispatch(application::OpenSourcePathsCommand{
            .context = *context,
            .sourceAPath = sourceA_->path,
            .sourceBPath = sourceB_->path,
        });
    }

    [[nodiscard]] bool first() {
        return dispatchNavigation([](const ReviewView& view) { return view.canFirst; },
                                  [](const application::CommandContext& context) {
                                      return application::PlaybackCommand{
                                          application::FirstFrameCommand{.context = context}};
                                  });
    }

    [[nodiscard]] bool previous() {
        return stepFrames(-1);
    }

    [[nodiscard]] bool next() {
        return stepFrames(1);
    }

    [[nodiscard]] bool last() {
        return dispatchNavigation([](const ReviewView& view) { return view.canLast; },
                                  [](const application::CommandContext& context) {
                                      return application::PlaybackCommand{
                                          application::LastFrameCommand{.context = context}};
                                  });
    }

    [[nodiscard]] bool stepFrames(const qint64 delta) {
        if (delta == 0) {
            return false;
        }
        return dispatchNavigation(
            [delta](const ReviewView& view) { return delta < 0 ? view.canPrevious : view.canNext; },
            [delta](const application::CommandContext& context) {
                return application::PlaybackCommand{application::StepFramesCommand{
                    .context = context,
                    .delta = static_cast<std::int64_t>(delta),
                }};
            });
    }

    [[nodiscard]] bool seekFrame(const qint64 frame) {
        return dispatchNavigation(
            [frame](const ReviewView& view) {
                return view.canFirst && frame >= 0 &&
                       static_cast<qulonglong>(frame) < view.totalFrames &&
                       frame != view.currentFrame;
            },
            [frame](const application::CommandContext& context) {
                return application::PlaybackCommand{application::SeekFrameCommand{
                    .context = context,
                    .frameId = domain::FrameId{static_cast<std::int64_t>(frame)},
                }};
            });
    }

    [[nodiscard]] bool play() {
        return dispatchTransport([](const ReviewView& view) { return view.canPlay; },
                                 [](const application::CommandContext& context) {
                                     return application::PlaybackCommand{
                                         application::PlayCommand{.context = context}};
                                 });
    }

    [[nodiscard]] bool pause() {
        return dispatchTransport([](const ReviewView& view) { return view.canPause; },
                                 [](const application::CommandContext& context) {
                                     return application::PlaybackCommand{
                                         application::PauseCommand{.context = context}};
                                 });
    }

    [[nodiscard]] bool togglePlayback() {
        if (!onOwnerThread() || stopped_) {
            return false;
        }
        refresh();
        if (stopped_) {
            return false;
        }
        return view_.playing ? pause() : play();
    }

    void stop() noexcept {
        if (stopped_) {
            return;
        }
        stopped_ = true;
        projectionTimer_.stop();
        pendingCommand_.reset();
        pendingTransportCommand_.reset();
        publishProjection();
    }

private:
    [[nodiscard]] bool onOwnerThread() const noexcept {
        return owner_.thread() == QThread::currentThread();
    }

    void refresh() noexcept {
        if (stopped_ || !onOwnerThread()) {
            return;
        }

        try {
            for (const application::CommandTerminal& terminal :
                 dependencies_.takeCompletedCommands()) {
                if (pendingCommand_.has_value() && terminal.context == *pendingCommand_) {
                    pendingCommand_.reset();
                }
                if (pendingTransportCommand_.has_value() &&
                    terminal.context == *pendingTransportCommand_) {
                    pendingTransportCommand_.reset();
                }
            }

            std::shared_ptr<const application::SessionSnapshot> snapshot = dependencies_.snapshot();
            if (!snapshot || !snapshot->isConsistent()) {
                failClosed();
                return;
            }
            snapshot_ = std::move(snapshot);
            publishProjection();
        } catch (...) {
            failClosed();
        }
    }

    void failClosed() noexcept {
        stopped_ = true;
        projectionTimer_.stop();
        pendingCommand_.reset();
        pendingTransportCommand_.reset();
        publishProjection();
    }

    void publishProjection() noexcept {
        ReviewView next;
        next.sourceAFilename = sourceA_.has_value() ? sourceA_->filename : QString{};
        next.sourceBFilename = sourceB_.has_value() ? sourceB_->filename : QString{};
        next.sourceAErrorKey = localSourceAErrorKey_;
        next.sourceBErrorKey = localSourceBErrorKey_;

        if (snapshot_) {
            next.displayState = mapDisplayState(snapshot_->sessionState);
            next.graphicsReady = snapshot_->graphicsReady && !stopped_;
            next.currentFrame = snapshot_->displayedFrame.has_value()
                                    ? static_cast<qint64>(snapshot_->displayedFrame->value())
                                    : -1;
            next.totalFrames = static_cast<qulonglong>(snapshot_->canonicalFrameCount);
            next.playing = snapshot_->playbackState == domain::PlaybackState::kPlaying ||
                           snapshot_->playbackState == domain::PlaybackState::kBuffering;

            if (snapshot_->lastError.has_value()) {
                const QString key = QString::fromStdString(snapshot_->lastError->userMessageKey);
                switch (snapshot_->lastError->sourceRole) {
                case domain::SourceRole::kA:
                    if (next.sourceAErrorKey.isEmpty()) {
                        next.sourceAErrorKey = key;
                    }
                    break;
                case domain::SourceRole::kB:
                    if (next.sourceBErrorKey.isEmpty()) {
                        next.sourceBErrorKey = key;
                    }
                    break;
                case domain::SourceRole::kNone:
                case domain::SourceRole::kPair:
                case domain::SourceRole::kProject:
                    next.pairErrorKey = key;
                    break;
                }
            }
        }

        next.busy = pendingCommand_.has_value() && !stopped_;
        const bool transportPending = pendingTransportCommand_.has_value();
        const bool playbackDraining =
            snapshot_ && !next.playing && snapshot_->requestedFrame.has_value();
        const bool playbackBlocksCommands = next.playing || playbackDraining || transportPending;
        next.canOpen = next.graphicsReady && !next.busy && !playbackBlocksCommands;
        const bool canNavigate = next.graphicsReady && !next.busy && !playbackBlocksCommands &&
                                 next.displayState == ReviewDisplayState::Ready &&
                                 next.totalFrames != 0U;
        next.canFirst = canNavigate;
        next.canLast = canNavigate;
        next.canPrevious = canNavigate && next.currentFrame > 0;
        next.canNext = canNavigate && next.currentFrame >= 0 &&
                       static_cast<qulonglong>(next.currentFrame) + 1U < next.totalFrames;
        next.canPlay = next.graphicsReady && !next.busy && !playbackBlocksCommands &&
                       next.displayState == ReviewDisplayState::Ready && next.currentFrame >= 0 &&
                       next.totalFrames > 1U;
        next.canPause = !next.busy && !transportPending && next.playing;

        if (!(next == view_)) {
            view_ = std::move(next);
            Q_EMIT owner_.stateChanged();
        }
    }

    [[nodiscard]] std::optional<application::CommandContext> allocateCommandContext() noexcept {
        if (!snapshot_ || commandIdsExhausted_) {
            return std::nullopt;
        }
        const std::uint64_t commandId = nextCommandId_;
        if (nextCommandId_ == (std::numeric_limits<std::uint64_t>::max)()) {
            commandIdsExhausted_ = true;
        } else {
            ++nextCommandId_;
        }
        return application::CommandContext{
            .sessionId = snapshot_->sessionId,
            .sessionEpoch = snapshot_->sessionEpoch,
            .commandId = domain::CommandId{commandId},
        };
    }

    [[nodiscard]] bool dispatch(application::PlaybackCommand command) noexcept {
        const application::CommandContext context = application::commandContext(command);
        application::PortSubmitResult result = application::PortSubmitResult::Closed;
        try {
            result = dependencies_.submit(std::move(command));
        } catch (...) {
            failClosed();
            return false;
        }
        if (result != application::PortSubmitResult::Accepted) {
            return false;
        }
        pendingCommand_ = context;
        publishProjection();
        return true;
    }

    template <typename Enabled, typename Factory>
    [[nodiscard]] bool dispatchNavigation(Enabled enabled, Factory factory) {
        if (!onOwnerThread() || stopped_) {
            return false;
        }
        refresh();
        if (stopped_) {
            return false;
        }

        if (!enabled(view_)) {
            return false;
        }
        const std::optional<application::CommandContext> context = allocateCommandContext();
        if (!context.has_value()) {
            failClosed();
            return false;
        }
        return dispatch(factory(*context));
    }

    template <typename Enabled, typename Factory>
    [[nodiscard]] bool dispatchTransport(Enabled enabled, Factory factory) {
        if (!onOwnerThread() || stopped_) {
            return false;
        }
        refresh();
        if (stopped_ || !enabled(view_)) {
            return false;
        }
        const std::optional<application::CommandContext> context = allocateCommandContext();
        if (!context.has_value()) {
            failClosed();
            return false;
        }

        application::PlaybackCommand command = factory(*context);
        application::PortSubmitResult result = application::PortSubmitResult::Closed;
        try {
            result = dependencies_.submit(std::move(command));
        } catch (...) {
            failClosed();
            return false;
        }
        if (result != application::PortSubmitResult::Accepted) {
            return false;
        }
        pendingTransportCommand_ = *context;
        publishProjection();
        return true;
    }

    ReviewController& owner_;
    Dependencies dependencies_;
    QTimer projectionTimer_;
    std::shared_ptr<const application::SessionSnapshot> snapshot_;
    std::optional<LocalFileCandidate> sourceA_;
    std::optional<LocalFileCandidate> sourceB_;
    QString localSourceAErrorKey_;
    QString localSourceBErrorKey_;
    std::optional<application::CommandContext> pendingCommand_;
    std::optional<application::CommandContext> pendingTransportCommand_;
    std::uint64_t nextCommandId_ = 1U;
    bool commandIdsExhausted_ = false;
    bool stopped_ = false;
    ReviewView view_;
};

ReviewController::ReviewController(Dependencies dependencies, QObject* const parent)
    : QObject(parent), impl_(std::make_unique<Impl>(*this, std::move(dependencies))) {}

ReviewController::~ReviewController() = default;

QString ReviewController::sourceAFilename() const {
    return impl_->view().sourceAFilename;
}

QString ReviewController::sourceBFilename() const {
    return impl_->view().sourceBFilename;
}

ReviewController::ReviewDisplayState ReviewController::displayState() const noexcept {
    return impl_->view().displayState;
}

bool ReviewController::busy() const noexcept {
    return impl_->view().busy;
}

bool ReviewController::playing() const noexcept {
    return impl_->view().playing;
}

bool ReviewController::graphicsReady() const noexcept {
    return impl_->view().graphicsReady;
}

qint64 ReviewController::currentFrame() const noexcept {
    return impl_->view().currentFrame;
}

qulonglong ReviewController::totalFrames() const noexcept {
    return impl_->view().totalFrames;
}

QString ReviewController::sourceAErrorKey() const {
    return impl_->view().sourceAErrorKey;
}

QString ReviewController::sourceBErrorKey() const {
    return impl_->view().sourceBErrorKey;
}

QString ReviewController::pairErrorKey() const {
    return impl_->view().pairErrorKey;
}

bool ReviewController::canOpen() const noexcept {
    return impl_->view().canOpen;
}

bool ReviewController::canFirst() const noexcept {
    return impl_->view().canFirst;
}

bool ReviewController::canPrevious() const noexcept {
    return impl_->view().canPrevious;
}

bool ReviewController::canNext() const noexcept {
    return impl_->view().canNext;
}

bool ReviewController::canLast() const noexcept {
    return impl_->view().canLast;
}

bool ReviewController::canPlay() const noexcept {
    return impl_->view().canPlay;
}

bool ReviewController::canPause() const noexcept {
    return impl_->view().canPause;
}

bool ReviewController::openPair(const QUrl& sourceA, const QUrl& sourceB) {
    return impl_->openPair(sourceA, sourceB);
}

bool ReviewController::first() {
    return impl_->first();
}

bool ReviewController::previous() {
    return impl_->previous();
}

bool ReviewController::next() {
    return impl_->next();
}

bool ReviewController::last() {
    return impl_->last();
}

bool ReviewController::stepFrames(const qint64 delta) {
    return impl_->stepFrames(delta);
}

bool ReviewController::seekFrame(const qint64 frame) {
    return impl_->seekFrame(frame);
}

bool ReviewController::play() {
    return impl_->play();
}

bool ReviewController::pause() {
    return impl_->pause();
}

bool ReviewController::togglePlayback() {
    return impl_->togglePlayback();
}

void ReviewController::stop() noexcept {
    if (thread() != QThread::currentThread()) {
        static_cast<void>(QMetaObject::invokeMethod(this, "stop", Qt::QueuedConnection));
        return;
    }
    impl_->stop();
}

} // namespace dvs::ui
