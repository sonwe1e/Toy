#include "dvs/application/AlignmentCacheIdentity.h"
#include "dvs/application/WorkspaceCoordinator.h"
#include "dvs/domain/ComparisonValidator.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace dvs::application {
namespace {

[[nodiscard]] domain::RationalRate makeRate() {
    auto rate = domain::RationalRate::create(30, 1);
    EXPECT_TRUE(rate);
    return std::move(rate).value();
}

[[nodiscard]] domain::ValidatedComparisonSet
makeSet(const std::filesystem::path& first = "C:/media/a.mp4",
        const std::filesystem::path& second = "C:/media/b.mp4",
        const std::int64_t frameCount = 10) {
    const domain::RationalRate rate = makeRate();
    const auto descriptor = [&rate, frameCount](const std::filesystem::path& path,
                                                const char fingerprint) {
        return domain::MediaDescriptor{
            .normalizedPath = path,
            .extent = domain::MediaExtent{.width = 1'920U, .height = 1'080U},
            .frameRate = rate,
            .frameCount =
                domain::FrameCountInfo{
                    .value = frameCount,
                    .origin = domain::FrameCountOrigin::kReported,
                },
            .duration = domain::MediaTime{frameCount * 33'333},
            .codecId = "h264",
            .pixelFormatId = "nv12",
            .bitDepth = 8U,
            .decodeCapabilities =
                domain::DecodeCapabilities{
                    .softwareDecode = true,
                    .d3d11VaDecode = true,
                },
            .timingConfidence = domain::TimingConfidence::kVerifiedCfr,
            .sourceIdentity =
                domain::SourceFileIdentity{
                    .byteSize = 1'024U,
                    .modifiedUtcMilliseconds = 1'234,
                    .fingerprintSha256 = std::string(64U, fingerprint),
                },
        };
    };
    auto validated = domain::ComparisonValidator::validate({
        domain::ComparisonSource{
            .id = 0U,
            .role = domain::ComparisonRole::kReference,
            .descriptor = descriptor(first, 'a'),
            .displayName = "A",
        },
        domain::ComparisonSource{
            .id = 1U,
            .role = domain::ComparisonRole::kPrediction,
            .descriptor = descriptor(second, 'b'),
            .displayName = "B",
        },
    });
    EXPECT_TRUE(validated);
    return std::move(validated).value().set;
}

[[nodiscard]] domain::Project makeProject(const bool withAlignment = true) {
    auto created = domain::Project::create(domain::ProjectId{"project-1"}, "Project", makeSet());
    EXPECT_TRUE(created);
    domain::Project project = std::move(created).value();
    EXPECT_TRUE(project.setLastDisplayedFrame(domain::FrameId{3}));
    EXPECT_TRUE(project.setViewState(domain::ProjectViewState{
        .layout = domain::ProjectViewLayout::kReferenceFocus,
        .differenceEdge = {0U, 1U},
        .differenceMetric = domain::ProjectDifferenceMetric::kHeatmap,
        .gain = 4U,
    }));
    if (withAlignment) {
        EXPECT_TRUE(project.setAlignmentState(domain::ProjectAlignmentState{
            .mode = domain::ProjectAlignmentMode::kManualAnchors,
            .offsets =
                {
                    domain::PersistedAlignmentOffset{.sourceId = 1U, .frames = 1},
                },
            .anchors =
                {
                    domain::PersistedAlignmentAnchor{
                        .sourceId = 1U,
                        .canonicalFrame = domain::FrameId{1},
                        .sourceFrame = domain::FrameId{2},
                    },
                },
        }));
    }
    return project;
}

[[nodiscard]] std::vector<SequenceAlignmentResult> makeSequenceResults() {
    SequenceAlignmentResult result{
        .sourceId = 1U,
        .segments =
            {
                SequenceAlignmentSegment{
                    .firstCanonicalFrame = domain::FrameId{0},
                    .lastCanonicalFrame = domain::FrameId{9},
                    .state = AlignmentSegmentState::Accepted,
                    .meanConfidence = 0.9F,
                    .p10Confidence = 0.8F,
                    .mappingSlope = 1.0F,
                },
            },
        .totalCost = 0.1F,
        .meanMatchCost = 0.1F,
        .confidence = 0.9F,
        .autoApplicable = true,
    };
    for (std::int64_t frame = 0; frame < 10; ++frame) {
        result.entries.push_back(SequenceAlignmentEntry{
            .canonicalFrameId = domain::FrameId{frame},
            .sourceFrameId = domain::FrameId{frame},
            .matchKind = FrameMatchKind::AutoAligned,
            .confidence = 0.9F,
        });
    }
    return {std::move(result)};
}

[[nodiscard]] domain::Project makeAutomaticProject() {
    domain::Project project = makeProject(false);
    const std::vector<SequenceAlignmentResult> results = makeSequenceResults();
    EXPECT_TRUE(project.setAlignmentState(domain::ProjectAlignmentState{
        .mode = domain::ProjectAlignmentMode::kAutomaticSequence,
        .offsets =
            {
                domain::PersistedAlignmentOffset{.sourceId = 1U, .frames = 1},
            },
        .anchors =
            {
                domain::PersistedAlignmentAnchor{
                    .sourceId = 1U,
                    .canonicalFrame = domain::FrameId{1},
                    .sourceFrame = domain::FrameId{2},
                },
            },
        .analysisCacheKey = makeDerivedAlignmentCacheKey(project.sources(), results),
    }));
    return project;
}

class FakeProjectRepository final : public IProjectRepository {
public:
    [[nodiscard]] PortSubmitResult submit(const ProjectLoadRequest& request,
                                          std::shared_ptr<IApplicationEventSink> events) override {
        loadRequest = request;
        sink = std::move(events);
        return submitResult;
    }

    [[nodiscard]] PortSubmitResult submit(const ProjectRelinkRequest& request,
                                          std::shared_ptr<IApplicationEventSink> events) override {
        relinkRequest = request;
        sink = std::move(events);
        return submitResult;
    }

    [[nodiscard]] PortSubmitResult submit(const ProjectSaveRequest& request,
                                          std::shared_ptr<IApplicationEventSink> events) override {
        saveRequest = request;
        sink = std::move(events);
        return submitResult;
    }

    void cancel(const RequestContext& context) noexcept override {
        canceled.push_back(context);
    }

    void post(ApplicationEvent event) {
        ASSERT_NE(sink, nullptr);
        EXPECT_EQ(sink->postCritical(std::move(event)), EventPostResult::Accepted);
    }

    PortSubmitResult submitResult = PortSubmitResult::Accepted;
    std::optional<ProjectLoadRequest> loadRequest;
    std::optional<ProjectRelinkRequest> relinkRequest;
    std::optional<ProjectSaveRequest> saveRequest;
    std::shared_ptr<IApplicationEventSink> sink;
    std::vector<RequestContext> canceled;
};

class FakePlayback final {
public:
    FakePlayback() {
        current = std::make_shared<SessionSnapshot>();
        current->sessionId = domain::SessionId{1U};
        current->sessionEpoch = domain::SessionEpoch{1U};
        current->graphicsReady = true;
    }

    [[nodiscard]] PortSubmitResult submit(PlaybackCommand command) {
        commands.push_back(std::move(command));
        return submitResult;
    }

    [[nodiscard]] std::shared_ptr<const SessionSnapshot> snapshot() const {
        return current;
    }

    [[nodiscard]] std::vector<CommandTerminal> takeTerminals() {
        std::vector<CommandTerminal> result = std::move(terminals);
        terminals.clear();
        return result;
    }

    void makeReady(domain::ValidatedComparisonSet set,
                   const domain::FrameId displayed = domain::FrameId{0}) {
        const std::uint64_t canonicalFrameCount =
            static_cast<std::uint64_t>(set.canonicalFrameCount());
        auto next = std::make_shared<SessionSnapshot>();
        next->sessionId = domain::SessionId{1U};
        next->sessionEpoch = domain::SessionEpoch{2U};
        next->playbackGeneration = domain::PlaybackGeneration{1U};
        next->graphicsReady = true;
        next->sessionState = domain::SessionState::kReady;
        next->playbackState = domain::PlaybackState::kPaused;
        next->displayedFrame = displayed;
        next->canonicalFrameCount = canonicalFrameCount;
        next->sources = {
            SessionSourceView{
                .sourceId = 0U,
                .role = domain::ComparisonRole::kReference,
                .displayName = "A",
            },
            SessionSourceView{
                .sourceId = 1U,
                .role = domain::ComparisonRole::kPrediction,
                .displayName = "B",
            },
        };
        next->validatedComparison =
            std::make_shared<const domain::ValidatedComparisonSet>(std::move(set));
        current = std::move(next);
    }

    void succeedLastCommand() {
        ASSERT_FALSE(commands.empty());
        terminals.push_back(CommandTerminal{
            .context = commandContext(commands.back()),
            .outcome = CommandOutcome::Succeeded,
        });
    }

    std::shared_ptr<SessionSnapshot> current;
    std::shared_ptr<const std::vector<SequenceAlignmentResult>> acceptedAlignments;
    PortSubmitResult submitResult = PortSubmitResult::Accepted;
    std::vector<PlaybackCommand> commands;
    std::vector<CommandTerminal> terminals;
};

[[nodiscard]] std::unique_ptr<WorkspaceCoordinator>
makeCoordinator(const std::shared_ptr<FakeProjectRepository>& repository,
                const std::shared_ptr<FakePlayback>& playback) {
    return WorkspaceCoordinator::create(WorkspaceCoordinator::Dependencies{
        .projectRepository = repository,
        .submitPlayback =
            [playback](PlaybackCommand command) { return playback->submit(std::move(command)); },
        .playbackSnapshot = [playback] { return playback->snapshot(); },
        .takePlaybackTerminals = [playback] { return playback->takeTerminals(); },
        .acceptedSequenceAlignments = [playback] { return playback->acceptedAlignments; },
        .createProjectId = [] { return domain::ProjectId{"generated-project"}; },
    });
}

void postSucceeded(FakeProjectRepository& repository, const RequestContext& context) {
    repository.post(ApplicationEvent{RequestTerminal{RequestSucceeded{
        .context = EventContext{context},
    }}});
}

TEST(WorkspaceCoordinatorTests, LoadsAndRestoresOffsetsAnchorsAndLastFrameInOrder) {
    auto repository = std::make_shared<FakeProjectRepository>();
    auto playback = std::make_shared<FakePlayback>();
    auto workspace = makeCoordinator(repository, playback);
    ASSERT_NE(workspace, nullptr);

    const std::filesystem::path projectPath = "C:/projects/review.dvsproj";
    ASSERT_EQ(workspace->openProject(projectPath), PortSubmitResult::Accepted);
    ASSERT_TRUE(repository->loadRequest.has_value());
    repository->post(ApplicationEvent{ProjectLoaded{
        .context = repository->loadRequest->context,
        .project = makeProject(),
        .sourceDiagnostics = {},
    }});
    postSucceeded(*repository, repository->loadRequest->context);

    EXPECT_TRUE(workspace->snapshot()->busy);
    ASSERT_EQ(playback->commands.size(), 1U);
    EXPECT_NE(std::get_if<OpenComparisonCommand>(&playback->commands[0]), nullptr);

    playback->makeReady(makeSet());
    playback->succeedLastCommand();
    static_cast<void>(workspace->snapshot());
    ASSERT_EQ(playback->commands.size(), 2U);
    const auto* offsets = std::get_if<SetAlignmentOffsetsCommand>(&playback->commands[1]);
    ASSERT_NE(offsets, nullptr);
    EXPECT_EQ(offsets->sourceOffsets,
              std::vector<SourceFrameOffset>({
                  SourceFrameOffset{.sourceId = 1U, .frames = 1},
              }));

    playback->succeedLastCommand();
    static_cast<void>(workspace->snapshot());
    ASSERT_EQ(playback->commands.size(), 3U);
    const auto* anchor = std::get_if<SetManualAlignmentAnchorCommand>(&playback->commands[2]);
    ASSERT_NE(anchor, nullptr);
    EXPECT_EQ(anchor->sourceId, 1U);
    EXPECT_EQ(anchor->anchor.canonicalFrameId, domain::FrameId{1});
    EXPECT_EQ(anchor->anchor.sourceFrameId, domain::FrameId{2});

    playback->succeedLastCommand();
    static_cast<void>(workspace->snapshot());
    ASSERT_EQ(playback->commands.size(), 4U);
    const auto* seek = std::get_if<SeekFrameCommand>(&playback->commands[3]);
    ASSERT_NE(seek, nullptr);
    EXPECT_EQ(seek->frameId, domain::FrameId{3});

    playback->makeReady(makeSet(), domain::FrameId{3});
    playback->succeedLastCommand();
    const auto restored = workspace->snapshot();
    EXPECT_FALSE(restored->busy);
    EXPECT_FALSE(restored->dirty);
    EXPECT_TRUE(restored->hasProject);
    EXPECT_EQ(restored->projectPath, projectPath);
    ASSERT_TRUE(restored->restoredViewState.has_value());
    EXPECT_EQ(restored->restoredViewState->layout, domain::ProjectViewLayout::kReferenceFocus);
    EXPECT_TRUE(workspace->takeCompletedPlaybackCommands().empty());
}

TEST(WorkspaceCoordinatorTests, KeepsInvalidProjectAvailableForExplicitRelinkThenReopensFresh) {
    auto repository = std::make_shared<FakeProjectRepository>();
    auto playback = std::make_shared<FakePlayback>();
    auto workspace = makeCoordinator(repository, playback);
    ASSERT_NE(workspace, nullptr);

    const std::filesystem::path projectPath = "C:/projects/relink.dvsproj";
    ASSERT_EQ(workspace->openProject(projectPath), PortSubmitResult::Accepted);
    const domain::MediaError missing =
        domain::makeMediaError(domain::MediaErrorCode::kSourceMissing,
                               domain::MediaOperation::kProjectPersistence,
                               domain::SourceId{1U},
                               true);
    repository->post(ApplicationEvent{ProjectLoaded{
        .context = repository->loadRequest->context,
        .project = makeProject(false),
        .sourceDiagnostics =
            {
                SourceRevalidationDiagnostic{
                    .sourceId = 1U,
                    .error = missing,
                },
            },
    }});
    postSucceeded(*repository, repository->loadRequest->context);
    const auto relinkRequired = workspace->snapshot();
    EXPECT_TRUE(relinkRequired->hasProject);
    EXPECT_FALSE(relinkRequired->busy);
    ASSERT_EQ(relinkRequired->sourceDiagnostics.size(), 1U);
    EXPECT_TRUE(playback->commands.empty());

    const std::filesystem::path replacement = "C:/media/replacement.mp4";
    ASSERT_EQ(workspace->relinkSource(1U, replacement), PortSubmitResult::Accepted);
    ASSERT_TRUE(repository->relinkRequest.has_value());
    auto candidate =
        domain::SourceRelinkCandidate::create(1U,
                                              replacement,
                                              domain::SourceFileIdentity{
                                                  .byteSize = 2'048U,
                                                  .modifiedUtcMilliseconds = 2'468,
                                                  .fingerprintSha256 = std::string(64U, 'c'),
                                              });
    ASSERT_TRUE(candidate);
    repository->post(ApplicationEvent{SourceRelinkPrepared{
        .context = repository->relinkRequest->context,
        .candidate = std::move(candidate).value(),
    }});
    postSucceeded(*repository, repository->relinkRequest->context);
    static_cast<void>(workspace->snapshot());
    ASSERT_EQ(playback->commands.size(), 1U);
    const auto* open = std::get_if<OpenComparisonCommand>(&playback->commands.back());
    ASSERT_NE(open, nullptr);
    EXPECT_EQ(open->sources[1].path, replacement);

    playback->makeReady(makeSet("C:/media/a.mp4", replacement));
    playback->succeedLastCommand();
    static_cast<void>(workspace->snapshot());
    ASSERT_EQ(playback->commands.size(), 2U);
    const auto* seek = std::get_if<SeekFrameCommand>(&playback->commands.back());
    ASSERT_NE(seek, nullptr);
    EXPECT_EQ(seek->frameId, domain::FrameId{3});

    playback->makeReady(makeSet("C:/media/a.mp4", replacement), domain::FrameId{3});
    playback->succeedLastCommand();
    const auto restored = workspace->snapshot();
    EXPECT_FALSE(restored->busy);
    EXPECT_TRUE(restored->hasProject);
    EXPECT_TRUE(restored->dirty);
    EXPECT_TRUE(restored->sourceDiagnostics.empty());
}

TEST(WorkspaceCoordinatorTests, FailedRelinkRestoresTheOriginalReadyPlaybackAndProjectState) {
    auto repository = std::make_shared<FakeProjectRepository>();
    auto playback = std::make_shared<FakePlayback>();
    playback->makeReady(makeSet(), domain::FrameId{3});
    auto workspace = makeCoordinator(repository, playback);
    ASSERT_NE(workspace, nullptr);

    const std::filesystem::path projectPath = "C:/projects/rollback.dvsproj";
    ASSERT_EQ(workspace->saveProject(projectPath, "Rollback", domain::ProjectViewState{}),
              PortSubmitResult::Accepted);
    repository->post(ApplicationEvent{ProjectSaved{
        .context = repository->saveRequest->context,
    }});
    postSucceeded(*repository, repository->saveRequest->context.request);
    ASSERT_FALSE(workspace->snapshot()->dirty);

    const std::filesystem::path replacement = "C:/media/short-replacement.mp4";
    ASSERT_EQ(workspace->relinkSource(1U, replacement), PortSubmitResult::Accepted);
    auto candidate =
        domain::SourceRelinkCandidate::create(1U,
                                              replacement,
                                              domain::SourceFileIdentity{
                                                  .byteSize = 2'048U,
                                                  .modifiedUtcMilliseconds = 2'468,
                                                  .fingerprintSha256 = std::string(64U, 'c'),
                                              });
    ASSERT_TRUE(candidate);
    repository->post(ApplicationEvent{SourceRelinkPrepared{
        .context = repository->relinkRequest->context,
        .candidate = std::move(candidate).value(),
    }});
    postSucceeded(*repository, repository->relinkRequest->context);
    static_cast<void>(workspace->snapshot());
    ASSERT_EQ(playback->commands.size(), 1U);

    playback->makeReady(makeSet("C:/media/a.mp4", replacement, 2));
    playback->succeedLastCommand();
    static_cast<void>(workspace->snapshot());
    ASSERT_EQ(playback->commands.size(), 2U);
    const auto* rollbackOpen = std::get_if<OpenComparisonCommand>(&playback->commands.back());
    ASSERT_NE(rollbackOpen, nullptr);
    EXPECT_EQ(rollbackOpen->sources[1].path, std::filesystem::path{"C:/media/b.mp4"});

    playback->makeReady(makeSet());
    playback->succeedLastCommand();
    static_cast<void>(workspace->snapshot());
    ASSERT_EQ(playback->commands.size(), 3U);
    const auto* restoreSeek = std::get_if<SeekFrameCommand>(&playback->commands.back());
    ASSERT_NE(restoreSeek, nullptr);
    EXPECT_EQ(restoreSeek->frameId, domain::FrameId{3});

    playback->makeReady(makeSet(), domain::FrameId{3});
    playback->succeedLastCommand();
    const auto restored = workspace->snapshot();
    EXPECT_FALSE(restored->busy);
    EXPECT_FALSE(restored->dirty);
    EXPECT_TRUE(restored->hasProject);
    EXPECT_EQ(restored->projectPath, projectPath);
    ASSERT_TRUE(restored->lastError.has_value());
    EXPECT_NE(restored->lastError->technicalDetail.find("canonical source timeline"),
              std::string::npos);
}

TEST(WorkspaceCoordinatorTests, SavesFreshPlaybackStateAndRoutesReviewTerminalsSeparately) {
    auto repository = std::make_shared<FakeProjectRepository>();
    auto playback = std::make_shared<FakePlayback>();
    playback->makeReady(makeSet(), domain::FrameId{4});
    playback->current->alignmentOffsets = {
        SourceFrameOffset{.sourceId = 1U, .frames = 1},
    };
    playback->current->manualAlignmentAnchors = {
        SourceAlignmentAnchors{
            .sourceId = 1U,
            .anchors =
                {
                    ManualAlignmentAnchor{
                        .canonicalFrameId = domain::FrameId{1},
                        .sourceFrameId = domain::FrameId{2},
                    },
                },
        },
    };
    auto workspace = makeCoordinator(repository, playback);
    ASSERT_NE(workspace, nullptr);

    const std::filesystem::path projectPath = "C:/projects/saved.dvsproj";
    const domain::ProjectViewState view{
        .layout = domain::ProjectViewLayout::kDifference,
        .differenceEdge = {0U, 1U},
        .differenceMetric = domain::ProjectDifferenceMetric::kLuma,
        .gain = 8U,
    };
    ASSERT_EQ(workspace->saveProject(projectPath, "Saved", view), PortSubmitResult::Accepted);
    ASSERT_TRUE(repository->saveRequest.has_value());
    EXPECT_EQ(repository->saveRequest->project.lastDisplayedFrame(), domain::FrameId{4});
    EXPECT_EQ(repository->saveRequest->project.alignmentState().mode,
              domain::ProjectAlignmentMode::kManualAnchors);
    EXPECT_EQ(repository->saveRequest->project.alignmentState().offsets.size(), 1U);
    EXPECT_EQ(repository->saveRequest->project.alignmentState().anchors.size(), 1U);
    EXPECT_EQ(repository->saveRequest->project.viewState(), view);

    repository->post(ApplicationEvent{ProjectSaved{
        .context = repository->saveRequest->context,
    }});
    postSucceeded(*repository, repository->saveRequest->context.request);
    const auto saved = workspace->snapshot();
    EXPECT_FALSE(saved->busy);
    EXPECT_FALSE(saved->dirty);
    EXPECT_TRUE(saved->hasProject);
    EXPECT_EQ(saved->projectPath, projectPath);

    const PlaybackCommand reviewCommand{SeekFrameCommand{
        .context =
            CommandContext{
                .sessionId = domain::SessionId{1U},
                .sessionEpoch = domain::SessionEpoch{2U},
                .commandId = domain::CommandId{7U},
            },
        .frameId = domain::FrameId{5},
    }};
    ASSERT_EQ(workspace->submitPlayback(reviewCommand), PortSubmitResult::Accepted);
    playback->succeedLastCommand();
    const std::vector<CommandTerminal> reviewTerminals = workspace->takeCompletedPlaybackCommands();
    ASSERT_EQ(reviewTerminals.size(), 1U);
    EXPECT_EQ(reviewTerminals.front().context.commandId, domain::CommandId{7U});
    EXPECT_TRUE(workspace->snapshot()->dirty);
}

TEST(WorkspaceCoordinatorTests, RestoresDerivedAlignmentBetweenOffsetsAndAnchors) {
    auto repository = std::make_shared<FakeProjectRepository>();
    auto playback = std::make_shared<FakePlayback>();
    auto workspace = makeCoordinator(repository, playback);
    ASSERT_NE(workspace, nullptr);
    domain::Project project = makeAutomaticProject();
    const auto cached =
        std::make_shared<const std::vector<SequenceAlignmentResult>>(makeSequenceResults());

    ASSERT_EQ(workspace->openProject("C:/projects/automatic.dvsproj"), PortSubmitResult::Accepted);
    repository->post(ApplicationEvent{ProjectLoaded{
        .context = repository->loadRequest->context,
        .project = project,
        .sourceDiagnostics = {},
        .derivedAlignmentResults = cached,
    }});
    postSucceeded(*repository, repository->loadRequest->context);
    static_cast<void>(workspace->snapshot());
    playback->makeReady(makeSet());
    playback->succeedLastCommand();
    static_cast<void>(workspace->snapshot());
    ASSERT_EQ(playback->commands.size(), 2U);
    EXPECT_NE(std::get_if<SetAlignmentOffsetsCommand>(&playback->commands.back()), nullptr);

    playback->succeedLastCommand();
    static_cast<void>(workspace->snapshot());
    ASSERT_EQ(playback->commands.size(), 3U);
    const auto* restore = std::get_if<RestoreSequenceAlignmentCommand>(&playback->commands.back());
    ASSERT_NE(restore, nullptr);
    EXPECT_EQ(restore->sequenceResults, cached);

    playback->succeedLastCommand();
    static_cast<void>(workspace->snapshot());
    ASSERT_EQ(playback->commands.size(), 4U);
    EXPECT_NE(std::get_if<SetManualAlignmentAnchorCommand>(&playback->commands.back()), nullptr);
}

TEST(WorkspaceCoordinatorTests, MissingDerivedCacheFallsBackToDirtyStrictIndex) {
    auto repository = std::make_shared<FakeProjectRepository>();
    auto playback = std::make_shared<FakePlayback>();
    auto workspace = makeCoordinator(repository, playback);
    ASSERT_NE(workspace, nullptr);

    ASSERT_EQ(workspace->openProject("C:/projects/degraded.dvsproj"), PortSubmitResult::Accepted);
    repository->post(ApplicationEvent{ProjectLoaded{
        .context = repository->loadRequest->context,
        .project = makeAutomaticProject(),
        .sourceDiagnostics = {},
        .alignmentCacheError = domain::makeMediaError(domain::MediaErrorCode::kInvalidProjectSchema,
                                                      domain::MediaOperation::kProjectPersistence,
                                                      std::nullopt,
                                                      true,
                                                      "Cache invalidated."),
    }});
    postSucceeded(*repository, repository->loadRequest->context);
    static_cast<void>(workspace->snapshot());
    playback->makeReady(makeSet());
    playback->succeedLastCommand();
    static_cast<void>(workspace->snapshot());
    ASSERT_EQ(playback->commands.size(), 2U);
    EXPECT_NE(std::get_if<SeekFrameCommand>(&playback->commands.back()), nullptr);
    playback->makeReady(makeSet(), domain::FrameId{3});
    playback->succeedLastCommand();

    const auto restored = workspace->snapshot();
    EXPECT_FALSE(restored->busy);
    EXPECT_TRUE(restored->dirty);
    ASSERT_TRUE(restored->lastError.has_value());
    EXPECT_EQ(restored->lastError->technicalDetail, "Cache invalidated.");
}

TEST(WorkspaceCoordinatorTests, SavesAcceptedSequenceMapAsDerivedCacheReference) {
    auto repository = std::make_shared<FakeProjectRepository>();
    auto playback = std::make_shared<FakePlayback>();
    playback->makeReady(makeSet(), domain::FrameId{4});
    playback->acceptedAlignments =
        std::make_shared<const std::vector<SequenceAlignmentResult>>(makeSequenceResults());
    auto workspace = makeCoordinator(repository, playback);
    ASSERT_NE(workspace, nullptr);

    ASSERT_EQ(workspace->saveProject(
                  "C:/projects/automatic-save.dvsproj", "Automatic", domain::ProjectViewState{}),
              PortSubmitResult::Accepted);
    ASSERT_TRUE(repository->saveRequest.has_value());
    EXPECT_EQ(repository->saveRequest->project.alignmentState().mode,
              domain::ProjectAlignmentMode::kAutomaticSequence);
    ASSERT_TRUE(repository->saveRequest->project.alignmentState().analysisCacheKey.has_value());
    EXPECT_EQ(repository->saveRequest->derivedAlignmentResults, playback->acceptedAlignments);
}

} // namespace
} // namespace dvs::application
