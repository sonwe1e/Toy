#include "dvs/domain/Project.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <utility>

namespace dvs::domain {
namespace {

[[nodiscard]] RationalRate makeRate() {
    auto result = RationalRate::create(30, 1);
    EXPECT_TRUE(result.hasValue());
    return std::move(result).value();
}

[[nodiscard]] MediaDescriptor makeDescriptor(const std::filesystem::path& path,
                                             const RationalRate& rate,
                                             const std::int64_t frameCount,
                                             const MediaExtent extent) {
    return MediaDescriptor{
        .normalizedPath = path,
        .extent = extent,
        .frameRate = rate,
        .frameCount = FrameCountInfo{.value = frameCount, .origin = FrameCountOrigin::kReported},
        .duration = MediaTime{frameCount * 1'000'000 / 30},
        .codecId = "h264",
        .pixelFormatId = "yuv420p",
        .bitDepth = 8,
        .decodeCapabilities = DecodeCapabilities{.softwareDecode = true, .d3d11VaDecode = false},
        .timingConfidence = TimingConfidence::kDeclaredCfr,
        .sourceIdentity = std::nullopt,
    };
}

[[nodiscard]] ValidatedSourcePair
makePair(const std::int64_t frameCount = 5,
         const MediaExtent extentA = MediaExtent{.width = 1'920, .height = 1'080},
         const MediaExtent extentB = MediaExtent{.width = 1'280, .height = 720}) {
    const RationalRate rate = makeRate();
    auto result = SourcePairValidator::validate(makeDescriptor("a.mp4", rate, frameCount, extentA),
                                                makeDescriptor("b.mp4", rate, frameCount, extentB));
    EXPECT_TRUE(result.hasValue());
    return std::move(result).value();
}

[[nodiscard]] Project makeProject(const std::int64_t frameCount = 5) {
    auto result = Project::create(ProjectId{"project-1"}, "Project", makePair(frameCount));
    EXPECT_TRUE(result.hasValue());
    return std::move(result).value();
}

} // namespace

TEST(ProjectTests, AcceptsBoundaryMarksAndRejectsOutOfRangeMarks) {
    Project project = makeProject(1);

    ASSERT_TRUE(project.setInMark(FrameId{0}).hasValue());
    ASSERT_TRUE(project.setOutMark(FrameId{0}).hasValue());
    EXPECT_EQ(*project.inMark(), FrameId{0});
    EXPECT_EQ(*project.outMark(), FrameId{0});

    const auto invalidMark = project.setOutMark(FrameId{1});
    ASSERT_FALSE(invalidMark.hasValue());
    EXPECT_EQ(invalidMark.error().code, MediaErrorCode::kFrameOutOfRange);
}

TEST(ProjectTests, LeavesReversedMarksEditable) {
    Project project = makeProject();

    ASSERT_TRUE(project.setInMark(FrameId{4}).hasValue());
    ASSERT_TRUE(project.setOutMark(FrameId{2}).hasValue());
    ASSERT_TRUE(project.inMark().has_value());
    ASSERT_TRUE(project.outMark().has_value());
    EXPECT_EQ(*project.inMark(), FrameId{4});
    EXPECT_EQ(*project.outMark(), FrameId{2});

    ASSERT_TRUE(project.setInMark(FrameId{3}).hasValue());
    ASSERT_TRUE(project.setOutMark(FrameId{4}).hasValue());
    EXPECT_EQ(*project.inMark(), FrameId{3});
    EXPECT_EQ(*project.outMark(), FrameId{4});
}

TEST(ProjectTests, RestoresPersistedMarksAndPreservesLiveReplacementState) {
    ProjectState persisted{
        .id = ProjectId{"project-restore"},
        .displayName = "Restored",
        .sources = makePair(),
        .inMark = FrameId{0},
        .outMark = FrameId{1},
        .lastDisplayedFrame = FrameId{0},
        .workspaceState = WorkspaceState{{"inspector-visible", "true"}},
    };

    const auto restored = Project::restorePersisted(std::move(persisted));
    ASSERT_TRUE(restored.hasValue());
    ASSERT_TRUE(restored.value().inMark().has_value());
    EXPECT_EQ(*restored.value().inMark(), FrameId{0});
    ASSERT_TRUE(restored.value().outMark().has_value());
    EXPECT_EQ(*restored.value().outMark(), FrameId{1});
    EXPECT_EQ(restored.value().lastDisplayedFrame(), FrameId{0});
    EXPECT_EQ(restored.value().workspaceState().at("inspector-visible"), "true");

    Project liveProject = makeProject();
    ASSERT_TRUE(liveProject.setInMark(FrameId{0}));
    ASSERT_TRUE(liveProject.setOutMark(FrameId{1}));
    const auto replacement = liveProject.replaceSources(makePair());
    ASSERT_TRUE(replacement);
    ASSERT_TRUE(replacement.value().inMark().has_value());
    EXPECT_EQ(*replacement.value().inMark(), FrameId{0});
    EXPECT_EQ(*replacement.value().outMark(), FrameId{1});
    EXPECT_EQ(liveProject.sources().canonicalFrameCount(), 5);
}

TEST(ProjectTests, RejectsInvalidSourceReplacementWithoutMutatingTheOriginalProject) {
    Project project = makeProject(5);
    ASSERT_TRUE(project.setInMark(FrameId{3}));
    ASSERT_TRUE(project.setOutMark(FrameId{4}));

    const auto rejected = project.replaceSources(makePair(3));
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, MediaErrorCode::kFrameOutOfRange);
    EXPECT_EQ(project.sources().canonicalFrameCount(), 5);
    ASSERT_TRUE(project.inMark().has_value());
    EXPECT_EQ(*project.inMark(), FrameId{3});
    EXPECT_EQ(*project.outMark(), FrameId{4});
}

TEST(ProjectTests, RejectsInvalidCreationAndSupportsMarkAndWorkspaceMutations) {
    const auto noId = Project::create(ProjectId{""}, "Project", makePair());
    ASSERT_FALSE(noId.hasValue());
    EXPECT_EQ(noId.error().code, MediaErrorCode::kInvalidArgument);

    const auto noName = Project::create(ProjectId{"project"}, "", makePair());
    ASSERT_FALSE(noName.hasValue());
    EXPECT_EQ(noName.error().code, MediaErrorCode::kInvalidArgument);

    Project project = makeProject();
    ASSERT_TRUE(project.setInMark(FrameId{0}).hasValue());
    ASSERT_TRUE(project.setOutMark(FrameId{1}).hasValue());
    ASSERT_TRUE(project.setLastDisplayedFrame(FrameId{2}).hasValue());
    EXPECT_EQ(project.lastDisplayedFrame(), FrameId{2});

    const auto invalidLast = project.setLastDisplayedFrame(FrameId{5});
    ASSERT_FALSE(invalidLast.hasValue());
    EXPECT_EQ(invalidLast.error().code, MediaErrorCode::kFrameOutOfRange);

    project.clearMarks();
    EXPECT_FALSE(project.inMark().has_value());
    EXPECT_FALSE(project.outMark().has_value());

    WorkspaceState workspace{{"inspector-visible", "true"}};
    project.setWorkspaceState(std::move(workspace));
    EXPECT_EQ(project.workspaceState().at("inspector-visible"), "true");
}

} // namespace dvs::domain
