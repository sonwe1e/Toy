#include "dvs/domain/ComparisonValidator.h"
#include "dvs/persistence/ProjectJson.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace dvs::persistence {
namespace {

[[nodiscard]] domain::RationalRate makeRate() {
    const auto rate = domain::RationalRate::create(30, 1);
    EXPECT_TRUE(rate);
    return rate.value();
}

[[nodiscard]] domain::ComparisonSource
makeSource(const domain::SourceId id,
           const std::filesystem::path& path,
           const std::string& fingerprint,
           const domain::ComparisonRole role,
           const std::string& displayName,
           const domain::FrameCountOrigin countOrigin = domain::FrameCountOrigin::kReported) {
    return domain::ComparisonSource{
        .id = id,
        .role = role,
        .descriptor =
            domain::MediaDescriptor{
                .normalizedPath = path,
                .extent = domain::MediaExtent{.width = 1920, .height = 1080},
                .frameRate = makeRate(),
                .frameCount = domain::FrameCountInfo{.value = 4, .origin = countOrigin},
                .duration = domain::MediaTime{133333},
                .codecId = "h264",
                .pixelFormatId = "yuv420p",
                .bitDepth = 8,
                .rotationDegrees = 90U,
                .sampleAspectRatio = {.numerator = 4U, .denominator = 3U},
                .colorMetadata =
                    domain::ColorMetadata{
                        .matrix = domain::ColorMatrix::kBt709,
                        .range = domain::ColorRange::kFull,
                        .transfer = domain::ColorTransfer::kSrgb,
                        .matrixInferred = false,
                        .transferInferred = false,
                    },
                .decodeCapabilities = {.softwareDecode = true, .d3d11VaDecode = false},
                .timingConfidence = domain::TimingConfidence::kVerifiedCfr,
                .sourceIdentity =
                    domain::SourceFileIdentity{
                        .byteSize = 3,
                        .modifiedUtcMilliseconds = 123456789,
                        .fingerprintSha256 = fingerprint,
                    },
            },
        .displayName = displayName,
    };
}

[[nodiscard]] domain::Project
makeProject(const std::filesystem::path& projectPath,
            const domain::FrameCountOrigin countOrigin = domain::FrameCountOrigin::kReported) {
    std::vector<domain::ComparisonSource> sources;
    sources.push_back(makeSource(0,
                                 projectPath.parent_path() / "source-a.mov",
                                 std::string(64, 'a'),
                                 domain::ComparisonRole::kReference,
                                 "Source A",
                                 countOrigin));
    sources.push_back(makeSource(1,
                                 projectPath.parent_path() / "source-b.mov",
                                 std::string(64, 'b'),
                                 domain::ComparisonRole::kPrediction,
                                 "Source B",
                                 countOrigin));

    const auto validated = domain::ComparisonValidator::validate(std::move(sources));
    EXPECT_TRUE(validated);

    const auto created = domain::Project::create(
        domain::ProjectId{"project-1"}, "Round trip", validated.value().set);
    EXPECT_TRUE(created);
    domain::Project project = created.value();

    EXPECT_TRUE(project.setInMark(domain::FrameId{1}));
    EXPECT_TRUE(project.setOutMark(domain::FrameId{2}));
    EXPECT_TRUE(project.setLastDisplayedFrame(domain::FrameId{2}));
    project.setWorkspaceState({{"comparisonMode", "difference"}, {"zoom", "125"}});
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
        .analysisCacheKey = "alignment-v2-cache-key",
    }));
    EXPECT_TRUE(project.setViewState(domain::ProjectViewState{
        .layout = domain::ProjectViewLayout::kReferenceFocus,
        .differenceEdge = std::array<domain::SourceId, 2U>{0U, 1U},
        .differenceMetric = domain::ProjectDifferenceMetric::kHeatmap,
        .differenceFilter = domain::ProjectDifferenceFilter::kBicubic,
        .gain = 4U,
        .wipePosition = 0.25F,
        .thresholdEnabled = true,
        .threshold = 0.125F,
        .viewport =
            domain::ProjectViewTransform{
                .centerX = 0.4F,
                .centerY = 0.6F,
                .scale = 2.0F,
            },
        .roi =
            domain::ProjectNormalizedRect{
                .left = 0.1F,
                .top = 0.2F,
                .right = 0.9F,
                .bottom = 0.8F,
            },
    }));
    return project;
}

[[nodiscard]] domain::ComparisonSource makeVfrSource(const domain::SourceId id,
                                                     const std::filesystem::path& path,
                                                     const std::string& fingerprint,
                                                     const domain::ComparisonRole role,
                                                     const std::string& displayName,
                                                     const std::int64_t frameCount = 4) {
    return domain::ComparisonSource{
        .id = id,
        .role = role,
        .descriptor =
            domain::MediaDescriptor{
                .normalizedPath = path,
                .extent = domain::MediaExtent{.width = 1920, .height = 1080},
                .frameRate = std::nullopt,
                .frameCount = domain::FrameCountInfo{.value = frameCount,
                                                     .origin = domain::FrameCountOrigin::kIndexed},
                .duration = domain::MediaTime{300000},
                .codecId = "h264",
                .pixelFormatId = "yuv420p",
                .bitDepth = 8,
                .colorMetadata =
                    domain::ColorMetadata{
                        .matrix = domain::ColorMatrix::kBt709,
                        .range = domain::ColorRange::kFull,
                        .matrixInferred = false,
                    },
                .decodeCapabilities = {.softwareDecode = true, .d3d11VaDecode = false},
                .timingConfidence = domain::TimingConfidence::kVariableFrameRate,
                .sourceIdentity =
                    domain::SourceFileIdentity{
                        .byteSize = 5,
                        .modifiedUtcMilliseconds = 123456789,
                        .fingerprintSha256 = fingerprint,
                    },
            },
        .displayName = displayName,
    };
}

[[nodiscard]] domain::Project makeVfrProject(const std::filesystem::path& projectPath) {
    std::vector<domain::ComparisonSource> sources;
    sources.push_back(makeVfrSource(0,
                                    projectPath.parent_path() / "source-a.mov",
                                    std::string(64, 'a'),
                                    domain::ComparisonRole::kReference,
                                    "Source A"));
    sources.push_back(makeVfrSource(1,
                                    projectPath.parent_path() / "source-b.mov",
                                    std::string(64, 'b'),
                                    domain::ComparisonRole::kPrediction,
                                    "Source B"));

    const auto validated = domain::ComparisonValidator::validate(std::move(sources));
    EXPECT_TRUE(validated);

    const auto created = domain::Project::create(
        domain::ProjectId{"vfr-project"}, "VFR project", validated.value().set);
    EXPECT_TRUE(created);
    domain::Project project = created.value();

    EXPECT_TRUE(project.setInMark(domain::FrameId{1}));
    EXPECT_TRUE(project.setOutMark(domain::FrameId{2}));
    EXPECT_TRUE(project.setLastDisplayedFrame(domain::FrameId{2}));
    project.setWorkspaceState({{"comparisonMode", "difference"}});
    return project;
}

TEST(ProjectJsonTests, RoundTripsCompleteSchemaFourDocument) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "roundtrip.dvsproject";
    const domain::Project project = makeProject(projectPath);

    const auto encoded = ProjectJson::encodeText(project, projectPath);
    ASSERT_TRUE(encoded);
    EXPECT_NE(encoded.value().find("\"schemaVersion\": 4"), std::string::npos);
    EXPECT_NE(encoded.value().find("\"mode\": \"manual-anchors\""), std::string::npos);
    EXPECT_NE(encoded.value().find("\"analysisCacheKey\": \"alignment-v2-cache-key\""),
              std::string::npos);
    EXPECT_NE(encoded.value().find("\"layout\": \"reference-focus\""), std::string::npos);

    const auto decoded = ProjectJson::decodeText(encoded.value(), projectPath);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value().id(), project.id());
    EXPECT_EQ(decoded.value().displayName(), project.displayName());
    EXPECT_EQ(decoded.value().sources().canonicalRate(), project.sources().canonicalRate());
    EXPECT_EQ(decoded.value().sources().canonicalFrameCount(),
              project.sources().canonicalFrameCount());

    const auto& decodedSources = decoded.value().sources().sources();
    const auto& projectSources = project.sources().sources();
    ASSERT_EQ(decodedSources.size(), projectSources.size());
    EXPECT_EQ(decodedSources[0].descriptor.normalizedPath,
              projectSources[0].descriptor.normalizedPath);
    EXPECT_EQ(decodedSources[1].descriptor.normalizedPath,
              projectSources[1].descriptor.normalizedPath);
    ASSERT_TRUE(decodedSources[0].descriptor.sourceIdentity.has_value());
    ASSERT_TRUE(projectSources[0].descriptor.sourceIdentity.has_value());
    const auto& decodedIdentity = decodedSources[0].descriptor.sourceIdentity.value();
    const auto& expectedIdentity = projectSources[0].descriptor.sourceIdentity.value();
    EXPECT_EQ(decodedIdentity.byteSize, expectedIdentity.byteSize);
    EXPECT_EQ(decodedIdentity.modifiedUtcMilliseconds, expectedIdentity.modifiedUtcMilliseconds);
    EXPECT_EQ(decodedIdentity.fingerprintSha256, expectedIdentity.fingerprintSha256);
    EXPECT_EQ(decodedSources[0].descriptor.colorMetadata.matrix, domain::ColorMatrix::kBt709);
    EXPECT_EQ(decodedSources[0].descriptor.colorMetadata.range, domain::ColorRange::kFull);
    EXPECT_EQ(decodedSources[0].descriptor.colorMetadata.transfer, domain::ColorTransfer::kSrgb);
    EXPECT_FALSE(decodedSources[0].descriptor.colorMetadata.matrixInferred);
    EXPECT_FALSE(decodedSources[0].descriptor.colorMetadata.transferInferred);
    EXPECT_EQ(decodedSources[0].descriptor.rotationDegrees, 90U);
    EXPECT_EQ(decodedSources[0].descriptor.sampleAspectRatio,
              (domain::SampleAspectRatio{.numerator = 4U, .denominator = 3U}));
    EXPECT_EQ(decoded.value().inMark(), project.inMark());
    EXPECT_EQ(decoded.value().outMark(), project.outMark());
    EXPECT_EQ(decoded.value().lastDisplayedFrame(), project.lastDisplayedFrame());
    EXPECT_EQ(decoded.value().workspaceState(), project.workspaceState());
    EXPECT_EQ(decoded.value().alignmentState(), project.alignmentState());
    EXPECT_EQ(decoded.value().viewState(), project.viewState());
}

TEST(ProjectJsonTests, RoundTripsIndexedFrameCounts) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "indexed-timeline.dvsproject";
    const domain::Project project = makeProject(projectPath, domain::FrameCountOrigin::kIndexed);

    const auto encoded = ProjectJson::encodeText(project, projectPath);
    ASSERT_TRUE(encoded);
    EXPECT_NE(encoded.value().find("\"origin\": \"indexed\""), std::string::npos);

    const auto decoded = ProjectJson::decodeText(encoded.value(), projectPath);
    ASSERT_TRUE(decoded);
    const auto& sources = decoded.value().sources().sources();
    EXPECT_EQ(sources[0].descriptor.frameCount.origin, domain::FrameCountOrigin::kIndexed);
}

TEST(ProjectJsonTests, RelocatesProjectRelativeSourcesButPreservesExternalAbsoluteSources) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "dvs-project-json-relocation";
    const std::filesystem::path originalProjectPath = root / "original" / "project.dvsproj";
    const std::filesystem::path embeddedSourcePath =
        originalProjectPath.parent_path() / "media" / "source-a.mov";
    const std::filesystem::path externalSourcePath = root / "external" / "source-b.mov";

    std::vector<domain::ComparisonSource> sources;
    sources.push_back(makeSource(0,
                                 embeddedSourcePath,
                                 std::string(64, 'a'),
                                 domain::ComparisonRole::kReference,
                                 "Source A"));
    sources.push_back(makeSource(1,
                                 externalSourcePath,
                                 std::string(64, 'b'),
                                 domain::ComparisonRole::kPrediction,
                                 "Source B"));
    const auto validated = domain::ComparisonValidator::validate(std::move(sources));
    ASSERT_TRUE(validated);
    const auto created = domain::Project::create(
        domain::ProjectId{"portable-project"}, "Portable project", validated.value().set);
    ASSERT_TRUE(created);

    const auto encoded = ProjectJson::encodeText(created.value(), originalProjectPath);
    ASSERT_TRUE(encoded);
    EXPECT_NE(encoded.value().find("\"path\": \"media/source-a.mov\""), std::string::npos);

    const std::filesystem::path relocatedProjectPath = root / "relocated" / "project.dvsproj";
    const auto decoded = ProjectJson::decodeText(encoded.value(), relocatedProjectPath);
    ASSERT_TRUE(decoded);
    const auto& decodedSources = decoded.value().sources().sources();
    EXPECT_EQ(decodedSources[0].descriptor.normalizedPath,
              (relocatedProjectPath.parent_path() / "media" / "source-a.mov").lexically_normal());
    EXPECT_EQ(decodedSources[1].descriptor.normalizedPath, externalSourcePath.lexically_normal());
}

TEST(ProjectJsonTests, RejectsMalformedJsonWithStableSchemaError) {
    const auto decoded = ProjectJson::decodeText("{ invalid-json", "project.dvsproject");

    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code, domain::MediaErrorCode::kInvalidProjectSchema);
    EXPECT_FALSE(decoded.error().source.has_value());
}

TEST(ProjectJsonTests, RejectsSchemaVersionOne) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "schema1.dvsproject";
    const auto encoded = ProjectJson::encodeText(makeProject(projectPath), projectPath);
    ASSERT_TRUE(encoded);

    std::string legacyDocument = encoded.value();
    const std::string schemaFour = "\"schemaVersion\": 4";
    const std::size_t schemaPosition = legacyDocument.find(schemaFour);
    ASSERT_NE(schemaPosition, std::string::npos);
    legacyDocument.replace(schemaPosition, schemaFour.size(), "\"schemaVersion\": 1");

    const auto decoded = ProjectJson::decodeText(legacyDocument, projectPath);
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code, domain::MediaErrorCode::kUnsupportedProjectSchema);
    EXPECT_FALSE(decoded.error().source.has_value());
}

TEST(ProjectJsonTests, MigratesSchemaVersionTwoWithDefaultAlignmentAndViewState) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "schema2.dvsproject";
    const auto encoded = ProjectJson::encodeText(makeProject(projectPath), projectPath);
    ASSERT_TRUE(encoded);

    nlohmann::json legacyDocument = nlohmann::json::parse(encoded.value());
    legacyDocument["schemaVersion"] = 2;
    legacyDocument.erase("alignment");
    legacyDocument.erase("view");

    const auto decoded = ProjectJson::decodeText(legacyDocument.dump(), projectPath);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value().alignmentState(), domain::ProjectAlignmentState{});
    EXPECT_EQ(decoded.value().viewState().layout, domain::ProjectViewLayout::kSideBySide);
    EXPECT_EQ(decoded.value().viewState().differenceEdge,
              (std::array<domain::SourceId, 2U>{0U, 1U}));
    EXPECT_EQ(decoded.value().viewState().differenceMetric,
              domain::ProjectDifferenceMetric::kRgbAbsolute);
    EXPECT_EQ(decoded.value().viewState().gain, 1U);
}

TEST(ProjectJsonTests, MigratesSchemaVersionThreeViewDefaultsIntoSchemaFourModel) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "schema3.dvsproject";
    const auto encoded = ProjectJson::encodeText(makeProject(projectPath), projectPath);
    ASSERT_TRUE(encoded);

    nlohmann::json legacyDocument = nlohmann::json::parse(encoded.value());
    legacyDocument["schemaVersion"] = 3;
    legacyDocument["view"].erase("differenceFilter");
    legacyDocument["view"].erase("wipePosition");
    legacyDocument["view"].erase("thresholdEnabled");
    legacyDocument["view"].erase("threshold");
    legacyDocument["view"].erase("viewport");
    legacyDocument["view"].erase("roi");

    const auto decoded = ProjectJson::decodeText(legacyDocument.dump(), projectPath);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value().viewState().layout, domain::ProjectViewLayout::kReferenceFocus);
    EXPECT_EQ(decoded.value().viewState().differenceFilter,
              domain::ProjectDifferenceFilter::kBilinear);
    EXPECT_FLOAT_EQ(decoded.value().viewState().wipePosition, 0.5F);
    EXPECT_EQ(decoded.value().viewState().viewport, domain::ProjectViewTransform{});
    EXPECT_FALSE(decoded.value().viewState().roi.has_value());
}

TEST(ProjectJsonTests, RoundTripsSingleSourceProjectWithoutComparisonEdge) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "single.dvsproject";
    auto validated =
        domain::ComparisonValidator::validate({makeSource(0,
                                                          projectPath.parent_path() / "single.mp4",
                                                          std::string(64, 'c'),
                                                          domain::ComparisonRole::kPrediction,
                                                          "Single")});
    ASSERT_TRUE(validated);
    auto created = domain::Project::create(
        domain::ProjectId{"single-project"}, "Single", std::move(validated).value().set);
    ASSERT_TRUE(created);
    EXPECT_EQ(created.value().viewState().layout, domain::ProjectViewLayout::kSingle);
    EXPECT_FALSE(created.value().viewState().differenceEdge.has_value());

    const auto encoded = ProjectJson::encodeText(created.value(), projectPath);
    ASSERT_TRUE(encoded);
    EXPECT_NE(encoded.value().find("\"layout\": \"single\""), std::string::npos);
    EXPECT_NE(encoded.value().find("\"differenceEdge\": null"), std::string::npos);
    const auto decoded = ProjectJson::decodeText(encoded.value(), projectPath);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value().sources().sourceCount(), 1U);
    EXPECT_EQ(decoded.value().viewState(), created.value().viewState());
}

TEST(ProjectJsonTests, RoundTripsAtomicSingleAndComparisonTopologyTransitions) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "topology.dvsproject";
    auto singleSet =
        domain::ComparisonValidator::validate({makeSource(0,
                                                          projectPath.parent_path() / "single.mp4",
                                                          std::string(64, 'c'),
                                                          domain::ComparisonRole::kPrediction,
                                                          "Single")});
    ASSERT_TRUE(singleSet);
    auto singleProject = domain::Project::create(
        domain::ProjectId{"topology-project"}, "Topology", singleSet.value().set);
    ASSERT_TRUE(singleProject);

    const domain::Project comparisonTemplate = makeProject(projectPath);
    const domain::ProjectViewState comparisonView{
        .layout = domain::ProjectViewLayout::kSideBySide,
        .differenceEdge = std::array<domain::SourceId, 2U>{0U, 1U},
    };
    auto comparison = singleProject.value().replaceReviewState(comparisonTemplate.sources(),
                                                               comparisonView,
                                                               domain::ProjectAlignmentState{},
                                                               domain::FrameId{2});
    ASSERT_TRUE(comparison);
    const auto comparisonJson = ProjectJson::encodeText(comparison.value(), projectPath);
    ASSERT_TRUE(comparisonJson);
    const auto reopenedComparison = ProjectJson::decodeText(comparisonJson.value(), projectPath);
    ASSERT_TRUE(reopenedComparison);
    EXPECT_EQ(reopenedComparison.value().sources().sourceCount(), 2U);
    EXPECT_EQ(reopenedComparison.value().viewState(), comparisonView);

    domain::ProjectViewState singleView;
    singleView.layout = domain::ProjectViewLayout::kSingle;
    singleView.differenceEdge.reset();
    auto singleAgain = reopenedComparison.value().replaceReviewState(
        singleSet.value().set, singleView, domain::ProjectAlignmentState{}, domain::FrameId{1});
    ASSERT_TRUE(singleAgain);
    const auto singleJson = ProjectJson::encodeText(singleAgain.value(), projectPath);
    ASSERT_TRUE(singleJson);
    const auto reopenedSingle = ProjectJson::decodeText(singleJson.value(), projectPath);
    ASSERT_TRUE(reopenedSingle);
    EXPECT_EQ(reopenedSingle.value().sources().sourceCount(), 1U);
    EXPECT_EQ(reopenedSingle.value().viewState(), singleView);
}

TEST(ProjectJsonTests, RejectsFutureSchemaVersionFive) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "schema5.dvsproject";
    const auto encoded = ProjectJson::encodeText(makeProject(projectPath), projectPath);
    ASSERT_TRUE(encoded);

    std::string futureDocument = encoded.value();
    const std::string schemaFour = "\"schemaVersion\": 4";
    const std::size_t schemaPosition = futureDocument.find(schemaFour);
    ASSERT_NE(schemaPosition, std::string::npos);
    futureDocument.replace(schemaPosition, schemaFour.size(), "\"schemaVersion\": 5");

    const auto decoded = ProjectJson::decodeText(futureDocument, projectPath);
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code, domain::MediaErrorCode::kUnsupportedProjectSchema);
    EXPECT_FALSE(decoded.error().source.has_value());
}

TEST(ProjectJsonTests, VfrProjectSerializesNullFrameRateAndRoundTrips) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "vfr.dvsproject";
    const domain::Project project = makeVfrProject(projectPath);

    const auto encoded = ProjectJson::encodeText(project, projectPath);
    ASSERT_TRUE(encoded);

    EXPECT_NE(encoded.value().find("\"schemaVersion\": 4"), std::string::npos);
    EXPECT_NE(encoded.value().find("\"timingConfidence\": \"variable-frame-rate\""),
              std::string::npos);

    // A variable-frame-rate project carries no nominal frame rate. Sample-aspect-ratio metadata
    // has its own numerator/denominator pair and must not be confused with this field.
    EXPECT_NE(encoded.value().find("\"frameRate\": null"), std::string::npos);
    EXPECT_EQ(encoded.value().find("\"frameRate\": {"), std::string::npos);

    const auto decoded = ProjectJson::decodeText(encoded.value(), projectPath);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value().id(), project.id());
    EXPECT_EQ(decoded.value().displayName(), project.displayName());
    // Source descriptors and the canonical timeline round-trip with null optional rates.
    const auto& decodedSources = decoded.value().sources().sources();
    EXPECT_FALSE(decodedSources[0].descriptor.frameRate.has_value());
    EXPECT_FALSE(decodedSources[1].descriptor.frameRate.has_value());
    EXPECT_FALSE(decoded.value().sources().canonicalRate().has_value());
    EXPECT_EQ(decodedSources[0].descriptor.timingConfidence,
              domain::TimingConfidence::kVariableFrameRate);
    EXPECT_EQ(decodedSources[1].descriptor.timingConfidence,
              domain::TimingConfidence::kVariableFrameRate);
    EXPECT_EQ(decoded.value().sources().canonicalFrameCount(),
              project.sources().canonicalFrameCount());
    ASSERT_TRUE(decodedSources[0].descriptor.sourceIdentity.has_value());
    const auto& projectSources = project.sources().sources();
    ASSERT_TRUE(projectSources[0].descriptor.sourceIdentity.has_value());
    EXPECT_EQ(decodedSources[0].descriptor.sourceIdentity->fingerprintSha256,
              projectSources[0].descriptor.sourceIdentity->fingerprintSha256);
    ASSERT_TRUE(decodedSources[1].descriptor.sourceIdentity.has_value());
    EXPECT_TRUE(projectSources[1].descriptor.sourceIdentity.has_value());
    EXPECT_EQ(decodedSources[1].descriptor.sourceIdentity->fingerprintSha256,
              projectSources[1].descriptor.sourceIdentity->fingerprintSha256);
}

TEST(ProjectJsonTests, CfrProjectRoundTripsWithNonNullFrameRate) {
    // The existing RoundTripsCompleteSchemaFourDocument test already exercises a CFR project;
    // this case makes the CFR compatibility assertion explicit and guards the non-null frame
    // rate serialization that the VFR path above deliberately avoids.
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "cfr-compat.dvsproject";
    const domain::Project project = makeProject(projectPath);
    ASSERT_TRUE(project.sources().canonicalRate().has_value());

    const auto encoded = ProjectJson::encodeText(project, projectPath);
    ASSERT_TRUE(encoded);

    EXPECT_NE(encoded.value().find("\"schemaVersion\": 4"), std::string::npos);
    EXPECT_NE(encoded.value().find("\"numerator\": 30"), std::string::npos);
    EXPECT_NE(encoded.value().find("\"denominator\": 1"), std::string::npos);
    EXPECT_NE(encoded.value().find("\"timingConfidence\": \"verified-cfr\""), std::string::npos);

    const auto decoded = ProjectJson::decodeText(encoded.value(), projectPath);
    ASSERT_TRUE(decoded);
    ASSERT_TRUE(decoded.value().sources().canonicalRate().has_value());
    EXPECT_EQ(decoded.value().sources().canonicalRate()->numerator(),
              project.sources().canonicalRate()->numerator());
    EXPECT_EQ(decoded.value().sources().canonicalRate()->denominator(),
              project.sources().canonicalRate()->denominator());
    EXPECT_EQ(decoded.value().sources().canonicalRate(), project.sources().canonicalRate());
    const auto& decodedSources = decoded.value().sources().sources();
    EXPECT_EQ(decodedSources[0].descriptor.timingConfidence,
              domain::TimingConfidence::kVerifiedCfr);
}

} // namespace
} // namespace dvs::persistence
