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

TEST(ProjectTests, SupportsOneFrameFinalClipAndRejectsOutOfRangeMarks) {
    Project project = makeProject(1);

    ASSERT_TRUE(project.setInMark(FrameId{0}).hasValue());
    ASSERT_TRUE(project.setOutMark(FrameId{0}).hasValue());
    const auto clip = project.addClipFromMarks(ClipId{"clip-1"}, "Only frame", "");
    ASSERT_TRUE(clip.hasValue());
    ASSERT_EQ(project.clips().size(), 1U);
    EXPECT_EQ(project.clips().front().range.first(), FrameId{0});
    EXPECT_EQ(project.clips().front().range.last(), FrameId{0});

    const auto invalidMark = project.setOutMark(FrameId{1});
    ASSERT_FALSE(invalidMark.hasValue());
    EXPECT_EQ(invalidMark.error().code, MediaErrorCode::kClipOutOfRange);
}

TEST(ProjectTests, LeavesReversedMarksEditableButRejectsClipCreation) {
    Project project = makeProject();

    ASSERT_TRUE(project.setInMark(FrameId{4}).hasValue());
    ASSERT_TRUE(project.setOutMark(FrameId{2}).hasValue());
    const auto reversed = project.addClipFromMarks(ClipId{"clip-1"}, "Reversed", "");
    ASSERT_FALSE(reversed.hasValue());
    EXPECT_EQ(reversed.error().code, MediaErrorCode::kMarksReversed);
    ASSERT_TRUE(project.inMark().has_value());
    ASSERT_TRUE(project.outMark().has_value());
    EXPECT_EQ(*project.inMark(), FrameId{4});
    EXPECT_EQ(*project.outMark(), FrameId{2});

    ASSERT_TRUE(project.setInMark(FrameId{3}).hasValue());
    ASSERT_TRUE(project.setOutMark(FrameId{4}).hasValue());
    ASSERT_TRUE(project.addClipFromMarks(ClipId{"clip-1"}, "Valid", "").hasValue());
    const auto duplicate = project.addClipFromMarks(ClipId{"clip-1"}, "Another", "");
    ASSERT_FALSE(duplicate.hasValue());
    EXPECT_EQ(duplicate.error().code, MediaErrorCode::kDuplicateIdentifier);
}

TEST(ProjectTests, NormalizesPersistedRunningExportsButPreservesLiveReplacementState) {
    const auto range = FrameRange::inclusive(FrameId{0}, FrameId{1});
    ASSERT_TRUE(range.hasValue());
    ProjectState persisted{
        .id = ProjectId{"project-restore"},
        .displayName = "Restored",
        .sources = makePair(),
        .clips = {Clip{.id = ClipId{"clip-1"}, .name = "Clip", .note = "", .range = range.value()}},
        .exportRecords = {ExportRecord{
            .id = ExportRecordId{"export-1"},
            .clipId = ClipId{"clip-1"},
            .state = ExportJobState::kRunning,
            .outputReference = "",
            .error = std::nullopt,
        }},
        .inMark = FrameId{0},
        .outMark = FrameId{1},
        .lastDisplayedFrame = FrameId{0},
        .workspaceState = {},
    };

    const auto restored = Project::restorePersisted(std::move(persisted));
    ASSERT_TRUE(restored.hasValue());
    ASSERT_EQ(restored.value().exportRecords().size(), 1U);
    EXPECT_EQ(restored.value().exportRecords().front().state, ExportJobState::kInterrupted);
    EXPECT_EQ(stableId(ExportJobState::kInterrupted), "interrupted");

    Project liveProject = makeProject();
    ASSERT_TRUE(liveProject.setInMark(FrameId{0}));
    ASSERT_TRUE(liveProject.setOutMark(FrameId{1}));
    ASSERT_TRUE(liveProject.addClipFromMarks(ClipId{"clip-1"}, "Clip", ""));
    ASSERT_TRUE(liveProject.addExportRecord(ExportRecord{
        .id = ExportRecordId{"live-export"},
        .clipId = ClipId{"clip-1"},
        .state = ExportJobState::kRunning,
        .outputReference = "",
        .error = std::nullopt,
    }));
    const auto replacement = liveProject.replaceSources(makePair());
    ASSERT_TRUE(replacement);
    ASSERT_EQ(replacement.value().exportRecords().size(), 1U);
    EXPECT_EQ(replacement.value().exportRecords().front().state, ExportJobState::kRunning);
    EXPECT_EQ(liveProject.exportRecords().front().state, ExportJobState::kRunning);
}

TEST(ProjectTests, RejectsInvalidSourceReplacementWithoutMutatingTheOriginalProject) {
    Project project = makeProject(5);
    ASSERT_TRUE(project.setInMark(FrameId{3}));
    ASSERT_TRUE(project.setOutMark(FrameId{4}));
    ASSERT_TRUE(project.addClipFromMarks(ClipId{"clip-1"}, "Final clip", ""));

    const auto rejected = project.replaceSources(makePair(3));
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, MediaErrorCode::kClipOutOfRange);
    EXPECT_EQ(project.sources().canonicalFrameCount(), 5);
    ASSERT_EQ(project.clips().size(), 1U);
    EXPECT_EQ(project.clips().front().range.first(), FrameId{3});
    EXPECT_EQ(project.clips().front().range.last(), FrameId{4});
}

TEST(ProjectTests, RejectsInvalidCreationAndSupportsClipAndExportMutations) {
    const auto noId = Project::create(ProjectId{""}, "Project", makePair());
    ASSERT_FALSE(noId.hasValue());
    EXPECT_EQ(noId.error().code, MediaErrorCode::kInvalidArgument);

    const auto noName = Project::create(ProjectId{"project"}, "", makePair());
    ASSERT_FALSE(noName.hasValue());
    EXPECT_EQ(noName.error().code, MediaErrorCode::kInvalidArgument);

    Project project = makeProject();
    const auto incomplete = project.addClipFromMarks(ClipId{"clip-1"}, "Clip", "");
    ASSERT_FALSE(incomplete.hasValue());
    EXPECT_EQ(incomplete.error().code, MediaErrorCode::kMarksIncomplete);

    ASSERT_TRUE(project.setInMark(FrameId{0}).hasValue());
    ASSERT_TRUE(project.setOutMark(FrameId{1}).hasValue());
    ASSERT_TRUE(project.addClipFromMarks(ClipId{"clip-1"}, "Clip", "").hasValue());
    const auto range = FrameRange::inclusive(FrameId{1}, FrameId{2});
    ASSERT_TRUE(range.hasValue());

    const auto unknownUpdate = project.updateClip(
        Clip{.id = ClipId{"missing"}, .name = "Missing", .note = "", .range = range.value()});
    ASSERT_FALSE(unknownUpdate.hasValue());
    EXPECT_EQ(unknownUpdate.error().code, MediaErrorCode::kClipNotFound);

    ASSERT_TRUE(
        project
            .updateClip(Clip{
                .id = ClipId{"clip-1"}, .name = "Updated", .note = "Note", .range = range.value()})
            .hasValue());
    EXPECT_EQ(project.clips().front().name, "Updated");

    const auto unknownRemoval = project.removeClip(ClipId{"missing"});
    ASSERT_FALSE(unknownRemoval.hasValue());
    EXPECT_EQ(unknownRemoval.error().code, MediaErrorCode::kClipNotFound);

    const auto invalidRecord = project.addExportRecord(ExportRecord{
        .id = ExportRecordId{""},
        .clipId = ClipId{"clip-1"},
        .state = ExportJobState::kPending,
        .outputReference = "",
        .error = std::nullopt,
    });
    ASSERT_FALSE(invalidRecord.hasValue());
    EXPECT_EQ(invalidRecord.error().code, MediaErrorCode::kInvalidArgument);

    const ExportRecord record{
        .id = ExportRecordId{"export-1"},
        .clipId = ClipId{"clip-1"},
        .state = ExportJobState::kSucceeded,
        .outputReference = "output.mp4",
        .error = std::nullopt,
    };
    ASSERT_TRUE(project.addExportRecord(record).hasValue());
    const auto duplicateRecord = project.addExportRecord(record);
    ASSERT_FALSE(duplicateRecord.hasValue());
    EXPECT_EQ(duplicateRecord.error().code, MediaErrorCode::kDuplicateIdentifier);

    ASSERT_TRUE(project.removeClip(ClipId{"clip-1"}).hasValue());
    EXPECT_TRUE(project.clips().empty());
    project.clearMarks();
    EXPECT_FALSE(project.inMark().has_value());
    EXPECT_FALSE(project.outMark().has_value());

    WorkspaceState workspace{{"inspector-visible", "true"}};
    project.setWorkspaceState(std::move(workspace));
    EXPECT_EQ(project.workspaceState().at("inspector-visible"), "true");
}

} // namespace dvs::domain
