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

[[nodiscard]] domain::ValidatedComparisonSet makeSingleSet() {
    const domain::ValidatedComparisonSet pair = makeSet();
    auto validated =
        domain::ComparisonValidator::validate({domain::ComparisonSource{pair.sources().front()}});
    EXPECT_TRUE(validated);
    return std::move(validated).value().set;
}

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
        next->sources.reserve(set.sourceCount());
        for (const domain::ComparisonSource& source : set.sources()) {
            next->sources.push_back(SessionSourceView{
                .sourceId = source.id,
                .role = source.role,
                .displayName = source.displayName,
            });
        }
        next->validatedComparison =
            std::make_shared<const domain::ValidatedComparisonSet>(std::move(set));
        current = std::move(next);
    }

    void makeEmpty() {
        auto next = std::make_shared<SessionSnapshot>();
        next->sessionId = domain::SessionId{1U};
        next->sessionEpoch = domain::SessionEpoch{3U};
        next->sessionState = domain::SessionState::kEmpty;
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
makeCoordinator(const std::shared_ptr<FakePlayback>& playback) {
    return WorkspaceCoordinator::create(WorkspaceCoordinator::Dependencies{
        .submitPlayback =
            [playback](PlaybackCommand command) { return playback->submit(std::move(command)); },
        .playbackSnapshot = [playback] { return playback->snapshot(); },
        .takePlaybackTerminals = [playback] { return playback->takeTerminals(); },
        .acceptedSequenceAlignments = [playback] { return playback->acceptedAlignments; },
    });
}

[[nodiscard]] CommandContext commandContextFor(const std::uint64_t commandId) {
    return CommandContext{
        .sessionId = domain::SessionId{1U},
        .sessionEpoch = domain::SessionEpoch{2U},
        .commandId = domain::CommandId{commandId},
    };
}

TEST(WorkspaceCoordinatorTests, SnapshotStartsQuiescentAndCloseEmptyReviewIsImmediate) {
    auto playback = std::make_shared<FakePlayback>();
    auto workspace = makeCoordinator(playback);
    ASSERT_NE(workspace, nullptr);

    const auto initial = workspace->snapshot();
    EXPECT_FALSE(initial->busy);
    EXPECT_TRUE(initial->displayName.empty());
    EXPECT_TRUE(initial->sourceDiagnostics.empty());
    EXPECT_FALSE(initial->lastError.has_value());

    playback->makeEmpty();
    ASSERT_EQ(workspace->closeReview(), PortSubmitResult::Accepted);
    EXPECT_TRUE(playback->commands.empty());
    const auto closed = workspace->snapshot();
    EXPECT_FALSE(closed->busy);
    EXPECT_TRUE(closed->displayName.empty());
}

TEST(WorkspaceCoordinatorTests, ForwardsPlaybackCommandsAndRoutesTerminalsSeparately) {
    auto playback = std::make_shared<FakePlayback>();
    playback->makeReady(makeSet());
    auto workspace = makeCoordinator(playback);
    ASSERT_NE(workspace, nullptr);

    const PlaybackCommand reviewCommand{SeekFrameCommand{
        .context = commandContextFor(7U),
        .frameId = domain::FrameId{5},
    }};
    ASSERT_EQ(workspace->submitPlayback(reviewCommand), PortSubmitResult::Accepted);
    ASSERT_EQ(playback->commands.size(), 1U);
    EXPECT_NE(std::get_if<SeekFrameCommand>(&playback->commands.back()), nullptr);

    playback->succeedLastCommand();
    const std::vector<CommandTerminal> reviewTerminals = workspace->takeCompletedPlaybackCommands();
    ASSERT_EQ(reviewTerminals.size(), 1U);
    EXPECT_EQ(reviewTerminals.front().context.commandId, domain::CommandId{7U});
    EXPECT_EQ(reviewTerminals.front().outcome, CommandOutcome::Succeeded);
    EXPECT_TRUE(workspace->takeCompletedPlaybackCommands().empty());
}

TEST(WorkspaceCoordinatorTests, OpensComparisonAndProjectsSourceDisplayName) {
    auto playback = std::make_shared<FakePlayback>();
    auto workspace = makeCoordinator(playback);
    ASSERT_NE(workspace, nullptr);

    ASSERT_EQ(workspace->submitPlayback(PlaybackCommand{OpenComparisonCommand{
                  .context = commandContextFor(41U),
                  .sources =
                      {
                          OpenComparisonSource{
                              .path = "C:/media/a.mp4",
                              .role = domain::ComparisonRole::kReference,
                              .displayName = "A",
                          },
                          OpenComparisonSource{
                              .path = "C:/media/b.mp4",
                              .role = domain::ComparisonRole::kPrediction,
                              .displayName = "B",
                          },
                      },
                  .intent = OpenReviewIntent::NewReview,
              }}),
              PortSubmitResult::Accepted);
    playback->makeReady(makeSet());
    playback->succeedLastCommand();
    static_cast<void>(workspace->snapshot());
    EXPECT_EQ(workspace->snapshot()->displayName, std::string{"A \xc2\xb7 B"});
    EXPECT_FALSE(workspace->snapshot()->busy);
}

TEST(WorkspaceCoordinatorTests, NewReviewReplacesPriorDisplayName) {
    auto playback = std::make_shared<FakePlayback>();
    playback->makeReady(makeSet());
    auto workspace = makeCoordinator(playback);
    ASSERT_NE(workspace, nullptr);

    ASSERT_EQ(workspace->submitPlayback(PlaybackCommand{OpenComparisonCommand{
                  .context = commandContextFor(41U),
                  .sources =
                      {
                          OpenComparisonSource{
                              .path = "C:/media/a.mp4",
                              .role = domain::ComparisonRole::kReference,
                              .displayName = "A",
                          },
                          OpenComparisonSource{
                              .path = "C:/media/b.mp4",
                              .role = domain::ComparisonRole::kPrediction,
                              .displayName = "B",
                          },
                      },
                  .intent = OpenReviewIntent::NewReview,
              }}),
              PortSubmitResult::Accepted);
    playback->makeReady(makeSet());
    playback->succeedLastCommand();
    EXPECT_EQ(workspace->snapshot()->displayName, std::string{"A \xc2\xb7 B"});

    ASSERT_EQ(workspace->submitPlayback(PlaybackCommand{OpenComparisonCommand{
                  .context = commandContextFor(42U),
                  .sources =
                      {
                          OpenComparisonSource{
                              .path = "C:/media/c.mp4",
                              .role = domain::ComparisonRole::kReference,
                              .displayName = "C",
                          },
                      },
                  .intent = OpenReviewIntent::NewReview,
              }}),
              PortSubmitResult::Accepted);
    playback->makeReady(makeSingleSet());
    playback->succeedLastCommand();
    EXPECT_EQ(workspace->snapshot()->displayName, std::string{"C"});
}

TEST(WorkspaceCoordinatorTests, ClosesReadyReviewAndResetsWorkspaceProjection) {
    auto playback = std::make_shared<FakePlayback>();
    playback->makeReady(makeSet());
    auto workspace = makeCoordinator(playback);
    ASSERT_NE(workspace, nullptr);

    ASSERT_EQ(workspace->submitPlayback(PlaybackCommand{OpenComparisonCommand{
                  .context = commandContextFor(41U),
                  .sources =
                      {
                          OpenComparisonSource{
                              .path = "C:/media/a.mp4",
                              .role = domain::ComparisonRole::kReference,
                              .displayName = "A",
                          },
                          OpenComparisonSource{
                              .path = "C:/media/b.mp4",
                              .role = domain::ComparisonRole::kPrediction,
                              .displayName = "B",
                          },
                      },
                  .intent = OpenReviewIntent::NewReview,
              }}),
              PortSubmitResult::Accepted);
    playback->makeReady(makeSet());
    playback->succeedLastCommand();
    ASSERT_FALSE(workspace->snapshot()->displayName.empty());

    ASSERT_EQ(workspace->closeReview(), PortSubmitResult::Accepted);
    ASSERT_TRUE(std::holds_alternative<CloseSessionCommand>(playback->commands.back()));
    playback->makeEmpty();
    playback->succeedLastCommand();
    const auto closed = workspace->snapshot();
    ASSERT_NE(closed, nullptr);
    EXPECT_FALSE(closed->busy);
    EXPECT_TRUE(closed->displayName.empty());
    EXPECT_TRUE(closed->sourceDiagnostics.empty());
    EXPECT_FALSE(closed->lastError.has_value());
}

TEST(WorkspaceCoordinatorTests, RejectsReviewCommandsAndClosesWhileBusy) {
    auto playback = std::make_shared<FakePlayback>();
    playback->makeReady(makeSet());
    auto workspace = makeCoordinator(playback);
    ASSERT_NE(workspace, nullptr);

    ASSERT_EQ(workspace->closeReview(), PortSubmitResult::Accepted);
    EXPECT_TRUE(workspace->snapshot()->busy);

    // The coordinator is busy closing the review; another close is rejected.
    EXPECT_EQ(workspace->closeReview(), PortSubmitResult::Busy);

    playback->makeEmpty();
    playback->succeedLastCommand();
    EXPECT_FALSE(workspace->snapshot()->busy);
}

} // namespace
} // namespace dvs::application
