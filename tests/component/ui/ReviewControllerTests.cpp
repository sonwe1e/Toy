#include "dvs/ui/ReviewController.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace dvs::ui {
namespace {

using namespace std::chrono_literals;

void ensureCoreApplication() {
    if (QCoreApplication::instance() != nullptr) {
        return;
    }
    static int argumentCount = 1;
    static char applicationName[] = "ReviewControllerTests";
    static char* arguments[] = {applicationName, nullptr};
    static QCoreApplication application{argumentCount, arguments};
    static_cast<void>(application);
}

template <typename Predicate>
[[nodiscard]] bool waitUntil(Predicate predicate, const std::chrono::milliseconds timeout = 1s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        QThread::msleep(1U);
    }
    return predicate();
}

[[nodiscard]] application::SessionSnapshot emptySnapshot(const bool graphicsReady = true,
                                                         const std::uint64_t sessionId = 17U,
                                                         const std::uint64_t sessionEpoch = 3U) {
    application::SessionSnapshot snapshot;
    snapshot.sessionId = domain::SessionId{sessionId};
    snapshot.sessionEpoch = domain::SessionEpoch{sessionEpoch};
    snapshot.deviceGeneration = domain::DeviceGeneration{2U};
    snapshot.graphicsReady = graphicsReady;
    return snapshot;
}

[[nodiscard]] application::SessionSnapshot readySnapshot(const std::int64_t frame,
                                                         const std::uint64_t frameCount,
                                                         const std::uint64_t sessionEpoch = 3U) {
    application::SessionSnapshot snapshot = emptySnapshot(true, 17U, sessionEpoch);
    snapshot.playbackGeneration = domain::PlaybackGeneration{5U};
    snapshot.sessionState = domain::SessionState::kReady;
    snapshot.playbackState = domain::PlaybackState::kPaused;
    snapshot.displayedFrame = domain::FrameId{frame};
    snapshot.canonicalFrameCount = frameCount;
    return snapshot;
}

class FakeBackend final {
public:
    application::SessionSnapshot currentSnapshot = emptySnapshot();
    application::PortSubmitResult submitResult = application::PortSubmitResult::Accepted;
    std::vector<application::PlaybackCommand> submitted;
    std::vector<application::CommandTerminal> terminals;
    std::size_t submitCalls = 0U;
    std::size_t snapshotCalls = 0U;
    std::size_t drainCalls = 0U;
    std::thread::id lastAccessThread;
};

[[nodiscard]] ReviewController::Dependencies
dependenciesFor(const std::weak_ptr<FakeBackend>& weakBackend) {
    return ReviewController::Dependencies{
        .submit =
            [weakBackend](application::PlaybackCommand command) {
                const std::shared_ptr<FakeBackend> backend = weakBackend.lock();
                if (!backend) {
                    return application::PortSubmitResult::Closed;
                }
                ++backend->submitCalls;
                backend->lastAccessThread = std::this_thread::get_id();
                backend->submitted.push_back(std::move(command));
                return backend->submitResult;
            },
        .snapshot = [weakBackend]() -> std::shared_ptr<const application::SessionSnapshot> {
            const std::shared_ptr<FakeBackend> backend = weakBackend.lock();
            if (!backend) {
                return {};
            }
            ++backend->snapshotCalls;
            backend->lastAccessThread = std::this_thread::get_id();
            return std::make_shared<const application::SessionSnapshot>(backend->currentSnapshot);
        },
        .takeCompletedCommands =
            [weakBackend] {
                const std::shared_ptr<FakeBackend> backend = weakBackend.lock();
                if (!backend) {
                    return std::vector<application::CommandTerminal>{};
                }
                ++backend->drainCalls;
                backend->lastAccessThread = std::this_thread::get_id();
                std::vector<application::CommandTerminal> result = std::move(backend->terminals);
                backend->terminals.clear();
                return result;
            },
    };
}

[[nodiscard]] QString createFile(QTemporaryDir& directory, const QString& filename) {
    const QString path = directory.filePath(filename);
    QFile file{path};
    if (!file.open(QIODevice::WriteOnly) || file.write("fixture") != 7) {
        return {};
    }
    file.close();
    return path;
}

void completeLastCommand(
    const std::shared_ptr<FakeBackend>& backend,
    const application::CommandOutcome outcome = application::CommandOutcome::Succeeded) {
    ASSERT_FALSE(backend->submitted.empty());
    backend->terminals.push_back(application::CommandTerminal{
        .context = application::commandContext(backend->submitted.back()),
        .outcome = outcome,
        .error = std::nullopt,
    });
}

class ReviewControllerTests : public testing::Test {
protected:
    void SetUp() override {
        ensureCoreApplication();
    }
};

TEST_F(ReviewControllerTests, CanonicalizesUnicodeLocalFilesAndDispatchesScopedOpenCommand) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString sourceAPath = createFile(directory, QStringLiteral("source_\u7532.mp4"));
    const QString sourceBPath = createFile(directory, QStringLiteral("source_\u4e59.mp4"));
    ASSERT_FALSE(sourceAPath.isEmpty());
    ASSERT_FALSE(sourceBPath.isEmpty());

    auto backend = std::make_shared<FakeBackend>();
    ReviewController controller{dependenciesFor(backend)};
    std::thread::id notificationThread;
    QObject::connect(&controller, &ReviewController::stateChanged, &controller, [&] {
        notificationThread = std::this_thread::get_id();
    });

    ASSERT_TRUE(controller.openComparison(QUrl::fromLocalFile(sourceAPath),
                                          QUrl::fromLocalFile(sourceBPath)));
    ASSERT_EQ(backend->submitted.size(), 1U);
    const auto* const command =
        std::get_if<application::OpenComparisonCommand>(&backend->submitted.front());
    ASSERT_NE(command, nullptr);
    EXPECT_EQ(command->context.sessionId, domain::SessionId{17U});
    EXPECT_EQ(command->context.sessionEpoch, domain::SessionEpoch{3U});
    EXPECT_EQ(command->context.commandId, domain::CommandId{1U});
    ASSERT_EQ(command->sources.size(), 2U);
    EXPECT_TRUE(command->sources[0].path ==
                std::filesystem::path{QFileInfo{sourceAPath}.canonicalFilePath().toStdWString()});
    EXPECT_TRUE(command->sources[1].path ==
                std::filesystem::path{QFileInfo{sourceBPath}.canonicalFilePath().toStdWString()});
    EXPECT_EQ(controller.sourceAFilename(), QStringLiteral("source_\u7532.mp4"));
    EXPECT_EQ(controller.sourceBFilename(), QStringLiteral("source_\u4e59.mp4"));
    EXPECT_TRUE(controller.busy());
    EXPECT_FALSE(controller.canOpen());
    EXPECT_EQ(backend->lastAccessThread, std::this_thread::get_id());
    EXPECT_EQ(notificationThread, std::this_thread::get_id());
    controller.stop();
}

TEST_F(ReviewControllerTests, DispatchesThreeSourcesWithTheSelectedReferenceRole) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString sourceAPath = createFile(directory, QStringLiteral("reference.mp4"));
    const QString sourceBPath = createFile(directory, QStringLiteral("prediction_1.mp4"));
    const QString sourceCPath = createFile(directory, QStringLiteral("prediction_2.mp4"));
    ASSERT_FALSE(sourceAPath.isEmpty());
    ASSERT_FALSE(sourceBPath.isEmpty());
    ASSERT_FALSE(sourceCPath.isEmpty());

    auto backend = std::make_shared<FakeBackend>();
    ReviewController controller{dependenciesFor(backend)};
    ASSERT_TRUE(controller.openComparisonSet(QUrl::fromLocalFile(sourceAPath),
                                             QUrl::fromLocalFile(sourceBPath),
                                             QUrl::fromLocalFile(sourceCPath),
                                             0));

    ASSERT_EQ(backend->submitted.size(), 1U);
    const auto* const command =
        std::get_if<application::OpenComparisonCommand>(&backend->submitted.front());
    ASSERT_NE(command, nullptr);
    ASSERT_EQ(command->sources.size(), 3U);
    EXPECT_EQ(command->sources[0].role, domain::ComparisonRole::kReference);
    EXPECT_EQ(command->sources[1].role, domain::ComparisonRole::kPrediction);
    EXPECT_EQ(command->sources[2].role, domain::ComparisonRole::kPrediction);
    EXPECT_EQ(controller.sourceCFilename(), QStringLiteral("prediction_2.mp4"));
    EXPECT_TRUE(controller.sourceCErrorKey().isEmpty());
    controller.stop();
}

TEST_F(ReviewControllerTests, RejectsAnAbsentOrOutOfRangeReferenceSource) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString sourceAPath = createFile(directory, QStringLiteral("first.mp4"));
    const QString sourceBPath = createFile(directory, QStringLiteral("second.mp4"));
    ASSERT_FALSE(sourceAPath.isEmpty());
    ASSERT_FALSE(sourceBPath.isEmpty());

    auto backend = std::make_shared<FakeBackend>();
    ReviewController controller{dependenciesFor(backend)};
    EXPECT_FALSE(controller.openComparisonSet(
        QUrl::fromLocalFile(sourceAPath), QUrl::fromLocalFile(sourceBPath), QUrl{}, 2));
    EXPECT_TRUE(backend->submitted.empty());
    controller.stop();
}

TEST_F(ReviewControllerTests, RejectsNonLocalMissingAndDirectoryUrlsWithoutDispatch) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString validPath = createFile(directory, QStringLiteral("valid.mp4"));
    ASSERT_FALSE(validPath.isEmpty());
    auto backend = std::make_shared<FakeBackend>();
    ReviewController controller{dependenciesFor(backend)};

    EXPECT_FALSE(controller.openComparison(QUrl{QStringLiteral("https://example.invalid/a.mp4")},
                                           QUrl::fromLocalFile(validPath)));
    EXPECT_EQ(controller.sourceAErrorKey(), QStringLiteral("invalid-argument"));
    EXPECT_TRUE(controller.sourceAFilename().isEmpty());
    EXPECT_EQ(controller.sourceBFilename(), QStringLiteral("valid.mp4"));
    EXPECT_TRUE(backend->submitted.empty());

    const QString missingPath = directory.filePath(QStringLiteral("missing.mp4"));
    EXPECT_FALSE(controller.openComparison(QUrl::fromLocalFile(validPath),
                                           QUrl::fromLocalFile(missingPath)));
    EXPECT_TRUE(controller.sourceAErrorKey().isEmpty());
    EXPECT_EQ(controller.sourceBErrorKey(), QStringLiteral("source-missing"));
    EXPECT_TRUE(backend->submitted.empty());

    EXPECT_FALSE(controller.openComparison(QUrl::fromLocalFile(directory.path()),
                                           QUrl::fromLocalFile(validPath)));
    EXPECT_EQ(controller.sourceAErrorKey(), QStringLiteral("source-missing"));
    EXPECT_TRUE(backend->submitted.empty());
    controller.stop();
}

TEST_F(ReviewControllerTests, BusyGatesCommandsUntilOnlyTheFullPendingContextCompletes) {
    auto backend = std::make_shared<FakeBackend>();
    backend->currentSnapshot = readySnapshot(3, 10U);
    ReviewController controller{dependenciesFor(backend)};
    ASSERT_TRUE(controller.next());
    ASSERT_TRUE(controller.busy());
    ASSERT_EQ(backend->submitted.size(), 1U);
    const application::CommandContext pending =
        application::commandContext(backend->submitted.front());

    EXPECT_FALSE(controller.first());
    EXPECT_FALSE(controller.previous());
    EXPECT_FALSE(controller.next());
    EXPECT_FALSE(controller.last());
    EXPECT_FALSE(controller.openComparison(QUrl{}, QUrl{}));
    EXPECT_EQ(backend->submitted.size(), 1U);

    backend->terminals.push_back(application::CommandTerminal{
        .context =
            application::CommandContext{
                .sessionId = pending.sessionId,
                .sessionEpoch = pending.sessionEpoch,
                .commandId = domain::CommandId{pending.commandId.value() + 1U},
            },
        .outcome = application::CommandOutcome::Succeeded,
    });
    ASSERT_TRUE(waitUntil([&backend] { return backend->terminals.empty(); }));
    EXPECT_TRUE(controller.busy());

    backend->terminals.push_back(application::CommandTerminal{
        .context =
            application::CommandContext{
                .sessionId = pending.sessionId,
                .sessionEpoch = domain::SessionEpoch{pending.sessionEpoch.value() + 1U},
                .commandId = pending.commandId,
            },
        .outcome = application::CommandOutcome::Succeeded,
    });
    ASSERT_TRUE(waitUntil([&backend] { return backend->terminals.empty(); }));
    EXPECT_TRUE(controller.busy());

    backend->terminals.push_back(application::CommandTerminal{
        .context =
            application::CommandContext{
                .sessionId = domain::SessionId{pending.sessionId.value() + 1U},
                .sessionEpoch = pending.sessionEpoch,
                .commandId = pending.commandId,
            },
        .outcome = application::CommandOutcome::Succeeded,
    });
    ASSERT_TRUE(waitUntil([&backend] { return backend->terminals.empty(); }));
    EXPECT_TRUE(controller.busy());

    backend->terminals.push_back(application::CommandTerminal{
        .context = pending,
        .outcome = application::CommandOutcome::Succeeded,
    });
    EXPECT_TRUE(waitUntil([&controller] { return !controller.busy(); }));
    controller.stop();
}

TEST_F(ReviewControllerTests, ProjectsDisplayFramesGraphicsAndRoleSpecificErrorKeys) {
    auto backend = std::make_shared<FakeBackend>();
    ReviewController controller{dependenciesFor(backend)};
    EXPECT_EQ(controller.displayState(), ReviewController::ReviewDisplayState::Empty);
    EXPECT_TRUE(controller.graphicsReady());
    EXPECT_EQ(controller.currentFrame(), -1);
    EXPECT_EQ(controller.totalFrames(), 0U);

    backend->currentSnapshot = emptySnapshot();
    backend->currentSnapshot.sessionState = domain::SessionState::kLoading;
    ASSERT_TRUE(waitUntil([&controller] {
        return controller.displayState() == ReviewController::ReviewDisplayState::Loading;
    }));

    backend->currentSnapshot = readySnapshot(2, 5U);
    backend->currentSnapshot.presentedSources = {
        application::PresentedSourceState{
            .sourceId = 0U,
            .sourceFrameId = domain::FrameId{2},
            .matchKind = application::FrameMatchKind::ExactIndex,
        },
        application::PresentedSourceState{
            .sourceId = 1U,
            .sourceFrameId = domain::FrameId{3},
            .matchKind = application::FrameMatchKind::AutoAligned,
            .alignmentConfidence = 0.64F,
        },
        application::PresentedSourceState{
            .sourceId = 2U,
            .sourceFrameId = std::nullopt,
            .matchKind = application::FrameMatchKind::Missing,
            .missingReason = application::MissingReason::AfterSourceEnd,
        },
    };
    backend->currentSnapshot.compatibilityWarnings = {
        domain::MediaErrorCode::kSourceFrameCountMismatch};
    backend->currentSnapshot.alignmentEstimates = {
        application::GlobalOffsetEstimate{
            .sourceId = 1U,
            .bestOffset = 1,
            .bestCost = 0.08F,
            .runnerUpCost = 0.22F,
            .confidence = 0.64F,
            .evidenceCount = 5U,
            .autoApplicable = true,
        },
        application::GlobalOffsetEstimate{
            .sourceId = 2U,
            .bestOffset = -2,
            .bestCost = 0.24F,
            .runnerUpCost = 0.25F,
            .confidence = 0.04F,
            .evidenceCount = 5U,
            .autoApplicable = false,
        },
    };
    backend->currentSnapshot.sequenceAlignments = {
        application::SequenceAlignmentSummary{
            .sourceId = 1U,
            .anomalies =
                {
                    application::SequenceAlignmentAnomaly{
                        .kind = application::SequenceAlignmentAnomalyKind::TargetFrameMissing,
                        .canonicalFrameId = domain::FrameId{3},
                    },
                    application::SequenceAlignmentAnomaly{
                        .kind = application::SequenceAlignmentAnomalyKind::TargetFrameDuplicate,
                        .canonicalFrameId = domain::FrameId{4},
                        .sourceFrameId = domain::FrameId{5},
                    },
                },
            .anomalyCount = 2U,
            .lowConfidenceRuns =
                {
                    application::SequenceAlignmentLowConfidenceRun{
                        .firstCanonicalFrame = domain::FrameId{0},
                        .lastCanonicalFrame = domain::FrameId{1},
                        .minimumConfidence = 0.18F,
                    },
                    application::SequenceAlignmentLowConfidenceRun{
                        .firstCanonicalFrame = domain::FrameId{4},
                        .lastCanonicalFrame = domain::FrameId{4},
                        .minimumConfidence = 0.25F,
                    },
                },
            .totalCost = 0.2F,
            .meanMatchCost = 0.02F,
            .confidence = 0.82F,
            .autoApplicable = true,
        },
    };
    backend->currentSnapshot.manualAlignmentAnchors = {
        application::SourceAlignmentAnchors{
            .sourceId = 1U,
            .anchors =
                {
                    application::ManualAlignmentAnchor{
                        .canonicalFrameId = domain::FrameId{3},
                        .sourceFrameId = domain::FrameId{4},
                    },
                    application::ManualAlignmentAnchor{
                        .canonicalFrameId = domain::FrameId{8},
                        .sourceFrameId = domain::FrameId{10},
                    },
                },
        },
    };
    backend->currentSnapshot.lastError =
        domain::makeMediaError(domain::MediaErrorCode::kMediaProbeFailed,
                               domain::MediaOperation::kMediaProbe,
                               domain::SourceId{0},
                               true,
                               "must never be exposed");
    ASSERT_TRUE(waitUntil([&controller] {
        return controller.displayState() == ReviewController::ReviewDisplayState::Ready &&
               controller.currentFrame() == 2;
    }));
    EXPECT_EQ(controller.totalFrames(), 5U);
    EXPECT_TRUE(controller.frameMappingStatus().contains(QStringLiteral("B: source frame 4")));
    EXPECT_TRUE(controller.frameMappingStatus().contains(QStringLiteral("C: Missing frame")));
    EXPECT_FALSE(controller.sourceAMissing());
    EXPECT_FALSE(controller.sourceBMissing());
    EXPECT_TRUE(controller.sourceCMissing());
    EXPECT_TRUE(controller.frameMappingStatus().contains(QStringLiteral("auto offset +1, 64%")));
    EXPECT_TRUE(controller.alignmentEstimateStatus().contains(QStringLiteral("B: auto +1 (64%)")));
    EXPECT_TRUE(controller.alignmentEstimateStatus().contains(
        QStringLiteral("C: suggested -2 (4%, review manually)")));
    EXPECT_TRUE(controller.autoAlignmentActive());
    EXPECT_TRUE(controller.sequenceAlignmentStatus().contains(
        QStringLiteral("B: sequence mapped, 82% confidence")));
    EXPECT_TRUE(controller.sequenceAlignmentStatus().contains(QStringLiteral("missing @ 4")));
    EXPECT_TRUE(controller.sequenceAlignmentStatus().contains(QStringLiteral("duplicate @ 5")));
    EXPECT_TRUE(controller.manualAnchorActive());
    EXPECT_EQ(controller.manualAnchorStatus(), QStringLiteral("B: anchors 4↔5, 9↔11"));
    const QVariantList timelineMarkers = controller.alignmentTimelineMarkers();
    ASSERT_EQ(timelineMarkers.size(), 6);
    EXPECT_EQ(timelineMarkers[0].toMap().value(QStringLiteral("kind")).toString(),
              QStringLiteral("missing"));
    EXPECT_EQ(timelineMarkers[0].toMap().value(QStringLiteral("frame")).toULongLong(), 3U);
    EXPECT_EQ(timelineMarkers[1].toMap().value(QStringLiteral("kind")).toString(),
              QStringLiteral("duplicate"));
    EXPECT_EQ(timelineMarkers[2].toMap().value(QStringLiteral("kind")).toString(),
              QStringLiteral("anchor"));
    EXPECT_EQ(timelineMarkers[2].toMap().value(QStringLiteral("frame")).toULongLong(), 3U);
    EXPECT_EQ(timelineMarkers[4].toMap().value(QStringLiteral("kind")).toString(),
              QStringLiteral("low-confidence"));
    EXPECT_EQ(timelineMarkers[4].toMap().value(QStringLiteral("frame")).toULongLong(), 0U);
    EXPECT_EQ(timelineMarkers[5].toMap().value(QStringLiteral("frame")).toULongLong(), 4U);
    EXPECT_EQ(controller.compatibilityWarningKeys(),
              QStringList{QStringLiteral("source-frame-count-mismatch")});
    EXPECT_EQ(controller.sourceAErrorKey(), QStringLiteral("media-probe-failed"));
    EXPECT_TRUE(controller.sourceBErrorKey().isEmpty());
    EXPECT_TRUE(controller.pairErrorKey().isEmpty());

    backend->currentSnapshot.lastError =
        domain::makeMediaError(domain::MediaErrorCode::kSourceMissing,
                               domain::MediaOperation::kMediaProbe,
                               domain::SourceId{1},
                               true);
    ASSERT_TRUE(waitUntil([&controller] {
        return controller.sourceBErrorKey() == QStringLiteral("source-missing");
    }));
    EXPECT_TRUE(controller.sourceAErrorKey().isEmpty());

    backend->currentSnapshot.lastError =
        domain::makeMediaError(domain::MediaErrorCode::kMediaDecodeFailed,
                               domain::MediaOperation::kMediaDecode,
                               domain::SourceId{2},
                               true);
    ASSERT_TRUE(waitUntil([&controller] {
        return controller.sourceCErrorKey() == QStringLiteral("media-decode-failed");
    }));
    EXPECT_TRUE(controller.sourceAErrorKey().isEmpty());
    EXPECT_TRUE(controller.sourceBErrorKey().isEmpty());

    backend->currentSnapshot.lastError =
        domain::makeMediaError(domain::MediaErrorCode::kSourceFrameCountMismatch,
                               domain::MediaOperation::kSourcePairValidation,
                               std::nullopt,
                               false);
    ASSERT_TRUE(waitUntil([&controller] {
        return controller.pairErrorKey() == QStringLiteral("source-frame-count-mismatch");
    }));
    EXPECT_TRUE(controller.sourceAErrorKey().isEmpty());
    EXPECT_TRUE(controller.sourceBErrorKey().isEmpty());

    backend->currentSnapshot = emptySnapshot(false);
    backend->currentSnapshot.sessionState = domain::SessionState::kInvalid;
    ASSERT_TRUE(waitUntil([&controller] {
        return controller.displayState() == ReviewController::ReviewDisplayState::Invalid &&
               !controller.graphicsReady();
    }));
    backend->currentSnapshot.sessionState = domain::SessionState::kError;
    ASSERT_TRUE(waitUntil([&controller] {
        return controller.displayState() == ReviewController::ReviewDisplayState::Error;
    }));
    controller.stop();
}

TEST_F(ReviewControllerTests, ProjectsContinuousPlaybackWithoutLatchingGeneralBusy) {
    auto backend = std::make_shared<FakeBackend>();
    backend->currentSnapshot = readySnapshot(2, 8U);
    ReviewController controller{dependenciesFor(backend)};

    EXPECT_FALSE(controller.playing());
    EXPECT_TRUE(controller.canPlay());
    EXPECT_FALSE(controller.canPause());
    ASSERT_TRUE(controller.play());
    ASSERT_TRUE(std::holds_alternative<application::PlayCommand>(backend->submitted.back()));
    EXPECT_FALSE(controller.busy());
    EXPECT_FALSE(controller.canPlay());

    backend->currentSnapshot.playbackState = domain::PlaybackState::kPlaying;
    completeLastCommand(backend);
    ASSERT_TRUE(waitUntil([&controller] { return controller.playing() && controller.canPause(); }));
    EXPECT_FALSE(controller.busy());
    EXPECT_FALSE(controller.canOpen());
    // Frame navigation stays enabled during playback: dispatching it pauses first, then seeks.
    EXPECT_TRUE(controller.canFirst());
    EXPECT_TRUE(controller.canPrevious());
    EXPECT_TRUE(controller.canNext());
    EXPECT_TRUE(controller.canLast());

    ASSERT_TRUE(controller.togglePlayback());
    ASSERT_TRUE(std::holds_alternative<application::PauseCommand>(backend->submitted.back()));
    EXPECT_FALSE(controller.busy());
    backend->currentSnapshot.playbackState = domain::PlaybackState::kPaused;
    backend->currentSnapshot.requestedFrame = domain::FrameId{3};
    completeLastCommand(backend);
    ASSERT_TRUE(waitUntil([&controller] {
        return !controller.playing() && !controller.canPause() && !controller.canPlay();
    }));
    // Navigation stays enabled while the pause drains; a new step supersedes the in-flight one.
    EXPECT_TRUE(controller.canFirst());

    backend->currentSnapshot.displayedFrame = domain::FrameId{3};
    backend->currentSnapshot.requestedFrame.reset();
    ASSERT_TRUE(waitUntil([&controller] { return controller.canPlay(); }));
    EXPECT_TRUE(controller.canFirst());
    controller.stop();
}

TEST_F(ReviewControllerTests, OneFrameKeepsFirstAndLastButDisablesMovement) {
    auto backend = std::make_shared<FakeBackend>();
    backend->currentSnapshot = readySnapshot(0, 1U);
    ReviewController controller{dependenciesFor(backend)};
    EXPECT_TRUE(controller.canFirst());
    EXPECT_TRUE(controller.canLast());
    EXPECT_FALSE(controller.canPrevious());
    EXPECT_FALSE(controller.canNext());
    EXPECT_FALSE(controller.canPlay());
    EXPECT_FALSE(controller.canPause());
    EXPECT_FALSE(controller.previous());
    EXPECT_FALSE(controller.next());

    ASSERT_TRUE(controller.first());
    ASSERT_TRUE(std::holds_alternative<application::FirstFrameCommand>(backend->submitted.back()));
    completeLastCommand(backend);
    ASSERT_TRUE(waitUntil([&controller] { return !controller.busy(); }));

    ASSERT_TRUE(controller.last());
    ASSERT_TRUE(std::holds_alternative<application::LastFrameCommand>(backend->submitted.back()));
    EXPECT_EQ(application::commandContext(backend->submitted.front()).commandId,
              domain::CommandId{1U});
    EXPECT_EQ(application::commandContext(backend->submitted.back()).commandId,
              domain::CommandId{2U});
    controller.stop();
}

TEST_F(ReviewControllerTests, NavigationDispatchesExactVariantsAndClampsUiAtBoundaries) {
    auto backend = std::make_shared<FakeBackend>();
    backend->currentSnapshot = readySnapshot(5, 10U);
    ReviewController controller{dependenciesFor(backend)};

    ASSERT_TRUE(controller.previous());
    ASSERT_EQ(std::get<application::StepFramesCommand>(backend->submitted.back()).delta, -1);
    completeLastCommand(backend);
    ASSERT_TRUE(waitUntil([&controller] { return !controller.busy(); }));

    ASSERT_TRUE(controller.next());
    ASSERT_EQ(std::get<application::StepFramesCommand>(backend->submitted.back()).delta, 1);
    completeLastCommand(backend);
    ASSERT_TRUE(waitUntil([&controller] { return !controller.busy(); }));

    ASSERT_TRUE(controller.first());
    EXPECT_TRUE(std::holds_alternative<application::FirstFrameCommand>(backend->submitted.back()));
    completeLastCommand(backend);
    ASSERT_TRUE(waitUntil([&controller] { return !controller.busy(); }));

    ASSERT_TRUE(controller.last());
    EXPECT_TRUE(std::holds_alternative<application::LastFrameCommand>(backend->submitted.back()));
    completeLastCommand(backend);
    ASSERT_TRUE(waitUntil([&controller] { return !controller.busy(); }));

    backend->currentSnapshot.displayedFrame = domain::FrameId{0};
    ASSERT_TRUE(waitUntil([&controller] { return !controller.canPrevious(); }));
    EXPECT_TRUE(controller.canNext());
    backend->currentSnapshot.displayedFrame = domain::FrameId{9};
    ASSERT_TRUE(waitUntil([&controller] { return !controller.canNext(); }));
    EXPECT_TRUE(controller.canPrevious());
    controller.stop();
}

TEST_F(ReviewControllerTests, GenericStepAndSeekDispatchExactCommandsAndRejectInvalidTargets) {
    auto backend = std::make_shared<FakeBackend>();
    backend->currentSnapshot = readySnapshot(5, 10U);
    ReviewController controller{dependenciesFor(backend)};

    ASSERT_TRUE(controller.stepFrames(-5));
    ASSERT_EQ(std::get<application::StepFramesCommand>(backend->submitted.back()).delta, -5);
    completeLastCommand(backend);
    ASSERT_TRUE(waitUntil([&controller] { return !controller.busy(); }));

    ASSERT_TRUE(controller.stepFrames(10));
    ASSERT_EQ(std::get<application::StepFramesCommand>(backend->submitted.back()).delta, 10);
    completeLastCommand(backend);
    ASSERT_TRUE(waitUntil([&controller] { return !controller.busy(); }));

    ASSERT_TRUE(controller.seekFrame(8));
    EXPECT_EQ(std::get<application::SeekFrameCommand>(backend->submitted.back()).frameId,
              domain::FrameId{8});
    completeLastCommand(backend);
    ASSERT_TRUE(waitUntil([&controller] { return !controller.busy(); }));

    const std::size_t submitted = backend->submitted.size();
    EXPECT_FALSE(controller.stepFrames(0));
    EXPECT_FALSE(controller.seekFrame(-1));
    EXPECT_FALSE(controller.seekFrame(10));
    EXPECT_FALSE(controller.seekFrame(5));
    EXPECT_EQ(backend->submitted.size(), submitted);

    backend->currentSnapshot.displayedFrame = domain::FrameId{0};
    ASSERT_TRUE(waitUntil([&controller] { return controller.currentFrame() == 0; }));
    EXPECT_FALSE(controller.stepFrames(-5));
    EXPECT_TRUE(controller.stepFrames(5));
    completeLastCommand(backend);
    ASSERT_TRUE(waitUntil([&controller] { return !controller.busy(); }));

    backend->currentSnapshot.displayedFrame = domain::FrameId{9};
    ASSERT_TRUE(waitUntil([&controller] { return controller.currentFrame() == 9; }));
    EXPECT_FALSE(controller.stepFrames(5));
    EXPECT_TRUE(controller.stepFrames(-5));
    controller.stop();
}

TEST_F(ReviewControllerTests, DispatchesExplicitAlignmentOffsetsAsOneAtomicCommand) {
    auto backend = std::make_shared<FakeBackend>();
    backend->currentSnapshot = readySnapshot(5, 10U);
    ReviewController controller{dependenciesFor(backend)};

    ASSERT_TRUE(controller.applyAlignmentOffsets(0, 2, 7));
    ASSERT_EQ(backend->submitted.size(), 1U);
    const auto* const command =
        std::get_if<application::SetAlignmentOffsetsCommand>(&backend->submitted.back());
    ASSERT_NE(command, nullptr);
    ASSERT_EQ(command->sourceOffsets.size(), 1U);
    EXPECT_EQ(command->sourceOffsets.front(),
              (application::SourceFrameOffset{.sourceId = 1U, .frames = 2}));
    EXPECT_TRUE(controller.busy());
    controller.stop();
}

TEST_F(ReviewControllerTests, DispatchesAutomaticAlignmentWithoutBlockingNavigation) {
    auto backend = std::make_shared<FakeBackend>();
    backend->currentSnapshot = readySnapshot(5, 10U);
    ReviewController controller{dependenciesFor(backend)};

    ASSERT_TRUE(controller.estimateAlignment());
    ASSERT_EQ(backend->submitted.size(), 1U);
    EXPECT_NE(std::get_if<application::EstimateAlignmentCommand>(&backend->submitted.back()),
              nullptr);
    EXPECT_FALSE(controller.busy());
    EXPECT_TRUE(controller.next());
    EXPECT_TRUE(controller.busy());
    controller.stop();
}

TEST_F(ReviewControllerTests, DispatchesSequenceAnalysisWithoutBlockingNavigation) {
    auto backend = std::make_shared<FakeBackend>();
    backend->currentSnapshot = readySnapshot(5, 10U);
    ReviewController controller{dependenciesFor(backend)};

    ASSERT_TRUE(controller.analyzeSequenceAlignment());
    ASSERT_EQ(backend->submitted.size(), 1U);
    EXPECT_NE(std::get_if<application::AnalyzeSequenceAlignmentCommand>(&backend->submitted.back()),
              nullptr);
    EXPECT_FALSE(controller.busy());
    EXPECT_TRUE(controller.previous());
    EXPECT_TRUE(controller.busy());
    controller.stop();
}

TEST_F(ReviewControllerTests, ProjectsAnalysisProgressAndDispatchesCancellation) {
    auto backend = std::make_shared<FakeBackend>();
    backend->currentSnapshot = readySnapshot(5, 10U);
    backend->currentSnapshot.alignmentAnalysisJobId = application::AlignmentAnalysisJobId{7U};
    backend->currentSnapshot.alignmentAnalysisKind = application::AlignmentAnalysisKind::Sequence;
    backend->currentSnapshot.alignmentAnalysisCompletedFrames = 25U;
    backend->currentSnapshot.alignmentAnalysisTotalFrames = 100U;
    ReviewController controller{dependenciesFor(backend)};

    EXPECT_TRUE(controller.alignmentAnalysisRunning());
    EXPECT_DOUBLE_EQ(controller.alignmentAnalysisProgress(), 0.25);
    EXPECT_FALSE(controller.alignmentAnalysisStatus().isEmpty());
    ASSERT_TRUE(controller.cancelAlignmentAnalysis());
    ASSERT_EQ(backend->submitted.size(), 1U);
    EXPECT_NE(std::get_if<application::CancelAlignmentAnalysisCommand>(&backend->submitted.back()),
              nullptr);
    EXPECT_FALSE(controller.busy());
    controller.stop();
}

TEST_F(ReviewControllerTests, DispatchesManualAnchorUpsertAndClearCommands) {
    auto backend = std::make_shared<FakeBackend>();
    backend->currentSnapshot = readySnapshot(5, 10U);
    ReviewController controller{dependenciesFor(backend)};

    EXPECT_FALSE(controller.setManualAlignmentAnchor(-1, 5, 6));
    EXPECT_FALSE(controller.setManualAlignmentAnchor(1, -1, 6));
    ASSERT_TRUE(controller.setManualAlignmentAnchor(1, 5, 6));
    ASSERT_EQ(backend->submitted.size(), 1U);
    const auto* const anchor =
        std::get_if<application::SetManualAlignmentAnchorCommand>(&backend->submitted.back());
    ASSERT_NE(anchor, nullptr);
    EXPECT_EQ(anchor->sourceId, 1U);
    EXPECT_EQ(anchor->anchor.canonicalFrameId, domain::FrameId{5});
    EXPECT_EQ(anchor->anchor.sourceFrameId, domain::FrameId{6});

    completeLastCommand(backend);
    ASSERT_TRUE(waitUntil([&controller] { return !controller.busy(); }));
    ASSERT_TRUE(controller.clearManualAlignmentAnchors());
    EXPECT_NE(
        std::get_if<application::ClearManualAlignmentAnchorsCommand>(&backend->submitted.back()),
        nullptr);
    controller.stop();
}

TEST_F(ReviewControllerTests, BusyAndClosedSubmissionsDoNotLatchAndIdsUseLatestEpoch) {
    auto backend = std::make_shared<FakeBackend>();
    backend->currentSnapshot = readySnapshot(5, 10U, 3U);
    backend->submitResult = application::PortSubmitResult::Busy;
    ReviewController controller{dependenciesFor(backend)};

    EXPECT_FALSE(controller.next());
    EXPECT_FALSE(controller.busy());
    ASSERT_EQ(backend->submitted.size(), 1U);
    EXPECT_EQ(application::commandContext(backend->submitted.back()).commandId,
              domain::CommandId{1U});

    const std::size_t snapshotCallsBeforeEpochChange = backend->snapshotCalls;
    backend->currentSnapshot.sessionEpoch = domain::SessionEpoch{4U};
    ASSERT_TRUE(waitUntil([&backend, snapshotCallsBeforeEpochChange] {
        return backend->snapshotCalls > snapshotCallsBeforeEpochChange;
    }));
    backend->submitResult = application::PortSubmitResult::Closed;
    EXPECT_FALSE(controller.previous());
    EXPECT_FALSE(controller.busy());
    ASSERT_EQ(backend->submitted.size(), 2U);
    EXPECT_EQ(application::commandContext(backend->submitted.back()).sessionEpoch,
              domain::SessionEpoch{4U});
    EXPECT_EQ(application::commandContext(backend->submitted.back()).commandId,
              domain::CommandId{2U});

    backend->submitResult = application::PortSubmitResult::Accepted;
    ASSERT_TRUE(controller.first());
    EXPECT_EQ(application::commandContext(backend->submitted.back()).commandId,
              domain::CommandId{3U});
    controller.stop();
}

TEST_F(ReviewControllerTests, StopAndExpiredBackendFailClosedWithoutFurtherAccess) {
    auto backend = std::make_shared<FakeBackend>();
    backend->currentSnapshot = readySnapshot(4, 10U);
    ReviewController controller{dependenciesFor(backend)};
    ASSERT_TRUE(controller.next());
    ASSERT_TRUE(controller.busy());

    std::thread::id notificationThread;
    QObject::connect(&controller, &ReviewController::stateChanged, &controller, [&] {
        notificationThread = std::this_thread::get_id();
    });
    std::thread stopper{[&controller] { controller.stop(); }};
    stopper.join();
    ASSERT_TRUE(waitUntil([&controller] { return !controller.graphicsReady(); }));
    EXPECT_EQ(notificationThread, std::this_thread::get_id());
    const std::size_t accesses =
        backend->submitCalls + backend->snapshotCalls + backend->drainCalls;
    EXPECT_FALSE(controller.busy());
    EXPECT_FALSE(controller.graphicsReady());
    EXPECT_FALSE(controller.canOpen());
    EXPECT_FALSE(controller.canFirst());
    EXPECT_FALSE(controller.canPrevious());
    EXPECT_FALSE(controller.canNext());
    EXPECT_FALSE(controller.canLast());
    EXPECT_FALSE(controller.canPlay());
    EXPECT_FALSE(controller.canPause());
    EXPECT_FALSE(controller.playing());
    EXPECT_FALSE(controller.first());
    EXPECT_FALSE(controller.play());
    EXPECT_FALSE(controller.pause());
    EXPECT_FALSE(controller.togglePlayback());
    EXPECT_FALSE(controller.openComparison(QUrl{}, QUrl{}));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    EXPECT_EQ(backend->submitCalls + backend->snapshotCalls + backend->drainCalls, accesses);

    auto expiringBackend = std::make_shared<FakeBackend>();
    expiringBackend->currentSnapshot = readySnapshot(4, 10U);
    ReviewController expiringController{dependenciesFor(expiringBackend)};
    ASSERT_TRUE(expiringController.graphicsReady());
    expiringBackend.reset();
    ASSERT_TRUE(waitUntil([&expiringController] {
        return !expiringController.graphicsReady() && !expiringController.canOpen();
    }));
    EXPECT_FALSE(expiringController.next());
}

} // namespace
} // namespace dvs::ui
