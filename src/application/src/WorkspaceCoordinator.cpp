#include "dvs/application/WorkspaceCoordinator.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace dvs::application {
namespace {

[[nodiscard]] domain::MediaError workspaceError(std::string detail) {
    return domain::makeMediaError(domain::MediaErrorCode::kInvalidArgument,
                                  domain::MediaOperation::kSourcePairValidation,
                                  std::nullopt,
                                  false,
                                  std::move(detail));
}

} // namespace

class WorkspaceCoordinator::Impl final {
public:
    explicit Impl(Dependencies dependencies) : dependencies_(std::move(dependencies)) {
        if (!dependencies_.submitPlayback || !dependencies_.playbackSnapshot ||
            !dependencies_.takePlaybackTerminals || !dependencies_.acceptedSequenceAlignments) {
            throw std::invalid_argument{"Workspace coordinator dependencies are required."};
        }
        publishSnapshot();
    }

    ~Impl() {
        shutdown();
    }

    [[nodiscard]] PortSubmitResult submitPlayback(PlaybackCommand command) {
        pump();
        if (stopped_) {
            return PortSubmitResult::Closed;
        }
        const CommandContext context = commandContext(command);
        std::optional<ObservedReviewCommand> observed;
        if (const auto* open = std::get_if<OpenComparisonCommand>(&command)) {
            observed = ObservedReviewCommand{
                .context = context,
                .openIntent = open->intent,
                .displayName = openDisplayName(open->sources),
            };
        }
        const PortSubmitResult result = dependencies_.submitPlayback(std::move(command));
        if (result == PortSubmitResult::Accepted && observed.has_value()) {
            observedReviewCommands_.push_back(std::move(*observed));
        }
        return result;
    }

    [[nodiscard]] std::shared_ptr<const SessionSnapshot> playbackSnapshot() {
        pump();
        return dependencies_.playbackSnapshot();
    }

    [[nodiscard]] std::vector<CommandTerminal> takeCompletedPlaybackCommands() {
        pump();
        std::vector<CommandTerminal> result = std::move(reviewTerminals_);
        reviewTerminals_.clear();
        return result;
    }

    [[nodiscard]] PortSubmitResult closeReview() {
        pump();
        if (stopped_) {
            return PortSubmitResult::Closed;
        }
        if (phase_ != Phase::Idle) {
            return PortSubmitResult::Busy;
        }
        const std::shared_ptr<const SessionSnapshot> playback = dependencies_.playbackSnapshot();
        if (!playback || playback->sessionState == domain::SessionState::kEmpty) {
            resetClosedReview();
            return PortSubmitResult::Accepted;
        }
        phase_ = Phase::ClosingReview;
        state_.busy = true;
        state_.lastError.reset();
        publishSnapshot();
        submitInternal(PlaybackCommand{CloseSessionCommand{
            .context = nextCommandContext(),
        }});
        return pendingPlaybackContext_.has_value() ? PortSubmitResult::Accepted
                                                   : PortSubmitResult::Busy;
    }

    [[nodiscard]] std::shared_ptr<const WorkspaceSnapshot> snapshot() {
        pump();
        return publishedSnapshot_;
    }

    void shutdown() noexcept {
        if (stopped_) {
            return;
        }
        stopped_ = true;
        state_.busy = false;
        publishSnapshot();
    }

private:
    enum class Phase {
        Idle,
        ClosingReview,
    };

    struct ObservedReviewCommand final {
        CommandContext context;
        OpenReviewIntent openIntent = OpenReviewIntent::NewReview;
        std::string displayName;
    };

    [[nodiscard]] static std::string
    openDisplayName(const std::vector<OpenComparisonSource>& sources) {
        std::string result;
        for (const OpenComparisonSource& source : sources) {
            if (source.displayName.empty()) {
                continue;
            }
            if (!result.empty()) {
                result.append(" \xc2\xb7 ");
            }
            result.append(source.displayName);
        }
        return result;
    }

    [[nodiscard]] CommandContext nextCommandContext() {
        const std::shared_ptr<const SessionSnapshot> playback = dependencies_.playbackSnapshot();
        return CommandContext{
            .sessionId = playback ? playback->sessionId : domain::SessionId{0U},
            .sessionEpoch = playback ? playback->sessionEpoch : domain::SessionEpoch{0U},
            .commandId = domain::CommandId{nextCommandId_--},
        };
    }

    void pump() {
        if (pumping_ || stopped_) {
            return;
        }
        pumping_ = true;
        for (CommandTerminal& terminal : dependencies_.takePlaybackTerminals()) {
            handlePlaybackTerminal(std::move(terminal));
        }
        pumping_ = false;
    }

    void handlePlaybackTerminal(CommandTerminal terminal) {
        if (pendingPlaybackContext_.has_value() && terminal.context == *pendingPlaybackContext_) {
            pendingPlaybackContext_.reset();
            if (terminal.outcome != CommandOutcome::Succeeded) {
                fail(terminal.error.value_or(
                    workspaceError("Playback failed while closing the review.")));
                return;
            }
            completeInternalPlaybackStep();
            return;
        }

        const auto observed = std::find_if(observedReviewCommands_.begin(),
                                           observedReviewCommands_.end(),
                                           [&terminal](const ObservedReviewCommand& command) {
                                               return command.context == terminal.context;
                                           });
        if (observed != observedReviewCommands_.end()) {
            if (terminal.outcome == CommandOutcome::Succeeded) {
                applyObservedOpen(*observed);
                publishSnapshot();
            }
            observedReviewCommands_.erase(observed);
        }
        reviewTerminals_.push_back(std::move(terminal));
    }

    void completeInternalPlaybackStep() {
        if (phase_ == Phase::ClosingReview) {
            resetClosedReview();
        }
    }

    void applyObservedOpen(const ObservedReviewCommand& observed) {
        if (!observed.displayName.empty()) {
            state_.displayName = observed.displayName;
        }
        if (observed.openIntent == OpenReviewIntent::NewReview) {
            state_.sourceDiagnostics.clear();
        }
    }

    void submitInternal(PlaybackCommand command) {
        const CommandContext context = commandContext(command);
        const PortSubmitResult result = dependencies_.submitPlayback(std::move(command));
        if (result != PortSubmitResult::Accepted) {
            fail(workspaceError("Playback coordinator rejected a review lifecycle command."));
            return;
        }
        pendingPlaybackContext_ = context;
        state_.busy = true;
        publishSnapshot();
    }

    void resetClosedReview() {
        phase_ = Phase::Idle;
        pendingPlaybackContext_.reset();
        observedReviewCommands_.clear();
        state_.busy = false;
        state_.displayName.clear();
        state_.sourceDiagnostics.clear();
        state_.lastError.reset();
        publishSnapshot();
    }

    void fail(domain::MediaError error) {
        phase_ = Phase::Idle;
        pendingPlaybackContext_.reset();
        observedReviewCommands_.clear();
        state_.busy = false;
        state_.lastError = std::move(error);
        publishSnapshot();
    }

    void publishSnapshot() {
        publishedSnapshot_ = std::make_shared<const WorkspaceSnapshot>(state_);
        if (dependencies_.statePublished) {
            try {
                dependencies_.statePublished();
            } catch (...) {
                // Publication callbacks are wake-up hints and must not alter coordinator state.
            }
        }
    }

    Dependencies dependencies_;
    Phase phase_ = Phase::Idle;
    WorkspaceSnapshot state_;
    std::shared_ptr<const WorkspaceSnapshot> publishedSnapshot_;
    std::optional<CommandContext> pendingPlaybackContext_;
    std::vector<ObservedReviewCommand> observedReviewCommands_;
    std::vector<CommandTerminal> reviewTerminals_;
    std::uint64_t nextCommandId_ = std::numeric_limits<std::uint64_t>::max();
    bool pumping_ = false;
    bool stopped_ = false;
};

std::unique_ptr<WorkspaceCoordinator> WorkspaceCoordinator::create(Dependencies dependencies) {
    try {
        return std::unique_ptr<WorkspaceCoordinator>{
            new WorkspaceCoordinator{std::move(dependencies)}};
    } catch (...) {
        return {};
    }
}

WorkspaceCoordinator::WorkspaceCoordinator(Dependencies dependencies)
    : impl_(std::make_unique<Impl>(std::move(dependencies))) {}

WorkspaceCoordinator::~WorkspaceCoordinator() = default;

PortSubmitResult WorkspaceCoordinator::submitPlayback(PlaybackCommand command) {
    return impl_->submitPlayback(std::move(command));
}

std::shared_ptr<const SessionSnapshot> WorkspaceCoordinator::playbackSnapshot() {
    return impl_->playbackSnapshot();
}

std::vector<CommandTerminal> WorkspaceCoordinator::takeCompletedPlaybackCommands() {
    return impl_->takeCompletedPlaybackCommands();
}

PortSubmitResult WorkspaceCoordinator::closeReview() {
    return impl_->closeReview();
}

std::shared_ptr<const WorkspaceSnapshot> WorkspaceCoordinator::snapshot() {
    return impl_->snapshot();
}

void WorkspaceCoordinator::shutdown() noexcept {
    impl_->shutdown();
}

} // namespace dvs::application
