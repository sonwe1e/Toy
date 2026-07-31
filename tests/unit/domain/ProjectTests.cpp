#include "dvs/domain/ComparisonValidator.h"
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

[[nodiscard]] ValidatedComparisonSet
makeSet(const std::int64_t frameCount = 5,
        const MediaExtent extentA = MediaExtent{.width = 1'920, .height = 1'080},
        const MediaExtent extentB = MediaExtent{.width = 1'280, .height = 720}) {
    const RationalRate rate = makeRate();
    auto result = ComparisonValidator::validate({
        ComparisonSource{
            .id = 0,
            .role = ComparisonRole::kPrediction,
            .descriptor = makeDescriptor("a.mp4", rate, frameCount, extentA),
            .displayName = "a",
        },
        ComparisonSource{
            .id = 1,
            .role = ComparisonRole::kPrediction,
            .descriptor = makeDescriptor("b.mp4", rate, frameCount, extentB),
            .displayName = "b",
        },
    });
    EXPECT_TRUE(result.hasValue());
    return std::move(result).value().set;
}

[[nodiscard]] Project makeProject(const std::int64_t frameCount = 5) {
    auto result = Project::create(ProjectId{"project-1"}, "Project", makeSet(frameCount));
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
        .sources = makeSet(),
        .inMark = FrameId{0},
        .outMark = FrameId{1},
        .lastDisplayedFrame = FrameId{0},
        .workspaceState = WorkspaceState{{"inspector-visible", "true"}},
        .alignmentState =
            ProjectAlignmentState{
                .mode = ProjectAlignmentMode::kManualAnchors,
                .offsets =
                    {
                        PersistedAlignmentOffset{.sourceId = 1U, .frames = 1},
                    },
                .anchors =
                    {
                        PersistedAlignmentAnchor{
                            .sourceId = 1U,
                            .canonicalFrame = FrameId{0},
                            .sourceFrame = FrameId{1},
                        },
                    },
                .analysisCacheKey = "alignment-cache-key",
            },
        .viewState =
            ProjectViewState{
                .layout = ProjectViewLayout::kReferenceFocus,
                .differenceEdge = std::array<SourceId, 2U>{0U, 1U},
                .differenceMetric = ProjectDifferenceMetric::kHeatmap,
                .gain = 4U,
            },
    };

    const auto restored = Project::restorePersisted(std::move(persisted));
    ASSERT_TRUE(restored.hasValue());
    ASSERT_TRUE(restored.value().inMark().has_value());
    EXPECT_EQ(*restored.value().inMark(), FrameId{0});
    ASSERT_TRUE(restored.value().outMark().has_value());
    EXPECT_EQ(*restored.value().outMark(), FrameId{1});
    EXPECT_EQ(restored.value().lastDisplayedFrame(), FrameId{0});
    EXPECT_EQ(restored.value().workspaceState().at("inspector-visible"), "true");
    EXPECT_EQ(restored.value().alignmentState().mode, ProjectAlignmentMode::kManualAnchors);
    EXPECT_EQ(restored.value().alignmentState().offsets,
              std::vector<PersistedAlignmentOffset>({
                  PersistedAlignmentOffset{.sourceId = 1U, .frames = 1},
              }));
    EXPECT_EQ(restored.value().viewState().layout, ProjectViewLayout::kReferenceFocus);
    EXPECT_EQ(restored.value().viewState().differenceMetric, ProjectDifferenceMetric::kHeatmap);
    EXPECT_EQ(restored.value().viewState().gain, 4U);

    Project liveProject = makeProject();
    ASSERT_TRUE(liveProject.setInMark(FrameId{0}));
    ASSERT_TRUE(liveProject.setOutMark(FrameId{1}));
    const auto replacement = liveProject.replaceSources(makeSet());
    ASSERT_TRUE(replacement);
    ASSERT_TRUE(replacement.value().inMark().has_value());
    EXPECT_EQ(*replacement.value().inMark(), FrameId{0});
    EXPECT_EQ(*replacement.value().outMark(), FrameId{1});
    EXPECT_EQ(replacement.value().alignmentState(), liveProject.alignmentState());
    EXPECT_EQ(replacement.value().viewState(), liveProject.viewState());
    EXPECT_EQ(liveProject.sources().canonicalFrameCount(), 5);
}

TEST(ProjectTests, RejectsInvalidSourceReplacementWithoutMutatingTheOriginalProject) {
    Project project = makeProject(5);
    ASSERT_TRUE(project.setInMark(FrameId{3}));
    ASSERT_TRUE(project.setOutMark(FrameId{4}));

    const auto rejected = project.replaceSources(makeSet(3));
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, MediaErrorCode::kFrameOutOfRange);
    EXPECT_EQ(project.sources().canonicalFrameCount(), 5);
    ASSERT_TRUE(project.inMark().has_value());
    EXPECT_EQ(*project.inMark(), FrameId{3});
    EXPECT_EQ(*project.outMark(), FrameId{4});
}

TEST(ProjectTests, RejectsInvalidCreationAndSupportsMarkAndWorkspaceMutations) {
    const auto noId = Project::create(ProjectId{""}, "Project", makeSet());
    ASSERT_FALSE(noId.hasValue());
    EXPECT_EQ(noId.error().code, MediaErrorCode::kInvalidArgument);

    const auto noName = Project::create(ProjectId{"project"}, "", makeSet());
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

TEST(ProjectTests, ValidatesAlignmentAndViewStateTransactionally) {
    Project project = makeProject();
    const ProjectAlignmentState validAlignment{
        .mode = ProjectAlignmentMode::kManualAnchors,
        .offsets =
            {
                PersistedAlignmentOffset{.sourceId = 1U, .frames = 1},
            },
        .anchors =
            {
                PersistedAlignmentAnchor{
                    .sourceId = 1U,
                    .canonicalFrame = FrameId{0},
                    .sourceFrame = FrameId{1},
                },
                PersistedAlignmentAnchor{
                    .sourceId = 1U,
                    .canonicalFrame = FrameId{2},
                    .sourceFrame = FrameId{3},
                },
            },
        .analysisCacheKey = "alignment-cache-key",
    };
    ASSERT_TRUE(project.setAlignmentState(validAlignment));
    EXPECT_EQ(project.alignmentState(), validAlignment);

    ProjectAlignmentState crossing = validAlignment;
    crossing.anchors[1].sourceFrame = FrameId{0};
    const auto rejectedAnchors = project.setAlignmentState(std::move(crossing));
    ASSERT_FALSE(rejectedAnchors);
    EXPECT_EQ(rejectedAnchors.error().code, MediaErrorCode::kInvalidArgument);
    EXPECT_EQ(project.alignmentState(), validAlignment);

    ProjectAlignmentState canonicalOffset{
        .mode = ProjectAlignmentMode::kGlobalOffsets,
        .offsets =
            {
                PersistedAlignmentOffset{.sourceId = 0U, .frames = 1},
            },
    };
    const auto rejectedOffset = project.setAlignmentState(std::move(canonicalOffset));
    ASSERT_FALSE(rejectedOffset);
    EXPECT_EQ(project.alignmentState(), validAlignment);

    const ProjectViewState validView{
        .layout = ProjectViewLayout::kDifference,
        .differenceEdge = std::array<SourceId, 2U>{0U, 1U},
        .differenceMetric = ProjectDifferenceMetric::kLuma,
        .gain = 8U,
    };
    ASSERT_TRUE(project.setViewState(validView));
    EXPECT_EQ(project.viewState(), validView);

    ProjectViewState invalidView = validView;
    invalidView.differenceEdge = std::array<SourceId, 2U>{0U, 2U};
    ASSERT_FALSE(project.setViewState(invalidView));
    EXPECT_EQ(project.viewState(), validView);

    invalidView = validView;
    invalidView.gain = 3U;
    ASSERT_FALSE(project.setViewState(invalidView));
    EXPECT_EQ(project.viewState(), validView);
}

} // namespace dvs::domain
