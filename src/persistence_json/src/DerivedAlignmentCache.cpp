#include "dvs/persistence/DerivedAlignmentCache.h"

#include "dvs/application/AlignmentCacheIdentity.h"
#include "dvs/platform/AtomicFilePublisher.h"
#include "dvs/platform/WindowsPaths.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace dvs::persistence {
namespace {

using Json = nlohmann::json;

constexpr std::uint32_t kCacheSchemaVersion = 1U;
constexpr std::uintmax_t kMaximumCacheBytes = 128U * 1024U * 1024U;

[[nodiscard]] domain::MediaError cacheError(std::string detail) {
    return domain::makeMediaError(domain::MediaErrorCode::kInvalidProjectSchema,
                                  domain::MediaOperation::kProjectPersistence,
                                  std::nullopt,
                                  true,
                                  std::move(detail));
}

[[nodiscard]] bool safeCacheKey(const std::string_view key) noexcept {
    if (key.empty() || key.size() > 96U) {
        return false;
    }
    for (const unsigned char character : key) {
        if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
              character == '-')) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string_view matchKindId(const application::FrameMatchKind kind) {
    switch (kind) {
    case application::FrameMatchKind::ExactIndex:
        return "exact-index";
    case application::FrameMatchKind::GlobalOffset:
        return "global-offset";
    case application::FrameMatchKind::AutoAligned:
        return "auto-aligned";
    case application::FrameMatchKind::ManualAnchor:
        return "manual-anchor";
    case application::FrameMatchKind::Missing:
        return "missing";
    }
    return "invalid";
}

[[nodiscard]] std::optional<application::FrameMatchKind>
parseMatchKind(const std::string_view value) noexcept {
    if (value == "exact-index") {
        return application::FrameMatchKind::ExactIndex;
    }
    if (value == "global-offset") {
        return application::FrameMatchKind::GlobalOffset;
    }
    if (value == "auto-aligned") {
        return application::FrameMatchKind::AutoAligned;
    }
    if (value == "manual-anchor") {
        return application::FrameMatchKind::ManualAnchor;
    }
    if (value == "missing") {
        return application::FrameMatchKind::Missing;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view segmentStateId(const application::AlignmentSegmentState state) {
    switch (state) {
    case application::AlignmentSegmentState::Accepted:
        return "accepted";
    case application::AlignmentSegmentState::ReviewRequired:
        return "review-required";
    case application::AlignmentSegmentState::Rejected:
        return "rejected";
    }
    return "invalid";
}

[[nodiscard]] std::optional<application::AlignmentSegmentState>
parseSegmentState(const std::string_view value) noexcept {
    if (value == "accepted") {
        return application::AlignmentSegmentState::Accepted;
    }
    if (value == "review-required") {
        return application::AlignmentSegmentState::ReviewRequired;
    }
    if (value == "rejected") {
        return application::AlignmentSegmentState::Rejected;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view anomalyKindId(const application::SequenceAlignmentAnomalyKind kind) {
    switch (kind) {
    case application::SequenceAlignmentAnomalyKind::TargetFrameMissing:
        return "target-frame-missing";
    case application::SequenceAlignmentAnomalyKind::TargetFrameExtra:
        return "target-frame-extra";
    case application::SequenceAlignmentAnomalyKind::TargetFrameDuplicate:
        return "target-frame-duplicate";
    }
    return "invalid";
}

[[nodiscard]] std::optional<application::SequenceAlignmentAnomalyKind>
parseAnomalyKind(const std::string_view value) noexcept {
    if (value == "target-frame-missing") {
        return application::SequenceAlignmentAnomalyKind::TargetFrameMissing;
    }
    if (value == "target-frame-extra") {
        return application::SequenceAlignmentAnomalyKind::TargetFrameExtra;
    }
    if (value == "target-frame-duplicate") {
        return application::SequenceAlignmentAnomalyKind::TargetFrameDuplicate;
    }
    return std::nullopt;
}

[[nodiscard]] Json encodeSources(const domain::ValidatedComparisonSet& sources) {
    Json document = Json::array();
    for (const domain::ComparisonSource& source : sources.sources()) {
        const domain::SourceFileIdentity& identity = *source.descriptor.sourceIdentity;
        document.push_back({
            {"sourceId", source.id},
            {"frameCount", source.descriptor.frameCount.value},
            {"byteSize", identity.byteSize},
            {"modifiedUtcMilliseconds", identity.modifiedUtcMilliseconds},
            {"fingerprintSha256", identity.fingerprintSha256},
        });
    }
    return document;
}

[[nodiscard]] Json
encodeResults(const std::span<const application::SequenceAlignmentResult> results) {
    Json document = Json::array();
    for (const application::SequenceAlignmentResult& result : results) {
        Json entries = Json::array();
        for (const application::SequenceAlignmentEntry& entry : result.entries) {
            entries.push_back({
                {"sourceFrame",
                 entry.sourceFrameId.has_value() ? Json(entry.sourceFrameId->value())
                                                 : Json(nullptr)},
                {"matchKind", matchKindId(entry.matchKind)},
                {"confidence", entry.confidence},
            });
        }
        Json anomalies = Json::array();
        for (const application::SequenceAlignmentAnomaly& anomaly : result.anomalies) {
            anomalies.push_back({
                {"kind", anomalyKindId(anomaly.kind)},
                {"canonicalFrame",
                 anomaly.canonicalFrameId.has_value() ? Json(anomaly.canonicalFrameId->value())
                                                      : Json(nullptr)},
                {"sourceFrame",
                 anomaly.sourceFrameId.has_value() ? Json(anomaly.sourceFrameId->value())
                                                   : Json(nullptr)},
            });
        }
        Json segments = Json::array();
        for (const application::SequenceAlignmentSegment& segment : result.segments) {
            segments.push_back({
                {"firstCanonicalFrame", segment.firstCanonicalFrame.value()},
                {"lastCanonicalFrame", segment.lastCanonicalFrame.value()},
                {"state", segmentStateId(segment.state)},
                {"meanConfidence", segment.meanConfidence},
                {"p10Confidence", segment.p10Confidence},
                {"maximumLowConfidenceRun", segment.maximumLowConfidenceRun},
                {"anomalyDensity", segment.anomalyDensity},
                {"sceneCutProximity", segment.sceneCutProximity},
                {"mappingSlope", segment.mappingSlope},
            });
        }
        document.push_back({
            {"sourceId", result.sourceId},
            {"entries", std::move(entries)},
            {"anomalies", std::move(anomalies)},
            {"segments", std::move(segments)},
            {"totalCost", result.totalCost},
            {"meanMatchCost", result.meanMatchCost},
            {"confidence", result.confidence},
            {"autoApplicable", result.autoApplicable},
        });
    }
    return document;
}

[[nodiscard]] bool sourceHeaderMatches(const Json& document,
                                       const domain::ValidatedComparisonSet& sources) {
    if (!document.is_array() || document.size() != sources.sourceCount()) {
        return false;
    }
    for (std::size_t index = 0U; index < document.size(); ++index) {
        const domain::ComparisonSource& source = sources.sources()[index];
        if (!source.descriptor.sourceIdentity.has_value()) {
            return false;
        }
        const domain::SourceFileIdentity& identity = *source.descriptor.sourceIdentity;
        const Json& value = document[index];
        if (!value.is_object() ||
            value.value("sourceId", std::numeric_limits<std::uint32_t>::max()) != source.id ||
            value.value("frameCount", std::int64_t{-1}) != source.descriptor.frameCount.value ||
            value.value("byteSize", std::uint64_t{0}) != identity.byteSize ||
            value.value("modifiedUtcMilliseconds", std::int64_t{0}) !=
                identity.modifiedUtcMilliseconds ||
            value.value("fingerprintSha256", std::string{}) != identity.fingerprintSha256) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<domain::FrameId> optionalFrame(const Json& value) {
    if (value.is_null()) {
        return std::nullopt;
    }
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        throw Json::type_error::create(302, "frame must be an integer or null", &value);
    }
    return domain::FrameId{value.get<std::int64_t>()};
}

[[nodiscard]] std::vector<application::SequenceAlignmentResult>
decodeResults(const Json& document) {
    if (!document.is_array()) {
        throw Json::type_error::create(302, "results must be an array", &document);
    }
    std::vector<application::SequenceAlignmentResult> results;
    results.reserve(document.size());
    for (const Json& value : document) {
        application::SequenceAlignmentResult result{
            .sourceId = value.at("sourceId").get<domain::SourceId>(),
            .totalCost = value.at("totalCost").get<float>(),
            .meanMatchCost = value.at("meanMatchCost").get<float>(),
            .confidence = value.at("confidence").get<float>(),
            .autoApplicable = value.at("autoApplicable").get<bool>(),
        };
        const Json& entries = value.at("entries");
        if (!entries.is_array()) {
            throw Json::type_error::create(302, "entries must be an array", &entries);
        }
        result.entries.reserve(entries.size());
        for (std::size_t frame = 0U; frame < entries.size(); ++frame) {
            const Json& entry = entries[frame];
            const auto matchKind =
                parseMatchKind(entry.at("matchKind").get_ref<const std::string&>());
            if (!matchKind.has_value()) {
                throw Json::other_error::create(501, "invalid match kind", &entry);
            }
            result.entries.push_back(application::SequenceAlignmentEntry{
                .canonicalFrameId = domain::FrameId{static_cast<std::int64_t>(frame)},
                .sourceFrameId = optionalFrame(entry.at("sourceFrame")),
                .matchKind = *matchKind,
                .confidence = entry.at("confidence").get<float>(),
            });
        }
        const Json& anomalies = value.at("anomalies");
        if (!anomalies.is_array()) {
            throw Json::type_error::create(302, "anomalies must be an array", &anomalies);
        }
        result.anomalies.reserve(anomalies.size());
        for (const Json& anomaly : anomalies) {
            const auto kind = parseAnomalyKind(anomaly.at("kind").get_ref<const std::string&>());
            if (!kind.has_value()) {
                throw Json::other_error::create(501, "invalid anomaly kind", &anomaly);
            }
            result.anomalies.push_back(application::SequenceAlignmentAnomaly{
                .kind = *kind,
                .canonicalFrameId = optionalFrame(anomaly.at("canonicalFrame")),
                .sourceFrameId = optionalFrame(anomaly.at("sourceFrame")),
            });
        }
        const Json& segments = value.at("segments");
        if (!segments.is_array()) {
            throw Json::type_error::create(302, "segments must be an array", &segments);
        }
        result.segments.reserve(segments.size());
        for (const Json& segment : segments) {
            const auto state = parseSegmentState(segment.at("state").get_ref<const std::string&>());
            if (!state.has_value()) {
                throw Json::other_error::create(501, "invalid segment state", &segment);
            }
            result.segments.push_back(application::SequenceAlignmentSegment{
                .firstCanonicalFrame =
                    domain::FrameId{segment.at("firstCanonicalFrame").get<std::int64_t>()},
                .lastCanonicalFrame =
                    domain::FrameId{segment.at("lastCanonicalFrame").get<std::int64_t>()},
                .state = *state,
                .meanConfidence = segment.at("meanConfidence").get<float>(),
                .p10Confidence = segment.at("p10Confidence").get<float>(),
                .maximumLowConfidenceRun = segment.at("maximumLowConfidenceRun").get<std::size_t>(),
                .anomalyDensity = segment.at("anomalyDensity").get<float>(),
                .sceneCutProximity = segment.at("sceneCutProximity").get<bool>(),
                .mappingSlope = segment.at("mappingSlope").get<float>(),
            });
        }
        results.push_back(std::move(result));
    }
    return results;
}

[[nodiscard]] domain::Result<std::string> readCache(const std::filesystem::path& path) {
    std::error_code errorCode;
    const std::uintmax_t byteCount = std::filesystem::file_size(path, errorCode);
    if (errorCode || byteCount > kMaximumCacheBytes ||
        byteCount > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        return domain::Result<std::string>::failure(
            cacheError("Derived alignment cache is missing or exceeds 128 MiB."));
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return domain::Result<std::string>::failure(
            cacheError("Derived alignment cache could not be opened."));
    }
    std::string text(static_cast<std::size_t>(byteCount), '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (stream.gcount() != static_cast<std::streamsize>(text.size())) {
        return domain::Result<std::string>::failure(
            cacheError("Derived alignment cache could not be read completely."));
    }
    return domain::Result<std::string>::success(std::move(text));
}

} // namespace

DerivedAlignmentCache::DerivedAlignmentCache(std::filesystem::path cacheDirectory)
    : cacheDirectory_(std::move(cacheDirectory)) {}

domain::Status
DerivedAlignmentCache::store(const std::string_view cacheKey,
                             const domain::ValidatedComparisonSet& sources,
                             const std::span<const application::SequenceAlignmentResult> results,
                             const std::uint64_t revision) const {
    if (!safeCacheKey(cacheKey) ||
        !application::validateDerivedSequenceAlignments(sources, results) ||
        application::makeDerivedAlignmentCacheKey(sources, results) != cacheKey) {
        return domain::Status::failure(
            cacheError("Derived alignment cache payload or key is invalid."));
    }
    for (const domain::ComparisonSource& source : sources.sources()) {
        if (!source.descriptor.sourceIdentity.has_value() ||
            !source.descriptor.sourceIdentity->isComplete()) {
            return domain::Status::failure(
                cacheError("Derived alignment cache requires complete source fingerprints."));
        }
    }
    const auto directoryStatus = platform::WindowsPaths::ensureDirectory(cacheDirectory_);
    if (!directoryStatus) {
        return domain::Status::failure(cacheError(directoryStatus.error().technicalDetail));
    }
    const Json document{
        {"schemaVersion", kCacheSchemaVersion},
        {"algorithmVersion", application::kSequenceAlignmentAlgorithmVersion},
        {"cacheKey", cacheKey},
        {"canonicalSourceId", sources.canonicalSourceId()},
        {"sources", encodeSources(sources)},
        {"results", encodeResults(results)},
    };
    const std::string text = document.dump();
    if (text.size() > kMaximumCacheBytes) {
        return domain::Status::failure(
            cacheError("Derived alignment cache exceeds the 128 MiB bound."));
    }
    auto publisher =
        platform::AtomicFilePublisher::begin(cacheDirectory_ / (std::string{cacheKey} + ".json"),
                                             platform::TemporaryFileIdentity{
                                                 .operation = "alignment-cache",
                                                 .ownerId = std::string{cacheKey},
                                                 .revision = revision,
                                             });
    if (!publisher) {
        return domain::Status::failure(cacheError(publisher.error().technicalDetail));
    }
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    auto status = publisher.value()->write(bytes);
    if (!status) {
        return domain::Status::failure(cacheError(status.error().technicalDetail));
    }
    status = publisher.value()->flush();
    if (!status) {
        return domain::Status::failure(cacheError(status.error().technicalDetail));
    }
    std::error_code errorCode;
    const bool exists =
        std::filesystem::exists(cacheDirectory_ / (std::string{cacheKey} + ".json"), errorCode);
    if (errorCode) {
        return domain::Status::failure(
            cacheError("Derived alignment cache destination could not be inspected."));
    }
    status =
        exists ? publisher.value()->publishReplacingExisting() : publisher.value()->publishNew();
    if (!status) {
        return domain::Status::failure(cacheError(status.error().technicalDetail));
    }
    return domain::Status::success();
}

domain::Result<std::vector<application::SequenceAlignmentResult>>
DerivedAlignmentCache::load(const std::string_view cacheKey,
                            const domain::ValidatedComparisonSet& sources) const {
    if (!safeCacheKey(cacheKey)) {
        return domain::Result<std::vector<application::SequenceAlignmentResult>>::failure(
            cacheError("Derived alignment cache key is invalid."));
    }
    auto text = readCache(cacheDirectory_ / (std::string{cacheKey} + ".json"));
    if (!text) {
        return domain::Result<std::vector<application::SequenceAlignmentResult>>::failure(
            text.error());
    }
    try {
        const Json document = Json::parse(text.value());
        if (!document.is_object() ||
            document.at("schemaVersion").get<std::uint32_t>() != kCacheSchemaVersion ||
            document.at("algorithmVersion").get<std::string>() !=
                application::kSequenceAlignmentAlgorithmVersion ||
            document.at("cacheKey").get<std::string>() != cacheKey ||
            document.at("canonicalSourceId").get<domain::SourceId>() !=
                sources.canonicalSourceId() ||
            !sourceHeaderMatches(document.at("sources"), sources)) {
            return domain::Result<std::vector<application::SequenceAlignmentResult>>::failure(
                cacheError("Derived alignment cache identity no longer matches the sources."));
        }
        auto results = decodeResults(document.at("results"));
        if (!application::validateDerivedSequenceAlignments(sources, results) ||
            application::makeDerivedAlignmentCacheKey(sources, results) != cacheKey) {
            return domain::Result<std::vector<application::SequenceAlignmentResult>>::failure(
                cacheError("Derived alignment cache payload failed validation."));
        }
        return domain::Result<std::vector<application::SequenceAlignmentResult>>::success(
            std::move(results));
    } catch (const std::exception& exception) {
        return domain::Result<std::vector<application::SequenceAlignmentResult>>::failure(
            cacheError("Derived alignment cache is malformed: " + std::string{exception.what()}));
    }
}

} // namespace dvs::persistence
