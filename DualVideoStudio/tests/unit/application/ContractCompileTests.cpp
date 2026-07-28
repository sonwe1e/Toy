#include "dvs/application/Commands.h"
#include "dvs/application/Ports.h"
#include "dvs/application/SessionSnapshot.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

namespace dvs::application {
namespace {

class TestFrameResource final : public IFrameResource {};

static_assert(std::is_copy_constructible_v<RequestContext>);
static_assert(std::is_copy_constructible_v<FramePair>);
static_assert(std::is_abstract_v<IMediaProbe>);
static_assert(std::is_abstract_v<IFrameProvider>);
static_assert(std::is_abstract_v<IApplicationEventSink>);
static_assert(std::is_abstract_v<IRenderChannel>);
static_assert(std::is_same_v<decltype(FrameProviderOpenRequest::context), PlaybackRequestContext>);
static_assert(std::is_same_v<decltype(FrameProviderCloseRequest::context), PlaybackRequestContext>);
static_assert(std::is_same_v<decltype(ProjectSaveRequest::projectPath), std::filesystem::path>);
static_assert(std::is_same_v<decltype(ProjectRelinkRequest::context), RequestContext>);
static_assert(std::is_same_v<decltype(ProjectRelinkRequest::newSourcePath), std::filesystem::path>);
static_assert(
    std::is_same_v<decltype(SourceRelinkPrepared::candidate), domain::SourceRelinkCandidate>);
static_assert(std::is_same_v<decltype(OpenSourcePathsCommand::context), CommandContext>);
static_assert(std::is_same_v<decltype(OpenSourcePathsCommand::sourceAPath), std::filesystem::path>);
static_assert(std::is_same_v<decltype(OpenSourcePathsCommand::sourceBPath), std::filesystem::path>);
static_assert(
    std::is_same_v<decltype(std::get<OpenSourcePathsCommand>(std::declval<PlaybackCommand&>())),
                   OpenSourcePathsCommand&>);
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

} // namespace

TEST(ContractCompileTests, FrameHandleAndPairRequireCompleteValidResources) {
    const auto missingResource = FrameHandle::create(nullptr, validGeometry(), 0);
    EXPECT_FALSE(missingResource.has_value());

    const auto handleA =
        FrameHandle::create(std::make_shared<const TestFrameResource>(), validGeometry(), 1'024);
    const auto handleB =
        FrameHandle::create(std::make_shared<const TestFrameResource>(), validGeometry(), 1'024);
    ASSERT_TRUE(handleA.has_value());
    ASSERT_TRUE(handleB.has_value());

    const auto invalidPair = FramePair::create(domain::FrameId{-1},
                                               domain::MediaTime{0},
                                               *handleA,
                                               domain::MediaTime{0},
                                               *handleB,
                                               domain::MediaTime{0},
                                               ActiveFrameSource::Direct);
    EXPECT_FALSE(invalidPair.has_value());

    const auto negativeTimePair = FramePair::create(domain::FrameId{0},
                                                    domain::MediaTime{-1},
                                                    *handleA,
                                                    domain::MediaTime{0},
                                                    *handleB,
                                                    domain::MediaTime{0},
                                                    ActiveFrameSource::Direct);
    EXPECT_FALSE(negativeTimePair.has_value());

    const auto completePair = FramePair::create(domain::FrameId{0},
                                                domain::MediaTime{0},
                                                *handleA,
                                                domain::MediaTime{0},
                                                *handleB,
                                                domain::MediaTime{0},
                                                ActiveFrameSource::Proxy);
    ASSERT_TRUE(completePair.has_value());
    EXPECT_EQ(completePair->source(), ActiveFrameSource::Proxy);
    EXPECT_EQ(completePair->frameId(), domain::FrameId{0});
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
    const JobRequestContext job{
        .request = request,
        .jobId = domain::JobId{4},
        .jobAttempt = domain::JobAttempt{2},
    };

    EXPECT_EQ(playback.request.sessionEpoch, domain::SessionEpoch{3});
    EXPECT_EQ(job.jobAttempt, domain::JobAttempt{2});
    EXPECT_NE(playback.playbackGeneration.value(), job.jobAttempt.value());
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
        .activeFrameSource = ActiveFrameSource::Direct,
        .displayedFrame = domain::FrameId{0},
        .requestedFrame = std::nullopt,
        .canonicalFrameCount = 1U,
        .lastError = std::nullopt,
    };
    EXPECT_FALSE(ready.graphicsReady);
    EXPECT_TRUE(ready.isConsistent());

    ready.graphicsReady = true;
    EXPECT_TRUE(ready.isConsistent());
}

} // namespace dvs::application
