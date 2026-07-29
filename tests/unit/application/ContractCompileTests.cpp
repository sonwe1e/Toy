#include "dvs/application/Commands.h"
#include "dvs/application/Ports.h"
#include "dvs/application/SessionSnapshot.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace dvs::application {
namespace {

class TestFrameResource final : public IFrameResource {};

static_assert(std::is_copy_constructible_v<RequestContext>);
static_assert(std::is_copy_constructible_v<FrameSet>);
static_assert(std::is_abstract_v<IMediaProbe>);
static_assert(std::is_abstract_v<IFrameProvider>);
static_assert(std::is_abstract_v<IApplicationEventSink>);
static_assert(std::is_abstract_v<IRenderChannel>);
static_assert(std::is_same_v<decltype(FrameProviderOpenRequest::context), PlaybackRequestContext>);
static_assert(std::is_same_v<decltype(FrameProviderOpenRequest::sources),
                             std::vector<domain::ComparisonSource>>);
static_assert(
    std::is_same_v<decltype(FrameProviderOpenRequest::canonicalSourceId), domain::SourceId>);
static_assert(std::is_same_v<decltype(SessionSnapshot::sequenceAlignments),
                             std::vector<SequenceAlignmentSummary>>);
static_assert(std::is_same_v<decltype(FrameProviderCloseRequest::context), PlaybackRequestContext>);
static_assert(std::is_same_v<decltype(ProjectSaveRequest::projectPath), std::filesystem::path>);
static_assert(std::is_same_v<decltype(ProjectRelinkRequest::context), RequestContext>);
static_assert(std::is_same_v<decltype(ProjectRelinkRequest::sourceId), domain::SourceId>);
static_assert(std::is_same_v<decltype(ProjectRelinkRequest::newSourcePath), std::filesystem::path>);
static_assert(
    std::is_same_v<decltype(SourceRelinkPrepared::candidate), domain::SourceRelinkCandidate>);
static_assert(std::is_same_v<decltype(OpenComparisonCommand::context), CommandContext>);
static_assert(
    std::is_same_v<decltype(OpenComparisonCommand::sources), std::vector<OpenComparisonSource>>);
static_assert(std::is_same_v<decltype(OpenComparisonSource::path), std::filesystem::path>);
static_assert(
    std::is_same_v<decltype(std::get<OpenComparisonCommand>(std::declval<PlaybackCommand&>())),
                   OpenComparisonCommand&>);
static_assert(std::is_same_v<decltype(PlayCommand::context), CommandContext>);
static_assert(std::is_same_v<decltype(PauseCommand::context), CommandContext>);
static_assert(std::is_same_v<decltype(std::get<PlayCommand>(std::declval<PlaybackCommand&>())),
                             PlayCommand&>);
static_assert(std::is_same_v<decltype(std::get<PauseCommand>(std::declval<PlaybackCommand&>())),
                             PauseCommand&>);
static_assert(
    std::is_same_v<decltype(GraphicsEventContext::deviceGeneration), domain::DeviceGeneration>);
static_assert(std::is_same_v<decltype(GraphicsDeviceReady::context), GraphicsEventContext>);
static_assert(std::is_same_v<decltype(GraphicsDeviceUnavailable::context), GraphicsEventContext>);
static_assert(std::is_same_v<decltype(GraphicsDeviceUnavailable::error), domain::MediaError>);
static_assert(std::is_same_v<decltype(GraphicsDeviceLost::context), GraphicsEventContext>);
static_assert(std::is_same_v<decltype(GraphicsDeviceLost::error), domain::MediaError>);
static_assert(
    std::is_same_v<decltype(std::get<GraphicsDeviceReady>(std::declval<ApplicationEvent&>())),
                   GraphicsDeviceReady&>);
static_assert(
    std::is_same_v<decltype(std::get<GraphicsDeviceUnavailable>(std::declval<ApplicationEvent&>())),
                   GraphicsDeviceUnavailable&>);
static_assert(
    std::is_same_v<decltype(std::get<GraphicsDeviceLost>(std::declval<ApplicationEvent&>())),
                   GraphicsDeviceLost&>);

[[nodiscard]] FrameGeometry validGeometry() {
    return FrameGeometry{
        .width = 16,
        .height = 16,
        .textureRegion = TextureRegion{},
    };
}

[[nodiscard]] MappedSourceFrame mapped(const domain::SourceId sourceId, const FrameHandle& frame) {
    return MappedSourceFrame{
        .sourceId = sourceId,
        .sourceFrameId = domain::FrameId{0},
        .frame = frame,
        .presentationTime = domain::MediaTime{0},
        .matchKind = FrameMatchKind::ExactIndex,
        .alignmentConfidence = 1.0F,
    };
}

[[nodiscard]] MappedSourceFrame missing(const domain::SourceId sourceId) {
    return MappedSourceFrame{
        .sourceId = sourceId,
        .sourceFrameId = std::nullopt,
        .frame = std::nullopt,
        .presentationTime = domain::MediaTime{0},
        .matchKind = FrameMatchKind::Missing,
        .alignmentConfidence = 0.0F,
        .missingReason = MissingReason::AlignmentGap,
    };
}

} // namespace

TEST(ContractCompileTests, FrameSetRequiresCanonicalValidityAndConsistentEntries) {
    const auto missingResource = FrameHandle::create(nullptr, validGeometry(), 0);
    EXPECT_FALSE(missingResource.has_value());

    const auto handleA =
        FrameHandle::create(std::make_shared<const TestFrameResource>(), validGeometry(), 1'024);
    const auto handleB =
        FrameHandle::create(std::make_shared<const TestFrameResource>(), validGeometry(), 1'024);
    ASSERT_TRUE(handleA.has_value());
    ASSERT_TRUE(handleB.has_value());

    const auto invalidId = FrameSet::create(
        domain::FrameId{-1}, domain::MediaTime{0}, {mapped(0, *handleA), mapped(1, *handleB)});
    EXPECT_FALSE(invalidId.has_value());

    const auto negativeTime = FrameSet::create(
        domain::FrameId{0}, domain::MediaTime{-1}, {mapped(0, *handleA), mapped(1, *handleB)});
    EXPECT_FALSE(negativeTime.has_value());

    const auto duplicateSources = FrameSet::create(
        domain::FrameId{0}, domain::MediaTime{0}, {mapped(0, *handleA), mapped(0, *handleB)});
    EXPECT_FALSE(duplicateSources.has_value());

    const auto inconsistentEntry = FrameSet::create(domain::FrameId{0},
                                                    domain::MediaTime{0},
                                                    {MappedSourceFrame{
                                                         .sourceId = 0,
                                                         .sourceFrameId = domain::FrameId{0},
                                                         .frame = *handleA,
                                                         .presentationTime = domain::MediaTime{0},
                                                         .matchKind = FrameMatchKind::Missing,
                                                         .alignmentConfidence = 1.0F,
                                                     },
                                                     mapped(1, *handleB)});
    EXPECT_FALSE(inconsistentEntry.has_value());

    const auto complete = FrameSet::create(
        domain::FrameId{0}, domain::MediaTime{0}, {mapped(0, *handleA), mapped(1, *handleB)});
    ASSERT_TRUE(complete.has_value());
    EXPECT_EQ(complete->canonicalFrameId(), domain::FrameId{0});
    EXPECT_TRUE(complete->isComplete());
    ASSERT_NE(complete->find(1), nullptr);
    EXPECT_EQ(complete->find(1)->sourceFrameId, domain::FrameId{0});
    EXPECT_EQ(complete->find(2), nullptr);

    const auto partial = FrameSet::create(
        domain::FrameId{0}, domain::MediaTime{0}, {mapped(0, *handleA), missing(1)});
    ASSERT_TRUE(partial.has_value());
    EXPECT_FALSE(partial->isComplete());
    ASSERT_NE(partial->find(1), nullptr);
    EXPECT_FALSE(partial->find(1)->hasFrame());
}

TEST(ContractCompileTests, RequestContextsKeepIndependentInvalidationScopes) {
    const RequestContext request{
        .sessionId = domain::SessionId{10},
        .sessionEpoch = domain::SessionEpoch{3},
        .requestId = domain::RequestId{99},
    };
    const PlaybackRequestContext playback{
        .request = request,
        .playbackGeneration = domain::PlaybackGeneration{8},
    };
    const SaveRequestContext save{
        .request = request,
        .projectRevision = domain::ProjectRevision{2},
    };

    EXPECT_EQ(playback.request.sessionEpoch, domain::SessionEpoch{3});
    EXPECT_EQ(save.projectRevision, domain::ProjectRevision{2});
    EXPECT_NE(playback.playbackGeneration.value(), save.projectRevision.value());
}

TEST(ContractCompileTests, SnapshotCarriesIndependentGraphicsReadiness) {
    SessionSnapshot empty;
    EXPECT_FALSE(empty.graphicsReady);
    EXPECT_TRUE(empty.isConsistent());

    empty.graphicsReady = true;
    EXPECT_TRUE(empty.isConsistent());

    SessionSnapshot ready{
        .sessionId = domain::SessionId{10},
        .sessionEpoch = domain::SessionEpoch{3},
        .playbackGeneration = domain::PlaybackGeneration{8},
        .deviceGeneration = domain::DeviceGeneration{2},
        .sessionState = domain::SessionState::kReady,
        .playbackState = domain::PlaybackState::kPaused,
        .displayedFrame = domain::FrameId{0},
        .requestedFrame = std::nullopt,
        .canonicalFrameCount = 1U,
        .sources =
            {
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
            },
        .lastError = std::nullopt,
    };
    EXPECT_FALSE(ready.graphicsReady);
    EXPECT_TRUE(ready.isConsistent());

    ready.graphicsReady = true;
    EXPECT_TRUE(ready.isConsistent());
}

} // namespace dvs::application
