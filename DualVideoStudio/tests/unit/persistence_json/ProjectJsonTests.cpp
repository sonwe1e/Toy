#include "dvs/persistence/ProjectJson.h"

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

[[nodiscard]] domain::MediaDescriptor
makeDescriptor(const std::filesystem::path& path,
               const std::string& fingerprint,
               const domain::FrameCountOrigin countOrigin = domain::FrameCountOrigin::kReported) {
    return domain::MediaDescriptor{
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
    };
}

[[nodiscard]] domain::Project makeProject(
    const std::filesystem::path& projectPath,
    const domain::MediaOperation errorOperation = domain::MediaOperation::kProjectPersistence,
    const domain::MediaErrorCode errorCode = domain::MediaErrorCode::kProjectFileIo,
    const domain::FrameCountOrigin countOrigin = domain::FrameCountOrigin::kReported) {
    const auto sources = domain::SourcePairValidator::validate(
        makeDescriptor(
            projectPath.parent_path() / "source-a.mov", std::string(64, 'a'), countOrigin),
        makeDescriptor(
            projectPath.parent_path() / "source-b.mov", std::string(64, 'b'), countOrigin));
    EXPECT_TRUE(sources);

    const auto created =
        domain::Project::create(domain::ProjectId{"project-1"}, "Round trip", sources.value());
    EXPECT_TRUE(created);
    domain::Project project = created.value();

    EXPECT_TRUE(project.setInMark(domain::FrameId{1}));
    EXPECT_TRUE(project.setOutMark(domain::FrameId{2}));
    EXPECT_TRUE(project.addClipFromMarks(domain::ClipId{"clip-1"}, "Clip", "note"));
    EXPECT_TRUE(project.addExportRecord(domain::ExportRecord{
        .id = domain::ExportRecordId{"export-1"},
        .clipId = domain::ClipId{"clip-1"},
        .state = domain::ExportJobState::kFailed,
        .outputReference = "exports/clip-1.mov",
        .error = domain::makeMediaError(errorCode,
                                        errorOperation,
                                        domain::SourceRole::kExport,
                                        true,
                                        "write failed",
                                        domain::RequestId{91}),
    }));
    EXPECT_TRUE(project.setLastDisplayedFrame(domain::FrameId{2}));
    project.setWorkspaceState({{"comparisonMode", "difference"}, {"zoom", "125"}});
    return project;
}

[[nodiscard]] domain::MediaDescriptor makeVfrDescriptor(const std::filesystem::path& path,
                                                        const std::string& fingerprint,
                                                        const std::int64_t frameCount = 4) {
    return domain::MediaDescriptor{
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
    };
}

[[nodiscard]] domain::Project makeVfrProject(
    const std::filesystem::path& projectPath,
    const domain::MediaOperation errorOperation = domain::MediaOperation::kProjectPersistence,
    const domain::MediaErrorCode errorCode = domain::MediaErrorCode::kProjectFileIo) {
    const auto sources = domain::SourcePairValidator::validate(
        makeVfrDescriptor(projectPath.parent_path() / "source-a.mov", std::string(64, 'a')),
        makeVfrDescriptor(projectPath.parent_path() / "source-b.mov", std::string(64, 'b')));
    EXPECT_TRUE(sources);

    const auto created =
        domain::Project::create(domain::ProjectId{"vfr-project"}, "VFR project", sources.value());
    EXPECT_TRUE(created);
    domain::Project project = created.value();

    EXPECT_TRUE(project.setInMark(domain::FrameId{1}));
    EXPECT_TRUE(project.setOutMark(domain::FrameId{2}));
    EXPECT_TRUE(project.addClipFromMarks(domain::ClipId{"clip-1"}, "Clip", "note"));
    EXPECT_TRUE(project.addExportRecord(domain::ExportRecord{
        .id = domain::ExportRecordId{"export-1"},
        .clipId = domain::ClipId{"clip-1"},
        .state = domain::ExportJobState::kFailed,
        .outputReference = "exports/clip-1.mov",
        .error = domain::makeMediaError(errorCode,
                                        errorOperation,
                                        domain::SourceRole::kExport,
                                        true,
                                        "write failed",
                                        domain::RequestId{91}),
    }));
    EXPECT_TRUE(project.setLastDisplayedFrame(domain::FrameId{2}));
    project.setWorkspaceState({{"comparisonMode", "difference"}});
    return project;
}

// Returns the substring of the JSON object value assigned to `key`, i.e. from its
// opening '{' to the matching closing '}'. Characters inside JSON strings are ignored
// so embedded braces don't upset the depth count.
[[nodiscard]] std::string objectValue(const std::string& document, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const std::size_t keyPosition = document.find(needle);
    if (keyPosition == std::string::npos) {
        return {};
    }
    const std::size_t bracePosition = document.find('{', keyPosition + needle.size());
    if (bracePosition == std::string::npos) {
        return {};
    }
    int depth = 0;
    bool inString = false;
    for (std::size_t index = bracePosition; index < document.size(); ++index) {
        const char glyph = document[index];
        if (inString) {
            if (glyph == '\\') {
                ++index;
            } else if (glyph == '"') {
                inString = false;
            }
            continue;
        }
        if (glyph == '"') {
            inString = true;
        } else if (glyph == '{') {
            ++depth;
        } else if (glyph == '}') {
            if (--depth == 0) {
                return document.substr(bracePosition, index - bracePosition + 1);
            }
        }
    }
    return {};
}

// Collects the top-level member keys of a JSON object substring. A quoted token counts
// as a key only when it sits at depth one and is immediately followed (modulo whitespace)
// by a ':' 鈥?so nested object/array members are excluded.
[[nodiscard]] std::vector<std::string> topLevelKeys(const std::string& document) {
    std::vector<std::string> keys;
    int depth = 0;
    std::size_t index = 0;
    const std::size_t length = document.size();
    while (index < length) {
        const char glyph = document[index];
        if (glyph == '\\') {
            index += 2;
            continue;
        }
        if (glyph == '"') {
            ++index;
            std::string token;
            bool closed = false;
            while (index < length) {
                if (document[index] == '\\') {
                    if (index + 1 < length) {
                        token.push_back(document[index + 1]);
                    }
                    index += 2;
                    continue;
                }
                if (document[index] == '"') {
                    closed = true;
                    ++index;
                    break;
                }
                token.push_back(document[index]);
                ++index;
            }
            if (!closed) {
                break;
            }
            while (index < length && (document[index] == ' ' || document[index] == '\t' ||
                                      document[index] == '\n' || document[index] == '\r')) {
                ++index;
            }
            if (depth == 1 && index < length && document[index] == ':') {
                keys.push_back(std::move(token));
            }
            continue;
        }
        if (glyph == '{' || glyph == '[') {
            ++depth;
        } else if ((glyph == '}' || glyph == ']') && depth > 0) {
            --depth;
        }
        ++index;
    }
    return keys;
}

TEST(ProjectJsonTests, RoundTripsCompleteSchemaOneDocument) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "roundtrip.dvsproject";
    const domain::Project project = makeProject(projectPath);

    const auto encoded = ProjectJson::encodeText(project, projectPath);
    ASSERT_TRUE(encoded);
    EXPECT_NE(encoded.value().find("\"schemaVersion\": 1"), std::string::npos);

    const auto decoded = ProjectJson::decodeText(encoded.value(), projectPath);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value().id(), project.id());
    EXPECT_EQ(decoded.value().displayName(), project.displayName());
    EXPECT_EQ(decoded.value().sources().canonicalRate(), project.sources().canonicalRate());
    EXPECT_EQ(decoded.value().sources().canonicalFrameCount(),
              project.sources().canonicalFrameCount());
    EXPECT_EQ(decoded.value().sources().sourceA().normalizedPath,
              project.sources().sourceA().normalizedPath);
    EXPECT_EQ(decoded.value().sources().sourceB().normalizedPath,
              project.sources().sourceB().normalizedPath);
    ASSERT_TRUE(decoded.value().sources().sourceA().sourceIdentity.has_value());
    ASSERT_TRUE(project.sources().sourceA().sourceIdentity.has_value());
    const auto& decodedIdentity = *decoded.value().sources().sourceA().sourceIdentity;
    const auto& expectedIdentity = *project.sources().sourceA().sourceIdentity;
    EXPECT_EQ(decodedIdentity.byteSize, expectedIdentity.byteSize);
    EXPECT_EQ(decodedIdentity.modifiedUtcMilliseconds, expectedIdentity.modifiedUtcMilliseconds);
    EXPECT_EQ(decodedIdentity.fingerprintSha256, expectedIdentity.fingerprintSha256);
    EXPECT_EQ(decoded.value().sources().sourceA().colorMetadata.matrix,
              domain::ColorMatrix::kBt709);
    EXPECT_EQ(decoded.value().sources().sourceA().colorMetadata.range, domain::ColorRange::kFull);
    EXPECT_FALSE(decoded.value().sources().sourceA().colorMetadata.matrixInferred);
    ASSERT_EQ(decoded.value().clips().size(), 1U);
    EXPECT_EQ(decoded.value().clips().front().id, domain::ClipId{"clip-1"});
    EXPECT_EQ(decoded.value().clips().front().range.first(), domain::FrameId{1});
    EXPECT_EQ(decoded.value().clips().front().range.last(), domain::FrameId{2});
    ASSERT_EQ(decoded.value().exportRecords().size(), 1U);
    EXPECT_EQ(decoded.value().exportRecords().front().state, domain::ExportJobState::kFailed);
    ASSERT_TRUE(decoded.value().exportRecords().front().error.has_value());
    EXPECT_EQ(decoded.value().exportRecords().front().error->requestId, domain::RequestId{91});
    EXPECT_EQ(decoded.value().inMark(), project.inMark());
    EXPECT_EQ(decoded.value().outMark(), project.outMark());
    EXPECT_EQ(decoded.value().lastDisplayedFrame(), project.lastDisplayedFrame());
    EXPECT_EQ(decoded.value().workspaceState(), project.workspaceState());
}

TEST(ProjectJsonTests, RoundTripsMediaProbeErrorOperations) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "media-probe.dvsproject";
    const domain::Project project = makeProject(projectPath, domain::MediaOperation::kMediaProbe);

    const auto encoded = ProjectJson::encodeText(project, projectPath);
    ASSERT_TRUE(encoded);
    EXPECT_NE(encoded.value().find("\"operation\": \"media-probe\""), std::string::npos);

    const auto decoded = ProjectJson::decodeText(encoded.value(), projectPath);
    ASSERT_TRUE(decoded);
    ASSERT_EQ(decoded.value().exportRecords().size(), 1U);
    ASSERT_TRUE(decoded.value().exportRecords().front().error.has_value());
    EXPECT_EQ(decoded.value().exportRecords().front().error->operation,
              domain::MediaOperation::kMediaProbe);
}

TEST(ProjectJsonTests, RoundTripsIndexedFrameCountsAndTimelineErrors) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "indexed-timeline.dvsproject";
    const domain::Project project = makeProject(projectPath,
                                                domain::MediaOperation::kMediaDecode,
                                                domain::MediaErrorCode::kFrameTimelineInvalid,
                                                domain::FrameCountOrigin::kIndexed);

    const auto encoded = ProjectJson::encodeText(project, projectPath);
    ASSERT_TRUE(encoded);
    EXPECT_NE(encoded.value().find("\"origin\": \"indexed\""), std::string::npos);
    EXPECT_NE(encoded.value().find("\"code\": \"frame-timeline-invalid\""), std::string::npos);

    const auto decoded = ProjectJson::decodeText(encoded.value(), projectPath);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value().sources().sourceA().frameCount.origin,
              domain::FrameCountOrigin::kIndexed);
    ASSERT_TRUE(decoded.value().exportRecords().front().error.has_value());
    EXPECT_EQ(decoded.value().exportRecords().front().error->code,
              domain::MediaErrorCode::kFrameTimelineInvalid);
}

TEST(ProjectJsonTests, RelocatesProjectRelativeSourcesButPreservesExternalAbsoluteSources) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "dvs-project-json-relocation";
    const std::filesystem::path originalProjectPath = root / "original" / "project.dvsproj";
    const std::filesystem::path embeddedSourcePath =
        originalProjectPath.parent_path() / "media" / "source-a.mov";
    const std::filesystem::path externalSourcePath = root / "external" / "source-b.mov";
    const auto sources = domain::SourcePairValidator::validate(
        makeDescriptor(embeddedSourcePath, std::string(64, 'a')),
        makeDescriptor(externalSourcePath, std::string(64, 'b')));
    ASSERT_TRUE(sources);
    const auto created = domain::Project::create(
        domain::ProjectId{"portable-project"}, "Portable project", sources.value());
    ASSERT_TRUE(created);

    const auto encoded = ProjectJson::encodeText(created.value(), originalProjectPath);
    ASSERT_TRUE(encoded);
    EXPECT_NE(encoded.value().find("\"path\": \"media/source-a.mov\""), std::string::npos);

    const std::filesystem::path relocatedProjectPath = root / "relocated" / "project.dvsproj";
    const auto decoded = ProjectJson::decodeText(encoded.value(), relocatedProjectPath);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value().sources().sourceA().normalizedPath,
              (relocatedProjectPath.parent_path() / "media" / "source-a.mov").lexically_normal());
    EXPECT_EQ(decoded.value().sources().sourceB().normalizedPath,
              externalSourcePath.lexically_normal());
}

TEST(ProjectJsonTests, RestoresRunningExportsAsInterrupted) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "interrupted.dvsproj";
    domain::Project project = makeProject(projectPath);
    ASSERT_TRUE(project.addExportRecord(domain::ExportRecord{
        .id = domain::ExportRecordId{"export-running"},
        .clipId = domain::ClipId{"clip-1"},
        .state = domain::ExportJobState::kRunning,
        .outputReference = "exports/running.mov",
        .error = std::nullopt,
    }));

    const auto encoded = ProjectJson::encodeText(project, projectPath);
    ASSERT_TRUE(encoded);
    const auto decoded = ProjectJson::decodeText(encoded.value(), projectPath);
    ASSERT_TRUE(decoded);
    ASSERT_EQ(decoded.value().exportRecords().size(), 2U);
    EXPECT_EQ(decoded.value().exportRecords()[1].state, domain::ExportJobState::kInterrupted);
}

TEST(ProjectJsonTests, RejectsMalformedJsonWithStableSchemaError) {
    const auto decoded = ProjectJson::decodeText("{ invalid-json", "project.dvsproject");

    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code, domain::MediaErrorCode::kInvalidProjectSchema);
    EXPECT_EQ(decoded.error().sourceRole, domain::SourceRole::kProject);
}

TEST(ProjectJsonTests, RejectsUnknownSchemaVersion) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "schema.dvsproject";
    const auto encoded = ProjectJson::encodeText(makeProject(projectPath), projectPath);
    ASSERT_TRUE(encoded);

    std::string unsupportedDocument = encoded.value();
    const std::string schemaOne = "\"schemaVersion\": 1";
    const std::size_t schemaPosition = unsupportedDocument.find(schemaOne);
    ASSERT_NE(schemaPosition, std::string::npos);
    unsupportedDocument.replace(schemaPosition, schemaOne.size(), "\"schemaVersion\": 2");

    const auto decoded = ProjectJson::decodeText(unsupportedDocument, projectPath);
    ASSERT_FALSE(decoded);
    EXPECT_EQ(decoded.error().code, domain::MediaErrorCode::kUnsupportedProjectSchema);
    EXPECT_EQ(decoded.error().sourceRole, domain::SourceRole::kProject);
}

TEST(ProjectJsonTests, VfrProjectSerializesNullFrameRateAndRoundTrips) {
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "vfr.dvsproject";
    const domain::Project project = makeVfrProject(projectPath);

    const auto encoded = ProjectJson::encodeText(project, projectPath);
    ASSERT_TRUE(encoded);

    EXPECT_NE(encoded.value().find("\"schemaVersion\": 1"), std::string::npos);
    EXPECT_NE(encoded.value().find("\"timingConfidence\": \"variable-frame-rate\""),
              std::string::npos);

    // A variable-frame-rate project carries no nominal rate anywhere, so the document must
    // not emit the numerator/denominator form but serialize every frameRate as JSON null.
    EXPECT_NE(encoded.value().find("\"frameRate\": null"), std::string::npos);
    EXPECT_EQ(encoded.value().find("\"numerator\""), std::string::npos);
    EXPECT_EQ(encoded.value().find("\"denominator\""), std::string::npos);

    const std::string canonical = objectValue(encoded.value(), "canonicalTimeline");
    ASSERT_FALSE(canonical.empty());
    const auto canonicalKeys = topLevelKeys(canonical);
    // The canonical timeline holds exactly frameRate and frameCount - no per-frame timeline.
    EXPECT_EQ(canonicalKeys.size(), 2U);
    EXPECT_NE(std::find(canonicalKeys.begin(), canonicalKeys.end(), "frameRate"),
              canonicalKeys.end());
    EXPECT_NE(std::find(canonicalKeys.begin(), canonicalKeys.end(), "frameCount"),
              canonicalKeys.end());
    EXPECT_EQ(canonical.find('['), std::string::npos);

    const auto decoded = ProjectJson::decodeText(encoded.value(), projectPath);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value().id(), project.id());
    EXPECT_EQ(decoded.value().displayName(), project.displayName());
    // Source descriptors and the canonical timeline round-trip with null optional rates.
    EXPECT_FALSE(decoded.value().sources().sourceA().frameRate.has_value());
    EXPECT_FALSE(decoded.value().sources().sourceB().frameRate.has_value());
    EXPECT_FALSE(decoded.value().sources().canonicalRate().has_value());
    EXPECT_EQ(decoded.value().sources().sourceA().timingConfidence,
              domain::TimingConfidence::kVariableFrameRate);
    EXPECT_EQ(decoded.value().sources().sourceB().timingConfidence,
              domain::TimingConfidence::kVariableFrameRate);
    EXPECT_EQ(decoded.value().sources().canonicalFrameCount(),
              project.sources().canonicalFrameCount());
    ASSERT_TRUE(decoded.value().sources().sourceA().sourceIdentity.has_value());
    ASSERT_TRUE(project.sources().sourceA().sourceIdentity.has_value());
    EXPECT_EQ(decoded.value().sources().sourceA().sourceIdentity->fingerprintSha256,
              project.sources().sourceA().sourceIdentity->fingerprintSha256);
    ASSERT_TRUE(decoded.value().sources().sourceB().sourceIdentity.has_value());
    EXPECT_TRUE(project.sources().sourceB().sourceIdentity.has_value());
    EXPECT_EQ(decoded.value().sources().sourceB().sourceIdentity->fingerprintSha256,
              project.sources().sourceB().sourceIdentity->fingerprintSha256);
}

TEST(ProjectJsonTests, CfrProjectRoundTripsWithNonNullFrameRate) {
    // The existing RoundTripsCompleteSchemaOneDocument test already exercises a CFR project;
    // this case makes the CFR compatibility assertion explicit and guards the non-null frame
    // rate serialization that the VFR path above deliberately avoids.
    const std::filesystem::path projectPath =
        std::filesystem::temp_directory_path() / "dvs-project-json" / "cfr-compat.dvsproject";
    const domain::Project project = makeProject(projectPath);
    ASSERT_TRUE(project.sources().canonicalRate().has_value());

    const auto encoded = ProjectJson::encodeText(project, projectPath);
    ASSERT_TRUE(encoded);

    EXPECT_NE(encoded.value().find("\"schemaVersion\": 1"), std::string::npos);
    EXPECT_NE(encoded.value().find("\"numerator\": 30"), std::string::npos);
    EXPECT_NE(encoded.value().find("\"denominator\": 1"), std::string::npos);
    EXPECT_NE(encoded.value().find("\"timingConfidence\": \"verified-cfr\""), std::string::npos);

    const std::string canonical = objectValue(encoded.value(), "canonicalTimeline");
    ASSERT_FALSE(canonical.empty());
    // A CFR canonical timeline keeps the exact object form of the frame rate, never null.
    EXPECT_NE(canonical.find("\"frameRate\":"), std::string::npos);
    EXPECT_EQ(canonical.find("\"frameRate\": null"), std::string::npos);
    const auto canonicalKeys = topLevelKeys(canonical);
    EXPECT_EQ(canonicalKeys.size(), 2U);
    EXPECT_NE(std::find(canonicalKeys.begin(), canonicalKeys.end(), "frameRate"),
              canonicalKeys.end());
    EXPECT_NE(std::find(canonicalKeys.begin(), canonicalKeys.end(), "frameCount"),
              canonicalKeys.end());

    const auto decoded = ProjectJson::decodeText(encoded.value(), projectPath);
    ASSERT_TRUE(decoded);
    ASSERT_TRUE(decoded.value().sources().canonicalRate().has_value());
    EXPECT_EQ(decoded.value().sources().canonicalRate()->numerator(),
              project.sources().canonicalRate()->numerator());
    EXPECT_EQ(decoded.value().sources().canonicalRate()->denominator(),
              project.sources().canonicalRate()->denominator());
    EXPECT_EQ(decoded.value().sources().canonicalRate(), project.sources().canonicalRate());
    EXPECT_EQ(decoded.value().sources().sourceA().timingConfidence,
              domain::TimingConfidence::kVerifiedCfr);
}

} // namespace
} // namespace dvs::persistence
