#include "dvs/application/WorkspaceCoordinator.h"

#include "dvs/application/AlignmentCacheIdentity.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace dvs::application {
namespace {

class WorkspaceEventInbox final : public IApplicationEventSink {
public:
    [[nodiscard]] EventPostResult postCritical(ApplicationEvent event) noexcept override {
        try {
            const std::scoped_lock lock(mutex_);
            if (closed_ || events_.size() == kCapacity) {
                return EventPostResult::Closed;
            }
            events_.push_back(std::move(event));
            return EventPostResult::Accepted;
        } catch (...) {
            return EventPostResult::Closed;
        }
    }

    [[nodiscard]] EventPostResult postRealtime(ApplicationEvent event) noexcept override {
        return postCritical(std::move(event));
    }

    void closeRealtimeIngress() noexcept override {
        close();
    }

    void closeCriticalIngress() noexcept override {
        close();
    }

    [[nodiscard]] std::vector<ApplicationEvent> take() {
        const std::scoped_lock lock(mutex_);
        std::vector<ApplicationEvent> result;
        result.reserve(events_.size());
        while (!events_.empty()) {
            result.push_back(std::move(events_.front()));
            events_.pop_front();
        }
        return result;
    }

private:
    void close() noexcept {
        const std::scoped_lock lock(mutex_);
        closed_ = true;
        events_.clear();
    }

    static constexpr std::size_t kCapacity = 64U;
    std::mutex mutex_;
    std::deque<ApplicationEvent> events_;
    bool closed_ = false;
};

[[nodiscard]] domain::MediaError workspaceError(std::string detail) {
    return domain::makeMediaError(domain::MediaErrorCode::kInvalidArgument,
                                  domain::MediaOperation::kProjectMutation,
                                  std::nullopt,
                                  false,
                                  std::move(detail));
}

[[nodiscard]] RequestContext requestContext(const EventContext& context) {
    return std::visit(
        [](const auto& value) -> RequestContext {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, RequestContext>) {
                return value;
            } else if constexpr (std::is_same_v<Value, PlaybackRequestContext>) {
                return value.request;
            } else if constexpr (std::is_same_v<Value, FrameRequestContext>) {
                return value.playback.request;
            } else {
                return value.request;
            }
        },
        context);
}

} // namespace

class WorkspaceCoordinator::Impl final {
public:
    explicit Impl(Dependencies dependencies)
        : dependencies_(std::move(dependencies)), events_(std::make_shared<WorkspaceEventInbox>()) {
        if (!dependencies_.projectRepository || !dependencies_.submitPlayback ||
            !dependencies_.playbackSnapshot || !dependencies_.takePlaybackTerminals ||
            !dependencies_.acceptedSequenceAlignments || !dependencies_.createProjectId) {
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
        const bool opensComparison = std::holds_alternative<OpenComparisonCommand>(command) ||
                                     std::holds_alternative<OpenDirectComparisonCommand>(command);
        const bool changesProject =
            opensComparison || std::holds_alternative<SetAlignmentOffsetsCommand>(command) ||
            std::holds_alternative<SetManualAlignmentAnchorCommand>(command) ||
            std::holds_alternative<ClearManualAlignmentAnchorsCommand>(command) ||
            std::holds_alternative<ConfirmAutomaticAlignmentCommand>(command) ||
            std::holds_alternative<UndoAutomaticAlignmentCommand>(command) ||
            std::holds_alternative<SeekFrameCommand>(command) ||
            std::holds_alternative<StepFramesCommand>(command) ||
            std::holds_alternative<FirstFrameCommand>(command) ||
            std::holds_alternative<LastFrameCommand>(command);
        const PortSubmitResult result = dependencies_.submitPlayback(std::move(command));
        if (result == PortSubmitResult::Accepted && (opensComparison || changesProject)) {
            observedReviewCommands_.push_back(
                ObservedReviewCommand{.context = context, .opensComparison = opensComparison});
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

    [[nodiscard]] PortSubmitResult openProject(const std::filesystem::path& projectPath) {
        pump();
        if (stopped_) {
            return PortSubmitResult::Closed;
        }
        if (phase_ != Phase::Idle) {
            return PortSubmitResult::Busy;
        }
        if (projectPath.empty()) {
            setError(workspaceError("Project path must not be empty."));
            return PortSubmitResult::Busy;
        }

        const RequestContext context = nextRequestContext();
        const PortSubmitResult result = dependencies_.projectRepository->submit(
            ProjectLoadRequest{.context = context, .projectPath = projectPath}, events_);
        if (result != PortSubmitResult::Accepted) {
            return result;
        }
        phase_ = Phase::LoadingProject;
        pendingRepositoryContext_ = context;
        pendingProjectPath_ = projectPath;
        pendingProject_.reset();
        pendingDiagnostics_.clear();
        pendingRepositoryPayload_ = false;
        state_.busy = true;
        state_.lastError.reset();
        publishSnapshot();
        return result;
    }

    [[nodiscard]] PortSubmitResult saveProject(const std::filesystem::path& projectPath,
                                               std::string displayName,
                                               const domain::ProjectViewState viewState) {
        pump();
        if (stopped_) {
            return PortSubmitResult::Closed;
        }
        if (phase_ != Phase::Idle) {
            return PortSubmitResult::Busy;
        }
        const std::shared_ptr<const SessionSnapshot> playback = dependencies_.playbackSnapshot();
        if (projectPath.empty() || !playback ||
            playback->sessionState != domain::SessionState::kReady ||
            !playback->displayedFrame.has_value() || !playback->validatedComparison) {
            setError(workspaceError("Saving requires a ready, freshly validated comparison."));
            return PortSubmitResult::Busy;
        }
        if (displayName.empty()) {
            displayName = projectPath.stem().string();
        }
        if (displayName.empty()) {
            displayName = "VCStation Review Project";
        }

        domain::Result<domain::Project> prepared =
            currentProject_.has_value()
                ? currentProject_->replaceSources(*playback->validatedComparison)
                : domain::Project::create(
                      dependencies_.createProjectId(), displayName, *playback->validatedComparison);
        if (!prepared) {
            setError(prepared.error());
            return PortSubmitResult::Busy;
        }
        domain::Project project = std::move(prepared).value();
        auto status = project.setLastDisplayedFrame(*playback->displayedFrame);
        if (!status) {
            setError(status.error());
            return PortSubmitResult::Busy;
        }
        const std::shared_ptr<const std::vector<SequenceAlignmentResult>> sequenceAlignments =
            dependencies_.acceptedSequenceAlignments();
        status = project.setAlignmentState(alignmentStateFor(*playback, sequenceAlignments));
        if (!status) {
            setError(status.error());
            return PortSubmitResult::Busy;
        }
        status = project.setViewState(viewState);
        if (!status) {
            setError(status.error());
            return PortSubmitResult::Busy;
        }

        const RequestContext request = nextRequestContext();
        const SaveRequestContext context{
            .request = request,
            .projectRevision = domain::ProjectRevision{++projectRevision_},
        };
        const PortSubmitResult result = dependencies_.projectRepository->submit(
            ProjectSaveRequest{
                .context = context,
                .projectPath = projectPath,
                .project = project,
                .derivedAlignmentResults = sequenceAlignments,
            },
            events_);
        if (result != PortSubmitResult::Accepted) {
            return result;
        }
        phase_ = Phase::SavingProject;
        pendingRepositoryContext_ = request;
        pendingProjectPath_ = projectPath;
        pendingProject_ = std::move(project);
        pendingRepositoryPayload_ = false;
        state_.busy = true;
        state_.lastError.reset();
        publishSnapshot();
        return result;
    }

    [[nodiscard]] PortSubmitResult relinkSource(const domain::SourceId sourceId,
                                                const std::filesystem::path& sourcePath) {
        pump();
        if (stopped_) {
            return PortSubmitResult::Closed;
        }
        if (phase_ != Phase::Idle) {
            return PortSubmitResult::Busy;
        }
        if (!currentProject_.has_value() || currentProject_->sources().find(sourceId) == nullptr) {
            setError(workspaceError("Relink requires a loaded project source."));
            return PortSubmitResult::Busy;
        }
        const std::shared_ptr<const SessionSnapshot> playback = dependencies_.playbackSnapshot();
        relinkHadReadyPlayback_ = playback &&
                                  playback->sessionState == domain::SessionState::kReady &&
                                  playback->validatedComparison;
        relinkOriginalDirty_ = state_.dirty;
        relinkOriginalDiagnostics_ = state_.sourceDiagnostics;
        rollbackDerivedAlignment_ = dependencies_.acceptedSequenceAlignments();
        const RequestContext context = nextRequestContext();
        const PortSubmitResult result = dependencies_.projectRepository->submit(
            ProjectRelinkRequest{
                .context = context,
                .sourceId = sourceId,
                .newSourcePath = sourcePath,
            },
            events_);
        if (result != PortSubmitResult::Accepted) {
            return result;
        }
        phase_ = Phase::PreparingRelink;
        pendingRepositoryContext_ = context;
        pendingRelinkSource_ = sourceId;
        pendingRelinkPath_.reset();
        pendingRepositoryPayload_ = false;
        state_.busy = true;
        state_.lastError.reset();
        publishSnapshot();
        return result;
    }

    [[nodiscard]] std::shared_ptr<const WorkspaceSnapshot> snapshot() {
        pump();
        return publishedSnapshot_;
    }

    void markViewDirty() {
        pump();
        if (!stopped_ && currentProject_.has_value() && !state_.dirty) {
            state_.dirty = true;
            publishSnapshot();
        }
    }

    void shutdown() noexcept {
        if (stopped_) {
            return;
        }
        stopped_ = true;
        if (pendingRepositoryContext_.has_value()) {
            dependencies_.projectRepository->cancel(*pendingRepositoryContext_);
        }
        events_->closeCriticalIngress();
        state_.busy = false;
        publishSnapshot();
    }

private:
    enum class Phase {
        Idle,
        LoadingProject,
        OpeningProject,
        PreparingRelink,
        OpeningRelink,
        OpeningRelinkRollback,
        ClosingFailedRelink,
        ApplyingOffsets,
        ApplyingSequenceAlignment,
        ApplyingAnchors,
        SeekingProjectFrame,
        SavingProject,
    };

    struct ObservedReviewCommand final {
        CommandContext context;
        bool opensComparison = false;
    };

    [[nodiscard]] RequestContext nextRequestContext() {
        const std::shared_ptr<const SessionSnapshot> playback = dependencies_.playbackSnapshot();
        return RequestContext{
            .sessionId = playback ? playback->sessionId : domain::SessionId{0U},
            .sessionEpoch = playback ? playback->sessionEpoch : domain::SessionEpoch{0U},
            .requestId = domain::RequestId{nextRequestId_++},
        };
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
        for (ApplicationEvent& event : events_->take()) {
            handleRepositoryEvent(std::move(event));
        }
        for (CommandTerminal& terminal : dependencies_.takePlaybackTerminals()) {
            handlePlaybackTerminal(std::move(terminal));
        }
        pumping_ = false;
    }

    void handleRepositoryEvent(ApplicationEvent event) {
        std::visit(
            [this](auto&& value) {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, ProjectLoaded>) {
                    if (phase_ == Phase::LoadingProject && pendingRepositoryContext_.has_value() &&
                        value.context == *pendingRepositoryContext_) {
                        pendingProject_ = std::move(value.project);
                        pendingDiagnostics_ = std::move(value.sourceDiagnostics);
                        pendingDerivedAlignment_ = std::move(value.derivedAlignmentResults);
                        pendingAlignmentCacheError_ = std::move(value.alignmentCacheError);
                        pendingRepositoryPayload_ = true;
                    }
                } else if constexpr (std::is_same_v<Value, SourceRelinkPrepared>) {
                    if (phase_ == Phase::PreparingRelink && pendingRepositoryContext_.has_value() &&
                        value.context == *pendingRepositoryContext_) {
                        pendingRelinkPath_ = value.candidate.normalizedPath();
                        pendingRepositoryPayload_ = true;
                    }
                } else if constexpr (std::is_same_v<Value, ProjectSaved>) {
                    if (phase_ == Phase::SavingProject && pendingRepositoryContext_.has_value() &&
                        value.context.request == *pendingRepositoryContext_) {
                        pendingRepositoryPayload_ = true;
                    }
                } else if constexpr (std::is_same_v<Value, RequestTerminal>) {
                    handleRepositoryTerminal(std::move(value));
                }
            },
            std::move(event));
    }

    void handleRepositoryTerminal(RequestTerminal terminal) {
        std::visit(
            [this](auto&& value) {
                const RequestContext context = requestContext(value.context);
                if (!pendingRepositoryContext_.has_value() ||
                    context != *pendingRepositoryContext_) {
                    return;
                }
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, RequestSucceeded>) {
                    if (!pendingRepositoryPayload_) {
                        fail(workspaceError("Project repository completed without its payload."));
                        return;
                    }
                    pendingRepositoryContext_.reset();
                    pendingRepositoryPayload_ = false;
                    if (phase_ == Phase::LoadingProject) {
                        completeProjectLoad();
                    } else if (phase_ == Phase::PreparingRelink) {
                        beginRelinkOpen();
                    } else if (phase_ == Phase::SavingProject) {
                        completeProjectSave();
                    }
                } else if constexpr (std::is_same_v<Value, RequestFailed>) {
                    fail(std::move(value.error));
                } else {
                    fail(workspaceError("Project operation was canceled."));
                }
            },
            std::move(terminal));
    }

    void completeProjectLoad() {
        if (!pendingProject_.has_value()) {
            fail(workspaceError("Loaded project payload is unavailable."));
            return;
        }
        restoreDegraded_ = false;
        if (pendingProject_->alignmentState().mode ==
                domain::ProjectAlignmentMode::kAutomaticSequence &&
            !pendingDerivedAlignment_) {
            auto status = pendingProject_->setAlignmentState(domain::ProjectAlignmentState{});
            if (!status) {
                fail(status.error());
                return;
            }
            restoreDegraded_ = true;
            state_.lastError = pendingAlignmentCacheError_.value_or(workspaceError(
                "Derived alignment cache is unavailable; strict index was restored."));
        }
        const bool relinkRequired = std::any_of(pendingDiagnostics_.begin(),
                                                pendingDiagnostics_.end(),
                                                [](const SourceRevalidationDiagnostic& diagnostic) {
                                                    return diagnostic.error.has_value();
                                                });
        if (relinkRequired) {
            currentProject_ = std::move(pendingProject_);
            currentProjectPath_ = pendingProjectPath_;
            state_.sourceDiagnostics = std::move(pendingDiagnostics_);
            phase_ = Phase::Idle;
            state_.busy = false;
            state_.dirty = restoreDegraded_;
            pendingProject_.reset();
            pendingDerivedAlignment_.reset();
            pendingAlignmentCacheError_.reset();
            updateProjectProjection();
            publishSnapshot();
            return;
        }
        beginProjectOpen(Phase::OpeningProject);
    }

    void beginRelinkOpen() {
        if (!currentProject_.has_value() || !pendingRelinkSource_.has_value() ||
            !pendingRelinkPath_.has_value()) {
            fail(workspaceError("Prepared relink payload is incomplete."));
            return;
        }
        pendingProject_ = currentProject_;
        phaseWasRelink_ = true;
        beginProjectOpen(Phase::OpeningRelink);
    }

    void beginProjectOpen(const Phase nextPhase) {
        if (!pendingProject_.has_value()) {
            fail(workspaceError("Project open has no project state."));
            return;
        }
        std::vector<OpenComparisonSource> sources;
        sources.reserve(pendingProject_->sources().sourceCount());
        for (const domain::ComparisonSource& source : pendingProject_->sources().sources()) {
            std::filesystem::path path = source.descriptor.normalizedPath;
            if (nextPhase == Phase::OpeningRelink && pendingRelinkSource_.has_value() &&
                source.id == *pendingRelinkSource_) {
                path = *pendingRelinkPath_;
            }
            sources.push_back(OpenComparisonSource{
                .path = std::move(path),
                .role = source.role,
                .displayName = source.displayName,
            });
        }
        phase_ = nextPhase;
        submitInternal(PlaybackCommand{OpenComparisonCommand{
            .context = nextCommandContext(),
            .sources = std::move(sources),
        }});
    }

    void submitInternal(PlaybackCommand command) {
        const CommandContext context = commandContext(command);
        const PortSubmitResult result = dependencies_.submitPlayback(std::move(command));
        if (result != PortSubmitResult::Accepted) {
            const domain::MediaError error =
                workspaceError("Playback coordinator rejected a project restore command.");
            if (phaseWasRelink_ && !rollingBack_) {
                beginRelinkRollback(error);
            } else {
                fail(error);
            }
            return;
        }
        pendingPlaybackContext_ = context;
        state_.busy = true;
        publishSnapshot();
    }

    void handlePlaybackTerminal(CommandTerminal terminal) {
        if (pendingPlaybackContext_.has_value() && terminal.context == *pendingPlaybackContext_) {
            pendingPlaybackContext_.reset();
            if (terminal.outcome != CommandOutcome::Succeeded) {
                const domain::MediaError error = terminal.error.value_or(
                    workspaceError("Playback failed while restoring the project."));
                if (phaseWasRelink_ && !rollingBack_) {
                    beginRelinkRollback(error);
                } else {
                    fail(error);
                }
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
                state_.dirty = true;
                if (observed->opensComparison) {
                    currentProject_.reset();
                    currentProjectPath_.clear();
                    state_.sourceDiagnostics.clear();
                    updateProjectProjection();
                }
                publishSnapshot();
            }
            observedReviewCommands_.erase(observed);
        }
        reviewTerminals_.push_back(std::move(terminal));
    }

    void completeInternalPlaybackStep() {
        if (phase_ == Phase::ClosingFailedRelink) {
            finishFailedRelink();
            return;
        }
        if (phase_ == Phase::OpeningProject || phase_ == Phase::OpeningRelink ||
            phase_ == Phase::OpeningRelinkRollback) {
            const std::shared_ptr<const SessionSnapshot> playback =
                dependencies_.playbackSnapshot();
            if (!playback || playback->sessionState != domain::SessionState::kReady ||
                !playback->validatedComparison || !pendingProject_.has_value()) {
                const domain::MediaError error =
                    workspaceError("Project media opened without a validated ready snapshot.");
                if (phaseWasRelink_ && !rollingBack_) {
                    beginRelinkRollback(error);
                } else {
                    fail(error);
                }
                return;
            }
            auto refreshed = pendingProject_->replaceSources(*playback->validatedComparison);
            if (!refreshed) {
                if (phaseWasRelink_ && !rollingBack_) {
                    beginRelinkRollback(refreshed.error());
                } else {
                    fail(refreshed.error());
                }
                return;
            }
            pendingProject_ = std::move(refreshed).value();
            nextAnchorIndex_ = 0U;
            offsetsApplied_ = false;
            sequenceAlignmentApplied_ = false;
            beginOffsetsOrAnchors();
            return;
        }
        if (phase_ == Phase::ApplyingOffsets || phase_ == Phase::ApplyingSequenceAlignment) {
            beginOffsetsOrAnchors();
            return;
        }
        if (phase_ == Phase::ApplyingAnchors) {
            beginNextAnchorOrSeek();
            return;
        }
        if (phase_ == Phase::SeekingProjectFrame) {
            finishProjectRestore();
        }
    }

    void beginOffsetsOrAnchors() {
        if (!pendingProject_.has_value()) {
            fail(workspaceError("Project restore state disappeared."));
            return;
        }
        const auto& offsets = pendingProject_->alignmentState().offsets;
        if (!offsetsApplied_ && !offsets.empty()) {
            offsetsApplied_ = true;
            std::vector<SourceFrameOffset> applicationOffsets;
            applicationOffsets.reserve(offsets.size());
            for (const domain::PersistedAlignmentOffset& offset : offsets) {
                applicationOffsets.push_back(SourceFrameOffset{
                    .sourceId = offset.sourceId,
                    .frames = offset.frames,
                });
            }
            phase_ = Phase::ApplyingOffsets;
            submitInternal(PlaybackCommand{SetAlignmentOffsetsCommand{
                .context = nextCommandContext(),
                .sourceOffsets = std::move(applicationOffsets),
            }});
            return;
        }
        if (!sequenceAlignmentApplied_ &&
            pendingProject_->alignmentState().mode ==
                domain::ProjectAlignmentMode::kAutomaticSequence &&
            pendingDerivedAlignment_) {
            sequenceAlignmentApplied_ = true;
            phase_ = Phase::ApplyingSequenceAlignment;
            submitInternal(PlaybackCommand{RestoreSequenceAlignmentCommand{
                .context = nextCommandContext(),
                .sequenceResults = pendingDerivedAlignment_,
            }});
            return;
        }
        beginNextAnchorOrSeek();
    }

    void beginNextAnchorOrSeek() {
        if (!pendingProject_.has_value()) {
            fail(workspaceError("Project restore state disappeared."));
            return;
        }
        const auto& anchors = pendingProject_->alignmentState().anchors;
        if (nextAnchorIndex_ < anchors.size()) {
            const domain::PersistedAlignmentAnchor& anchor = anchors[nextAnchorIndex_++];
            phase_ = Phase::ApplyingAnchors;
            submitInternal(PlaybackCommand{SetManualAlignmentAnchorCommand{
                .context = nextCommandContext(),
                .sourceId = anchor.sourceId,
                .anchor =
                    ManualAlignmentAnchor{
                        .canonicalFrameId = anchor.canonicalFrame,
                        .sourceFrameId = anchor.sourceFrame,
                    },
            }});
            return;
        }
        if (pendingProject_->lastDisplayedFrame() != domain::FrameId{0}) {
            phase_ = Phase::SeekingProjectFrame;
            submitInternal(PlaybackCommand{SeekFrameCommand{
                .context = nextCommandContext(),
                .frameId = pendingProject_->lastDisplayedFrame(),
            }});
            return;
        }
        finishProjectRestore();
    }

    void finishProjectRestore() {
        if (rollingBack_) {
            currentProject_ = std::move(pendingProject_);
            state_.sourceDiagnostics = std::move(relinkOriginalDiagnostics_);
            phase_ = Phase::Idle;
            state_.busy = false;
            state_.dirty = relinkOriginalDirty_;
            state_.lastError = std::move(relinkFailure_);
            clearRelinkState();
            updateProjectProjection();
            publishSnapshot();
            return;
        }
        const bool relinked = phaseWasRelink_;
        currentProject_ = std::move(pendingProject_);
        if (!relinked) {
            currentProjectPath_ = pendingProjectPath_;
        }
        state_.sourceDiagnostics.clear();
        phase_ = Phase::Idle;
        state_.busy = false;
        state_.dirty = relinked || restoreDegraded_;
        clearRelinkState();
        restoreDegraded_ = false;
        pendingProject_.reset();
        pendingDerivedAlignment_.reset();
        pendingAlignmentCacheError_.reset();
        updateProjectProjection();
        publishSnapshot();
    }

    void beginRelinkRollback(domain::MediaError error) {
        relinkFailure_ = std::move(error);
        rollingBack_ = true;
        pendingProject_ = currentProject_;
        pendingDerivedAlignment_ = rollbackDerivedAlignment_;
        offsetsApplied_ = false;
        sequenceAlignmentApplied_ = false;
        nextAnchorIndex_ = 0U;
        pendingRelinkSource_.reset();
        pendingRelinkPath_.reset();
        if (relinkHadReadyPlayback_) {
            beginProjectOpen(Phase::OpeningRelinkRollback);
            return;
        }
        phase_ = Phase::ClosingFailedRelink;
        submitInternal(PlaybackCommand{CloseSessionCommand{
            .context = nextCommandContext(),
        }});
    }

    void finishFailedRelink() {
        phase_ = Phase::Idle;
        state_.busy = false;
        state_.dirty = relinkOriginalDirty_;
        state_.sourceDiagnostics = std::move(relinkOriginalDiagnostics_);
        state_.lastError = std::move(relinkFailure_);
        pendingProject_.reset();
        pendingDerivedAlignment_.reset();
        clearRelinkState();
        updateProjectProjection();
        publishSnapshot();
    }

    void clearRelinkState() {
        pendingRelinkSource_.reset();
        pendingRelinkPath_.reset();
        rollbackDerivedAlignment_.reset();
        relinkFailure_.reset();
        relinkOriginalDiagnostics_.clear();
        phaseWasRelink_ = false;
        rollingBack_ = false;
        relinkHadReadyPlayback_ = false;
        relinkOriginalDirty_ = false;
    }

    void completeProjectSave() {
        currentProject_ = std::move(pendingProject_);
        currentProjectPath_ = pendingProjectPath_;
        phase_ = Phase::Idle;
        state_.busy = false;
        state_.dirty = false;
        state_.sourceDiagnostics.clear();
        updateProjectProjection();
        publishSnapshot();
    }

    [[nodiscard]] static domain::ProjectAlignmentState alignmentStateFor(
        const SessionSnapshot& playback,
        const std::shared_ptr<const std::vector<SequenceAlignmentResult>>& sequenceAlignments) {
        domain::ProjectAlignmentState state;
        state.offsets.reserve(playback.alignmentOffsets.size());
        for (const SourceFrameOffset& offset : playback.alignmentOffsets) {
            state.offsets.push_back(domain::PersistedAlignmentOffset{
                .sourceId = offset.sourceId,
                .frames = offset.frames,
            });
        }
        for (const SourceAlignmentAnchors& source : playback.manualAlignmentAnchors) {
            for (const ManualAlignmentAnchor& anchor : source.anchors) {
                state.anchors.push_back(domain::PersistedAlignmentAnchor{
                    .sourceId = source.sourceId,
                    .canonicalFrame = anchor.canonicalFrameId,
                    .sourceFrame = anchor.sourceFrameId,
                });
            }
        }
        if (sequenceAlignments && playback.validatedComparison &&
            validateDerivedSequenceAlignments(*playback.validatedComparison, *sequenceAlignments)) {
            state.mode = domain::ProjectAlignmentMode::kAutomaticSequence;
            state.analysisCacheKey =
                makeDerivedAlignmentCacheKey(*playback.validatedComparison, *sequenceAlignments);
        } else if (!state.anchors.empty()) {
            state.mode = domain::ProjectAlignmentMode::kManualAnchors;
        } else if (!state.offsets.empty()) {
            state.mode = domain::ProjectAlignmentMode::kGlobalOffsets;
        }
        return state;
    }

    void updateProjectProjection() {
        state_.hasProject = currentProject_.has_value();
        state_.projectPath = currentProjectPath_;
        state_.displayName =
            currentProject_.has_value() ? currentProject_->displayName() : std::string{};
        state_.restoredViewState =
            currentProject_.has_value()
                ? std::optional<domain::ProjectViewState>{currentProject_->viewState()}
                : std::nullopt;
    }

    void setError(domain::MediaError error) {
        state_.lastError = std::move(error);
        publishSnapshot();
    }

    void fail(domain::MediaError error) {
        phase_ = Phase::Idle;
        pendingRepositoryContext_.reset();
        pendingPlaybackContext_.reset();
        pendingProject_.reset();
        pendingDiagnostics_.clear();
        pendingDerivedAlignment_.reset();
        pendingAlignmentCacheError_.reset();
        pendingRelinkSource_.reset();
        pendingRelinkPath_.reset();
        rollbackDerivedAlignment_.reset();
        relinkFailure_.reset();
        relinkOriginalDiagnostics_.clear();
        pendingRepositoryPayload_ = false;
        phaseWasRelink_ = false;
        rollingBack_ = false;
        relinkHadReadyPlayback_ = false;
        relinkOriginalDirty_ = false;
        restoreDegraded_ = false;
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
    std::shared_ptr<WorkspaceEventInbox> events_;
    Phase phase_ = Phase::Idle;
    WorkspaceSnapshot state_;
    std::shared_ptr<const WorkspaceSnapshot> publishedSnapshot_;
    std::optional<domain::Project> currentProject_;
    std::filesystem::path currentProjectPath_;
    std::optional<domain::Project> pendingProject_;
    std::filesystem::path pendingProjectPath_;
    SourceRevalidationDiagnostics pendingDiagnostics_;
    std::shared_ptr<const std::vector<SequenceAlignmentResult>> pendingDerivedAlignment_;
    std::optional<domain::MediaError> pendingAlignmentCacheError_;
    std::optional<domain::SourceId> pendingRelinkSource_;
    std::optional<std::filesystem::path> pendingRelinkPath_;
    std::shared_ptr<const std::vector<SequenceAlignmentResult>> rollbackDerivedAlignment_;
    std::optional<domain::MediaError> relinkFailure_;
    SourceRevalidationDiagnostics relinkOriginalDiagnostics_;
    std::optional<RequestContext> pendingRepositoryContext_;
    std::optional<CommandContext> pendingPlaybackContext_;
    std::vector<ObservedReviewCommand> observedReviewCommands_;
    std::vector<CommandTerminal> reviewTerminals_;
    std::size_t nextAnchorIndex_ = 0U;
    std::uint64_t nextRequestId_ = 1U;
    std::uint64_t nextCommandId_ = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t projectRevision_ = 0U;
    bool pendingRepositoryPayload_ = false;
    bool phaseWasRelink_ = false;
    bool rollingBack_ = false;
    bool relinkHadReadyPlayback_ = false;
    bool relinkOriginalDirty_ = false;
    bool restoreDegraded_ = false;
    bool offsetsApplied_ = false;
    bool sequenceAlignmentApplied_ = false;
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

PortSubmitResult WorkspaceCoordinator::openProject(const std::filesystem::path& projectPath) {
    return impl_->openProject(projectPath);
}

PortSubmitResult WorkspaceCoordinator::saveProject(const std::filesystem::path& projectPath,
                                                   std::string displayName,
                                                   const domain::ProjectViewState viewState) {
    return impl_->saveProject(projectPath, std::move(displayName), viewState);
}

PortSubmitResult WorkspaceCoordinator::relinkSource(const domain::SourceId sourceId,
                                                    const std::filesystem::path& sourcePath) {
    return impl_->relinkSource(sourceId, sourcePath);
}

std::shared_ptr<const WorkspaceSnapshot> WorkspaceCoordinator::snapshot() {
    return impl_->snapshot();
}

void WorkspaceCoordinator::markViewDirty() {
    impl_->markViewDirty();
}

void WorkspaceCoordinator::shutdown() noexcept {
    impl_->shutdown();
}

} // namespace dvs::application
