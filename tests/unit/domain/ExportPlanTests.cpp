#include "dvs/domain/ExportPlan.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <limits>
#include <utility>
#include <vector>

namespace dvs::domain {
namespace {

[[nodiscard]] RationalRate makeRate() {
    auto result = RationalRate::create(30, 1);
    EXPECT_TRUE(result.hasValue());
    return std::move(result).value();
}

[[nodiscard]] MediaDescriptor makeDescriptor(const std::filesystem::path& path,
                                             const RationalRate& rate,
                                             const MediaExtent extent) {
    return MediaDescriptor{
        .normalizedPath = path,
        .extent = extent,
        .frameRate = rate,
        .frameCount = FrameCountInfo{.value = 10, .origin = FrameCountOrigin::kReported},
        .duration = MediaTime{333'333},
        .codecId = "h264",
        .pixelFormatId = "yuv420p",
        .bitDepth = 8,
        .decodeCapabilities = DecodeCapabilities{.softwareDecode = true, .d3d11VaDecode = false},
        .timingConfidence = TimingConfidence::kVerifiedCfr,
        .sourceIdentity = std::nullopt,
    };
}
[[nodiscard]] MediaDescriptor makeVfrDescriptor(const std::filesystem::path& path,
                                                const MediaExtent extent) {
    return MediaDescriptor{
        .normalizedPath = path,
        .extent = extent,
        .frameRate = std::nullopt,
        .frameCount = FrameCountInfo{.value = 10, .origin = FrameCountOrigin::kIndexed},
        .duration = MediaTime{333'333},
        .codecId = "h264",
        .pixelFormatId = "yuv420p",
        .bitDepth = 8,
        .decodeCapabilities = DecodeCapabilities{.softwareDecode = true, .d3d11VaDecode = false},
        .timingConfidence = TimingConfidence::kVariableFrameRate,
        .sourceIdentity = std::nullopt,
    };
}

[[nodiscard]] Project makeProject() {
    const RationalRate rate = makeRate();
    auto pair = SourcePairValidator::validate(
        makeDescriptor("a.mp4", rate, MediaExtent{.width = 1'919, .height = 1'081}),
        makeDescriptor("b.mp4", rate, MediaExtent{.width = 1'001, .height = 720}));
    EXPECT_TRUE(pair.hasValue());
    auto project = Project::create(ProjectId{"project-export"}, "Export", std::move(pair).value());
    EXPECT_TRUE(project.hasValue());
    Project value = std::move(project).value();
    EXPECT_TRUE(value.setInMark(FrameId{2}).hasValue());
    EXPECT_TRUE(value.setOutMark(FrameId{4}).hasValue());
    EXPECT_TRUE(value.addClipFromMarks(ClipId{"clip-export"}, "Clip", "").hasValue());
    return value;
}

} // namespace

TEST(ExportPlanTests, BuildsSeparateNativeOutputsWithEvenCanvasPadding) {
    const Project project = makeProject();
    const std::vector<ClipId> selected{ClipId{"clip-export"}};

    const auto plan = ExportPlanBuilder{}.build(project, selected, ExportMode::kSeparateAB);
    ASSERT_TRUE(plan.hasValue());
    ASSERT_EQ(plan.value().clips.size(), 1U);
    const auto& outputs = plan.value().clips.front().outputs;
    ASSERT_EQ(outputs.size(), 2U);
    EXPECT_EQ(outputs[0].canvas.width, 1'920U);
    EXPECT_EQ(outputs[0].canvas.height, 1'082U);
    EXPECT_EQ(outputs[0].operations.front().destination.width, 1'919U);
    EXPECT_EQ(outputs[0].operations.front().frames.first, FrameId{2});
    EXPECT_EQ(outputs[0].operations.front().frames.endExclusive, 5);
    EXPECT_TRUE(outputs[0].operations.front().resetTimestamps);
    EXPECT_EQ(outputs[1].canvas.width, 1'002U);
    EXPECT_EQ(outputs[1].canvas.height, 720U);
}

TEST(ExportPlanTests, CentersSideBySideContentWithoutScaling) {
    const Project project = makeProject();
    const std::vector<ClipId> selected{ClipId{"clip-export"}};

    const auto plan = ExportPlanBuilder{}.build(project, selected, ExportMode::kSideBySide);
    ASSERT_TRUE(plan.hasValue());
    const auto& output = plan.value().clips.front().outputs.front();
    ASSERT_EQ(output.operations.size(), 2U);
    EXPECT_EQ(output.canvas.width, 2'920U);
    EXPECT_EQ(output.canvas.height, 1'082U);
    EXPECT_EQ(output.operations[0].destination.x, 0U);
    EXPECT_EQ(output.operations[0].destination.y, 0U);
    EXPECT_EQ(output.operations[1].destination.x, 1'919U);
    EXPECT_EQ(output.operations[1].destination.y, 181U);
    EXPECT_EQ(output.operations[1].destination.width, 1'001U);
    EXPECT_TRUE(output.operations[0].resetTimestamps);
    EXPECT_TRUE(output.operations[1].resetTimestamps);
}

TEST(ExportPlanTests, RejectsEmptyDuplicateAndUnknownSelections) {
    const Project project = makeProject();
    const std::vector<ClipId> empty;
    const std::vector<ClipId> duplicate{ClipId{"clip-export"}, ClipId{"clip-export"}};
    const std::vector<ClipId> unknown{ClipId{"missing"}};
    const ExportPlanBuilder builder;

    const auto emptyPlan = builder.build(project, empty, ExportMode::kSeparateAB);
    ASSERT_FALSE(emptyPlan.hasValue());
    EXPECT_EQ(emptyPlan.error().code, MediaErrorCode::kInvalidArgument);

    const auto duplicatePlan = builder.build(project, duplicate, ExportMode::kSeparateAB);
    ASSERT_FALSE(duplicatePlan.hasValue());
    EXPECT_EQ(duplicatePlan.error().code, MediaErrorCode::kDuplicateClipSelection);

    const auto unknownPlan = builder.build(project, unknown, ExportMode::kSeparateAB);
    ASSERT_FALSE(unknownPlan.hasValue());
    EXPECT_EQ(unknownPlan.error().code, MediaErrorCode::kClipNotFound);
}

TEST(ExportPlanTests, RejectsImpossibleGeometryAndUnknownMode) {
    const RationalRate rate = makeRate();
    auto widePair = SourcePairValidator::validate(
        makeDescriptor(
            "a.mp4",
            rate,
            MediaExtent{.width = std::numeric_limits<std::uint32_t>::max(), .height = 1}),
        makeDescriptor("b.mp4", rate, MediaExtent{.width = 1, .height = 1}));
    ASSERT_TRUE(widePair.hasValue());
    auto wideProject = Project::create(ProjectId{"wide"}, "Wide", std::move(widePair).value());
    ASSERT_TRUE(wideProject.hasValue());
    ASSERT_TRUE(wideProject.value().setInMark(FrameId{0}).hasValue());
    ASSERT_TRUE(wideProject.value().setOutMark(FrameId{0}).hasValue());
    ASSERT_TRUE(wideProject.value().addClipFromMarks(ClipId{"wide-clip"}, "Wide", "").hasValue());
    const std::vector<ClipId> wideSelection{ClipId{"wide-clip"}};

    const auto geometryFailure =
        ExportPlanBuilder{}.build(wideProject.value(), wideSelection, ExportMode::kSeparateAB);
    ASSERT_FALSE(geometryFailure.hasValue());
    EXPECT_EQ(geometryFailure.error().code, MediaErrorCode::kInvalidExportGeometry);

    const Project normalProject = makeProject();
    const std::vector<ClipId> normalSelection{ClipId{"clip-export"}};
    const auto modeFailure =
        ExportPlanBuilder{}.build(normalProject, normalSelection, static_cast<ExportMode>(99));
    ASSERT_FALSE(modeFailure.hasValue());
    EXPECT_EQ(modeFailure.error().code, MediaErrorCode::kInvalidExportMode);
}

TEST(ExportPlanTests, RejectsVariableFrameRateSources) {
    auto pair = SourcePairValidator::validate(
        makeVfrDescriptor("a.mp4", MediaExtent{.width = 1'919, .height = 1'081}),
        makeVfrDescriptor("b.mp4", MediaExtent{.width = 1'001, .height = 720}));
    ASSERT_TRUE(pair.hasValue());
    auto project = Project::create(ProjectId{"vfr"}, "VFR", std::move(pair).value());
    ASSERT_TRUE(project.hasValue());
    Project value = std::move(project).value();
    ASSERT_TRUE(value.setInMark(FrameId{2}).hasValue());
    ASSERT_TRUE(value.setOutMark(FrameId{4}).hasValue());
    ASSERT_TRUE(value.addClipFromMarks(ClipId{"vfr-clip"}, "VFR", "").hasValue());

    const std::vector<ClipId> selected{ClipId{"vfr-clip"}};
    const auto plan = ExportPlanBuilder{}.build(value, selected, ExportMode::kSeparateAB);
    ASSERT_FALSE(plan.hasValue());
    EXPECT_EQ(plan.error().code, MediaErrorCode::kInvalidCfrTiming);
}

} // namespace dvs::domain
