#include "dvs/persistence/ProjectJson.h"

#include "dvs/domain/ComparisonValidator.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <gtest/gtest.h>
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
        .descriptor = domain::MediaDescriptor{
            .normalizedPath = path,
            .extent = domain::MediaExtent{.width = 1920, .height = 1080},
            .frameRate = makeRate(),
            .frameCount = domain::FrameCountInfo{.value = 4, .origin = countOrigin},
            .duration = domain::MediaTime{133333},
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

[[nodiscard]] domain::Project makeProject(
    const std::filesystem::path& projectPath,
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

    const auto created =
        domain::Project::create(domain::ProjectId{"project-1"}, "Round trip", validated.value().set);
    EXPECT_TRUE(created);
    domain::Project project = created.value();

    EXPECT_TRUE(project.setInMark(domain::FrameId{1}));
    EXPECT_TRUE(project.setOutMark(domain::FrameId{2}));
    EXPECT_TRUE(project.setLastDisplayedFrame(domain::FrameId{2}));
    project.setWorkspaceState({{"comparisonMode", "difference"}, {"zoom", "125"}});
    return project;
}

[[nodiscard]] domain::ComparisonSource
makeVfrSource(const domain::SourceId id,
              const std::filesystem::path& path,
              const std::string& fingerprint,
              const domain::ComparisonRole role,
              const std::string& displayName,
              const std::int64_t frameCount = 4) {
    return domain::ComparisonSource{
        .id = id,
        .role = role,
        .descriptor = domain::MediaDescriptor{
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

[[nodiscard]] domain::Project makeVfrProject(
    const std::filesystem::path& projectPath) {
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

    const auto created =
        domain::Project::create(domain::ProjectId{"vfr-project"}, "VFR project", validated.value().set);
    EXPECT_TRUE(created);
    domain::Project project = created.value();

    EXPECT_TRUE(project.setInMark(domain::FrameId{1}));
    EXPECT_TRUE(project.setOutMark(domain::FrameId{2}));
    EXPECT_TRUE(project.setLastDisplayedFrame(domain::FrameId{2}));
    project.setWorkspaceState({{"comparisonMode", "difference"}});
    return project;
}

TEST(ProjectJsonTests, RoundTripsCompleteSchemaTwoDocument) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "roundtrip.dvsproject";
    const domain::Project project = makeProject(projectPath);

    const auto encoded = ProjectJson::encodeText(project, projectPath);
    ASSERT_TRUE(encoded);
    EXPECT_NE(encoded.value().find("\"schemaVersion\": 2"), std::string::npos);

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
    EXPECT_EQ(decodedSources[0].descriptor.colorMetadata.matrix,
              domain::ColorMatrix::kBt709);
    EXPECT_EQ(decodedSources[0].descriptor.colorMetadata.range, domain::ColorRange::kFull);
    EXPECT_FALSE(decodedSources[0].descriptor.colorMetadata.matrixInferred);
    EXPECT_EQ(decoded.value().inMark(), project.inMark());
    EXPECT_EQ(decoded.value().outMark(), project.outMark());
    EXPECT_EQ(decoded.value().lastDisplayedFrame(), project.lastDisplayedFrame());
    EXPECT_EQ(decoded.value().workspaceState(), project.workspaceState());
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
    EXPECT_EQ(sources[0].descriptor.frameCount.origin,
              domain::FrameCountOrigin::kIndexed);
}

TEST(ProjectJsonTests, RelocatesProjectRelativeSourcesButPreservesExternalAbsoluteSources) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "dvs-project-json-relocation";
    const std::filesystem::path originalProjectPath = root / "original" / "project.dvsproj";
    const std::filesystem::path embeddedSourcePath =
        originalProjectPath.parent_path() / "media" / "source-a.mov";
    const std::filesystem::path externalSourcePath = root / "external" / "source-b.mov";

    std::vector<domain::ComparisonSource> sources;
    sources.push_back(makeSource(0, embeddedSourcePath, std::string(64, 'a'),
                                 domain::ComparisonRole::kReference, "Source A"));
    sources.push_back(makeSource(1, externalSourcePath, std::string(64, 'b'),
                                 domain::ComparisonRole::kPrediction, "Source B"));
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
    EXPECT_EQ(decodedSources[1].descriptor.normalizedPath,
              externalSourcePath.lexically_normal());
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
    const std::string schemaTwo = "\"schemaVersion\": 2";
    const std::size_t schemaPosition = legacyDocument.find(schemaTwo);
    ASSERT_NE(schemaPosition, std::string::npos);
    legacyDocument.replace(schemaPosition, schemaTwo.size(), "\"schemaVersion\": 1");

    const auto decoded = ProjectJson::decodeText(legacyDocument, projectPath);
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code, domain::MediaErrorCode::kUnsupportedProjectSchema);
    EXPECT_FALSE(decoded.error().source.has_value());
}

TEST(ProjectJsonTests, RejectsSchemaVersionThree) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "schema3.dvsproject";
    const auto encoded = ProjectJson::encodeText(makeProject(projectPath), projectPath);
    ASSERT_TRUE(encoded);

    std::string futureDocument = encoded.value();
    const std::string schemaTwo = "\"schemaVersion\": 2";
    const std::size_t schemaPosition = futureDocument.find(schemaTwo);
    ASSERT_NE(schemaPosition, std::string::npos);
    futureDocument.replace(schemaPosition, schemaTwo.size(), "\"schemaVersion\": 3");

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

    EXPECT_NE(encoded.value().find("\"schemaVersion\": 2"), std::string::npos);
    EXPECT_NE(encoded.value().find("\"timingConfidence\": \"variable-frame-rate\""),
              std::string::npos);

    // A variable-frame-rate project carries no nominal rate anywhere, so the document must
    // not emit the numerator/denominator form but serialize every frameRate as JSON null.
    EXPECT_NE(encoded.value().find("\"frameRate\": null"), std::string::npos);
    EXPECT_EQ(encoded.value().find("\"numerator\""), std::string::npos);
    EXPECT_EQ(encoded.value().find("\"denominator\""), std::string::npos);

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
    // The existing RoundTripsCompleteSchemaTwoDocument test already exercises a CFR project;
    // this case makes the CFR compatibility assertion explicit and guards the non-null frame
    // rate serialization that the VFR path above deliberately avoids.
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "cfr-compat.dvsproject";
    const domain::Project project = makeProject(projectPath);
    ASSERT_TRUE(project.sources().canonicalRate().has_value());

    const auto encoded = ProjectJson::encodeText(project, projectPath);
    ASSERT_TRUE(encoded);

    EXPECT_NE(encoded.value().find("\"schemaVersion\": 2"), std::string::npos);
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
