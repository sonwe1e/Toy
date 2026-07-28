#include "dvs/persistence/ProjectJson.h"

#include "dvs/domain/ComparisonValidator.h"

#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace dvs::persistence {
namespace {

using Json = nlohmann::json;

constexpr std::int64_t kSchemaVersion = 2;

[[nodiscard]] domain::MediaError persistenceError(const domain::MediaErrorCode code,
                                                  std::optional<domain::SourceId> sourceId,
                                                  std::string technicalDetail) {
    return domain::makeMediaError(code,
                                  domain::MediaOperation::kProjectPersistence,
                                  sourceId,
                                  false,
                                  std::move(technicalDetail));
}

template <typename TValue>
[[nodiscard]] domain::Result<TValue> invalidSchema(std::optional<domain::SourceId> sourceId,
                                                   std::string technicalDetail) {
    return domain::Result<TValue>::failure(persistenceError(
        domain::MediaErrorCode::kInvalidProjectSchema, sourceId, std::move(technicalDetail)));
}

[[nodiscard]] domain::Result<const Json*> requiredMember(const Json& object,
                                                         const std::string_view field,
                                                         std::optional<domain::SourceId> sourceId) {
    if (!object.is_object()) {
        return invalidSchema<const Json*>(sourceId, "Expected a JSON object.");
    }

    const auto iterator = object.find(std::string{field});
    if (iterator == object.end()) {
        return invalidSchema<const Json*>(sourceId,
                                          "Missing required field: " + std::string{field} + ".");
    }
    return domain::Result<const Json*>::success(std::addressof(*iterator));
}

[[nodiscard]] domain::Result<const Json*> objectMember(const Json& object,
                                                       const std::string_view field,
                                                       std::optional<domain::SourceId> sourceId) {
    auto member = requiredMember(object, field, sourceId);
    if (!member) {
        return domain::Result<const Json*>::failure(member.error());
    }
    if (!member.value()->is_object()) {
        return invalidSchema<const Json*>(
            sourceId, "Field must be a JSON object: " + std::string{field} + ".");
    }
    return member;
}

[[nodiscard]] domain::Result<const Json*>
arrayMember(const Json& object, const std::string_view field, std::optional<domain::SourceId> sourceId) {
    auto member = requiredMember(object, field, sourceId);
    if (!member) {
        return domain::Result<const Json*>::failure(member.error());
    }
    if (!member.value()->is_array()) {
        return invalidSchema<const Json*>(
            sourceId, "Field must be a JSON array: " + std::string{field} + ".");
    }
    return member;
}

[[nodiscard]] domain::Result<std::string> stringMember(const Json& object,
                                                       const std::string_view field,
                                                       std::optional<domain::SourceId> sourceId) {
    auto member = requiredMember(object, field, sourceId);
    if (!member) {
        return domain::Result<std::string>::failure(member.error());
    }
    if (!member.value()->is_string()) {
        return invalidSchema<std::string>(sourceId,
                                          "Field must be a string: " + std::string{field} + ".");
    }
    return domain::Result<std::string>::success(member.value()->get<std::string>());
}

[[nodiscard]] domain::Result<bool>
boolMember(const Json& object, const std::string_view field, std::optional<domain::SourceId> sourceId) {
    auto member = requiredMember(object, field, sourceId);
    if (!member) {
        return domain::Result<bool>::failure(member.error());
    }
    if (!member.value()->is_boolean()) {
        return invalidSchema<bool>(sourceId,
                                   "Field must be a boolean: " + std::string{field} + ".");
    }
    return domain::Result<bool>::success(member.value()->get<bool>());
}

[[nodiscard]] domain::Result<std::int64_t>
int64Value(const Json& value, std::optional<domain::SourceId> sourceId, const std::string_view field) {
    if (value.is_number_unsigned()) {
        const std::uint64_t unsignedValue = value.get<std::uint64_t>();
        if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return invalidSchema<std::int64_t>(
                sourceId, "Integer value exceeds int64 range: " + std::string{field} + ".");
        }
        return domain::Result<std::int64_t>::success(static_cast<std::int64_t>(unsignedValue));
    }
    if (value.is_number_integer()) {
        return domain::Result<std::int64_t>::success(value.get<std::int64_t>());
    }
    return invalidSchema<std::int64_t>(sourceId,
                                       "Field must be an integer: " + std::string{field} + ".");
}

[[nodiscard]] domain::Result<std::uint64_t>
uint64Value(const Json& value, std::optional<domain::SourceId> sourceId, const std::string_view field) {
    if (value.is_number_unsigned()) {
        return domain::Result<std::uint64_t>::success(value.get<std::uint64_t>());
    }
    if (value.is_number_integer()) {
        const std::int64_t signedValue = value.get<std::int64_t>();
        if (signedValue >= 0) {
            return domain::Result<std::uint64_t>::success(static_cast<std::uint64_t>(signedValue));
        }
    }
    return invalidSchema<std::uint64_t>(
        sourceId, "Field must be an unsigned integer: " + std::string{field} + ".");
}

[[nodiscard]] domain::Result<std::int64_t>
int64Member(const Json& object, const std::string_view field, std::optional<domain::SourceId> sourceId) {
    auto member = requiredMember(object, field, sourceId);
    if (!member) {
        return domain::Result<std::int64_t>::failure(member.error());
    }
    return int64Value(*member.value(), sourceId, field);
}

[[nodiscard]] domain::Result<std::uint64_t> uint64Member(const Json& object,
                                                         const std::string_view field,
                                                         std::optional<domain::SourceId> sourceId) {
    auto member = requiredMember(object, field, sourceId);
    if (!member) {
        return domain::Result<std::uint64_t>::failure(member.error());
    }
    return uint64Value(*member.value(), sourceId, field);
}

[[nodiscard]] domain::Result<std::uint32_t> uint32Member(const Json& object,
                                                         const std::string_view field,
                                                         std::optional<domain::SourceId> sourceId) {
    auto value = uint64Member(object, field, sourceId);
    if (!value) {
        return domain::Result<std::uint32_t>::failure(value.error());
    }
    if (value.value() > std::numeric_limits<std::uint32_t>::max()) {
        return invalidSchema<std::uint32_t>(
            sourceId, "Integer value exceeds uint32 range: " + std::string{field} + ".");
    }
    return domain::Result<std::uint32_t>::success(static_cast<std::uint32_t>(value.value()));
}

[[nodiscard]] domain::Result<std::uint8_t>
uint8Member(const Json& object, const std::string_view field, std::optional<domain::SourceId> sourceId) {
    auto value = uint64Member(object, field, sourceId);
    if (!value) {
        return domain::Result<std::uint8_t>::failure(value.error());
    }
    if (value.value() > std::numeric_limits<std::uint8_t>::max()) {
        return invalidSchema<std::uint8_t>(
            sourceId, "Integer value exceeds uint8 range: " + std::string{field} + ".");
    }
    return domain::Result<std::uint8_t>::success(static_cast<std::uint8_t>(value.value()));
}

[[nodiscard]] domain::Result<std::filesystem::path>
projectDirectoryFor(const std::filesystem::path& projectPath) {
    if (projectPath.empty()) {
        return invalidSchema<std::filesystem::path>(std::nullopt,
                                                    "Project JSON requires a destination path.");
    }

    std::error_code errorCode;
    const std::filesystem::path absoluteProjectPath =
        std::filesystem::absolute(projectPath, errorCode);
    if (errorCode) {
        return invalidSchema<std::filesystem::path>(
            std::nullopt, "Could not resolve the project destination path.");
    }

    const std::filesystem::path directory = absoluteProjectPath.parent_path();
    if (directory.empty()) {
        return invalidSchema<std::filesystem::path>(std::nullopt,
                                                    "Project destination has no parent directory.");
    }
    return domain::Result<std::filesystem::path>::success(directory.lexically_normal());
}

[[nodiscard]] bool hasParentReference(const std::filesystem::path& path) noexcept {
    for (const std::filesystem::path& component : path) {
        if (component == std::filesystem::path{".."}) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] domain::Result<std::filesystem::path>
normalizeSourcePath(const std::filesystem::path& sourcePath,
                    const std::filesystem::path& projectDirectory,
                    std::optional<domain::SourceId> sourceId) {
    if (sourcePath.empty()) {
        return invalidSchema<std::filesystem::path>(sourceId, "Source path is empty.");
    }
    if (sourcePath.is_relative()) {
        return domain::Result<std::filesystem::path>::success(
            (projectDirectory / sourcePath).lexically_normal());
    }

    std::error_code errorCode;
    const std::filesystem::path absoluteSourcePath =
        std::filesystem::absolute(sourcePath, errorCode);
    if (errorCode) {
        return invalidSchema<std::filesystem::path>(sourceId, "Could not resolve source path.");
    }
    return domain::Result<std::filesystem::path>::success(absoluteSourcePath.lexically_normal());
}

[[nodiscard]] domain::Result<std::string>
pathForDocument(const std::filesystem::path& sourcePath,
                const std::filesystem::path& projectDirectory,
                std::optional<domain::SourceId> sourceId) {
    auto normalizedSourcePath = normalizeSourcePath(sourcePath, projectDirectory, sourceId);
    if (!normalizedSourcePath) {
        return domain::Result<std::string>::failure(normalizedSourcePath.error());
    }

    const std::filesystem::path relativePath =
        normalizedSourcePath.value().lexically_relative(projectDirectory);
    if (!relativePath.empty() && !relativePath.is_absolute() && !hasParentReference(relativePath)) {
        return domain::Result<std::string>::success(relativePath.generic_string());
    }
    return domain::Result<std::string>::success(normalizedSourcePath.value().generic_string());
}

[[nodiscard]] domain::Result<std::filesystem::path>
pathFromDocument(const std::string_view storedPath,
                 const std::filesystem::path& projectDirectory,
                 std::optional<domain::SourceId> sourceId) {
    if (storedPath.empty() || storedPath.find('\0') != std::string_view::npos) {
        return invalidSchema<std::filesystem::path>(sourceId,
                                                    "Persisted source path is invalid.");
    }

    const std::filesystem::path path{std::string{storedPath}};
    if (path.is_relative() && hasParentReference(path)) {
        return invalidSchema<std::filesystem::path>(
            sourceId, "Relative source paths may not escape the project directory.");
    }
    return normalizeSourcePath(path, projectDirectory, sourceId);
}

[[nodiscard]] std::optional<domain::FrameCountOrigin>
frameCountOriginFromId(const std::string_view identifier) noexcept {
    if (identifier == "reported") {
        return domain::FrameCountOrigin::kReported;
    }
    if (identifier == "estimated") {
        return domain::FrameCountOrigin::kEstimated;
    }
    if (identifier == "indexed") {
        return domain::FrameCountOrigin::kIndexed;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view frameCountOriginId(const domain::FrameCountOrigin origin) noexcept {
    switch (origin) {
    case domain::FrameCountOrigin::kReported:
        return "reported";
    case domain::FrameCountOrigin::kEstimated:
        return "estimated";
    case domain::FrameCountOrigin::kIndexed:
        return "indexed";
    }
    return "unknown";
}

[[nodiscard]] std::optional<domain::TimingConfidence>
timingConfidenceFromId(const std::string_view identifier) noexcept {
    if (identifier == "declared-cfr") {
        return domain::TimingConfidence::kDeclaredCfr;
    }
    if (identifier == "verified-cfr") {
        return domain::TimingConfidence::kVerifiedCfr;
    }
    if (identifier == "variable-frame-rate") {
        return domain::TimingConfidence::kVariableFrameRate;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view
timingConfidenceId(const domain::TimingConfidence confidence) noexcept {
    switch (confidence) {
    case domain::TimingConfidence::kDeclaredCfr:
        return "declared-cfr";
    case domain::TimingConfidence::kVerifiedCfr:
        return "verified-cfr";
    case domain::TimingConfidence::kVariableFrameRate:
        return "variable-frame-rate";
    }
    return "unknown";
}

[[nodiscard]] std::optional<domain::ColorMatrix>
colorMatrixFromId(const std::string_view identifier) noexcept {
    if (identifier == "bt601") {
        return domain::ColorMatrix::kBt601;
    }
    if (identifier == "bt709") {
        return domain::ColorMatrix::kBt709;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view colorMatrixId(const domain::ColorMatrix matrix) noexcept {
    switch (matrix) {
    case domain::ColorMatrix::kBt601:
        return "bt601";
    case domain::ColorMatrix::kBt709:
        return "bt709";
    }
    return "unknown";
}

[[nodiscard]] std::optional<domain::ColorRange>
colorRangeFromId(const std::string_view identifier) noexcept {
    if (identifier == "limited") {
        return domain::ColorRange::kLimited;
    }
    if (identifier == "full") {
        return domain::ColorRange::kFull;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view colorRangeId(const domain::ColorRange range) noexcept {
    switch (range) {
    case domain::ColorRange::kLimited:
        return "limited";
    case domain::ColorRange::kFull:
        return "full";
    }
    return "unknown";
}

[[nodiscard]] std::optional<domain::MediaErrorCode>
mediaErrorCodeFromId(const std::string_view identifier) noexcept {
    using Code = domain::MediaErrorCode;
    if (identifier == "invalid-argument") {
        return Code::kInvalidArgument;
    }
    if (identifier == "invalid-rate") {
        return Code::kInvalidRate;
    }
    if (identifier == "invalid-frame-id") {
        return Code::kInvalidFrameId;
    }
    if (identifier == "invalid-frame-count") {
        return Code::kInvalidFrameCount;
    }
    if (identifier == "invalid-dimensions") {
        return Code::kInvalidDimensions;
    }
    if (identifier == "invalid-duration") {
        return Code::kInvalidDuration;
    }
    if (identifier == "invalid-media-descriptor") {
        return Code::kInvalidMediaDescriptor;
    }
    if (identifier == "arithmetic-overflow") {
        return Code::kArithmeticOverflow;
    }
    if (identifier == "source-frame-rate-mismatch") {
        return Code::kSourceFrameRateMismatch;
    }
    if (identifier == "source-frame-count-mismatch") {
        return Code::kSourceFrameCountMismatch;
    }
    if (identifier == "source-duration-mismatch") {
        return Code::kSourceDurationMismatch;
    }
    if (identifier == "unsupported-project-schema") {
        return Code::kUnsupportedProjectSchema;
    }
    if (identifier == "invalid-project-schema") {
        return Code::kInvalidProjectSchema;
    }
    if (identifier == "source-missing") {
        return Code::kSourceMissing;
    }
    if (identifier == "source-fingerprint-mismatch") {
        return Code::kSourceFingerprintMismatch;
    }
    if (identifier == "project-file-io") {
        return Code::kProjectFileIo;
    }
    if (identifier == "media-open-failed") {
        return Code::kMediaOpenFailed;
    }
    if (identifier == "media-probe-failed") {
        return Code::kMediaProbeFailed;
    }
    if (identifier == "invalid-cfr-timing") {
        return Code::kInvalidCfrTiming;
    }
    if (identifier == "unsupported-codec") {
        return Code::kUnsupportedCodec;
    }
    if (identifier == "unsupported-pixel-format") {
        return Code::kUnsupportedPixelFormat;
    }
    if (identifier == "media-decode-failed") {
        return Code::kMediaDecodeFailed;
    }
    if (identifier == "frame-timeline-invalid") {
        return Code::kFrameTimelineInvalid;
    }
    if (identifier == "frame-budget-exceeded") {
        return Code::kFrameBudgetExceeded;
    }
    if (identifier == "frame-out-of-range") {
        return Code::kFrameOutOfRange;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<domain::MediaOperation>
mediaOperationFromId(const std::string_view identifier) noexcept {
    using Operation = domain::MediaOperation;
    if (identifier == "rational-conversion") {
        return Operation::kRationalConversion;
    }
    if (identifier == "media-descriptor-validation") {
        return Operation::kMediaDescriptorValidation;
    }
    if (identifier == "source-pair-validation") {
        return Operation::kSourcePairValidation;
    }
    if (identifier == "project-mutation") {
        return Operation::kProjectMutation;
    }
    if (identifier == "project-persistence") {
        return Operation::kProjectPersistence;
    }
    if (identifier == "media-probe") {
        return Operation::kMediaProbe;
    }
    if (identifier == "media-decode") {
        return Operation::kMediaDecode;
    }
    return std::nullopt;
}

[[nodiscard]] Json encodeRate(const domain::RationalRate& rate) {
    return Json{
        {"numerator", rate.numerator()},
        {"denominator", rate.denominator()},
    };
}

[[nodiscard]] domain::Result<domain::RationalRate> decodeRate(const Json& document,
                                                              std::optional<domain::SourceId> sourceId) {
    auto numerator = int64Member(document, "numerator", sourceId);
    if (!numerator) {
        return domain::Result<domain::RationalRate>::failure(numerator.error());
    }
    auto denominator = int64Member(document, "denominator", sourceId);
    if (!denominator) {
        return domain::Result<domain::RationalRate>::failure(denominator.error());
    }

    auto rate = domain::RationalRate::create(numerator.value(), denominator.value());
    if (!rate) {
        return invalidSchema<domain::RationalRate>(
            sourceId, "Frame-rate numerator and denominator must form a positive rational rate.");
    }
    return rate;
}

[[nodiscard]] Json encodeOptionalRate(const std::optional<domain::RationalRate>& rate) {
    if (!rate.has_value()) {
        return Json(nullptr);
    }
    return encodeRate(*rate);
}

[[nodiscard]] domain::Result<std::optional<domain::RationalRate>> decodeOptionalRate(
    const Json& document, const std::string_view field, std::optional<domain::SourceId> sourceId) {
    auto member = requiredMember(document, field, sourceId);
    if (!member) {
        return domain::Result<std::optional<domain::RationalRate>>::failure(member.error());
    }
    if (member.value()->is_null()) {
        return domain::Result<std::optional<domain::RationalRate>>::success(std::nullopt);
    }
    auto rate = decodeRate(*member.value(), sourceId);
    if (!rate) {
        return domain::Result<std::optional<domain::RationalRate>>::failure(rate.error());
    }
    return domain::Result<std::optional<domain::RationalRate>>::success(
        std::optional<domain::RationalRate>{std::move(rate).value()});
}

[[nodiscard]] Json encodeDescriptor(const domain::MediaDescriptor& descriptor) {
    return Json{
        {"extent",
         Json{
             {"width", descriptor.extent.width},
             {"height", descriptor.extent.height},
         }},
        {"frameRate", encodeOptionalRate(descriptor.frameRate)},
        {"frameCount",
         Json{
             {"value", descriptor.frameCount.value},
             {"origin", std::string{frameCountOriginId(descriptor.frameCount.origin)}},
         }},
        {"durationMicroseconds", descriptor.duration.microseconds()},
        {"codecId", descriptor.codecId},
        {"pixelFormatId", descriptor.pixelFormatId},
        {"bitDepth", static_cast<std::uint32_t>(descriptor.bitDepth)},
        {"color",
         Json{
             {"matrix", std::string{colorMatrixId(descriptor.colorMetadata.matrix)}},
             {"range", std::string{colorRangeId(descriptor.colorMetadata.range)}},
             {"matrixInferred", descriptor.colorMetadata.matrixInferred},
         }},
        {"decodeCapabilities",
         Json{
             {"softwareDecode", descriptor.decodeCapabilities.softwareDecode},
             {"d3d11VaDecode", descriptor.decodeCapabilities.d3d11VaDecode},
         }},
        {"timingConfidence", std::string{timingConfidenceId(descriptor.timingConfidence)}},
    };
}

[[nodiscard]] domain::Result<domain::SourceFileIdentity>
decodeSourceIdentity(const Json& document, std::optional<domain::SourceId> sourceId) {
    auto byteSize = uint64Member(document, "byteSize", sourceId);
    if (!byteSize) {
        return domain::Result<domain::SourceFileIdentity>::failure(byteSize.error());
    }
    auto modifiedUtcMilliseconds = int64Member(document, "modifiedUtcMilliseconds", sourceId);
    if (!modifiedUtcMilliseconds) {
        return domain::Result<domain::SourceFileIdentity>::failure(modifiedUtcMilliseconds.error());
    }
    auto fingerprint = stringMember(document, "fingerprintSha256", sourceId);
    if (!fingerprint) {
        return domain::Result<domain::SourceFileIdentity>::failure(fingerprint.error());
    }

    domain::SourceFileIdentity identity{
        .byteSize = byteSize.value(),
        .modifiedUtcMilliseconds = modifiedUtcMilliseconds.value(),
        .fingerprintSha256 = std::move(fingerprint).value(),
    };
    if (!identity.isComplete()) {
        return invalidSchema<domain::SourceFileIdentity>(
            sourceId,
            "Source identity must contain a non-zero size and a 64-character SHA-256 value.");
    }
    return domain::Result<domain::SourceFileIdentity>::success(std::move(identity));
}

[[nodiscard]] domain::Result<domain::MediaDescriptor>
decodeSource(const Json& document,
             const std::filesystem::path& projectDirectory,
             std::optional<domain::SourceId> sourceId) {
    if (!document.is_object()) {
        return invalidSchema<domain::MediaDescriptor>(sourceId,
                                                      "Source entry must be an object.");
    }

    auto storedPath = stringMember(document, "path", sourceId);
    if (!storedPath) {
        return domain::Result<domain::MediaDescriptor>::failure(storedPath.error());
    }
    auto normalizedPath = pathFromDocument(storedPath.value(), projectDirectory, sourceId);
    if (!normalizedPath) {
        return domain::Result<domain::MediaDescriptor>::failure(normalizedPath.error());
    }

    auto identityDocument = objectMember(document, "identity", sourceId);
    if (!identityDocument) {
        return domain::Result<domain::MediaDescriptor>::failure(identityDocument.error());
    }
    auto identity = decodeSourceIdentity(*identityDocument.value(), sourceId);
    if (!identity) {
        return domain::Result<domain::MediaDescriptor>::failure(identity.error());
    }

    auto descriptorDocument = objectMember(document, "descriptor", sourceId);
    if (!descriptorDocument) {
        return domain::Result<domain::MediaDescriptor>::failure(descriptorDocument.error());
    }
    const Json& descriptor = *descriptorDocument.value();

    auto extentDocument = objectMember(descriptor, "extent", sourceId);
    if (!extentDocument) {
        return domain::Result<domain::MediaDescriptor>::failure(extentDocument.error());
    }
    auto width = uint32Member(*extentDocument.value(), "width", sourceId);
    if (!width) {
        return domain::Result<domain::MediaDescriptor>::failure(width.error());
    }
    auto height = uint32Member(*extentDocument.value(), "height", sourceId);
    if (!height) {
        return domain::Result<domain::MediaDescriptor>::failure(height.error());
    }

    auto frameRate = decodeOptionalRate(descriptor, "frameRate", sourceId);
    if (!frameRate) {
        return domain::Result<domain::MediaDescriptor>::failure(frameRate.error());
    }

    auto frameCountDocument = objectMember(descriptor, "frameCount", sourceId);
    if (!frameCountDocument) {
        return domain::Result<domain::MediaDescriptor>::failure(frameCountDocument.error());
    }
    auto frameCount = int64Member(*frameCountDocument.value(), "value", sourceId);
    if (!frameCount) {
        return domain::Result<domain::MediaDescriptor>::failure(frameCount.error());
    }
    auto frameCountOriginIdValue = stringMember(*frameCountDocument.value(), "origin", sourceId);
    if (!frameCountOriginIdValue) {
        return domain::Result<domain::MediaDescriptor>::failure(frameCountOriginIdValue.error());
    }
    const auto frameCountOrigin = frameCountOriginFromId(frameCountOriginIdValue.value());
    if (!frameCountOrigin.has_value()) {
        return invalidSchema<domain::MediaDescriptor>(sourceId, "Frame-count origin is unknown.");
    }

    auto duration = int64Member(descriptor, "durationMicroseconds", sourceId);
    if (!duration) {
        return domain::Result<domain::MediaDescriptor>::failure(duration.error());
    }
    auto codecId = stringMember(descriptor, "codecId", sourceId);
    if (!codecId) {
        return domain::Result<domain::MediaDescriptor>::failure(codecId.error());
    }
    auto pixelFormatId = stringMember(descriptor, "pixelFormatId", sourceId);
    if (!pixelFormatId) {
        return domain::Result<domain::MediaDescriptor>::failure(pixelFormatId.error());
    }
    auto bitDepth = uint8Member(descriptor, "bitDepth", sourceId);
    if (!bitDepth) {
        return domain::Result<domain::MediaDescriptor>::failure(bitDepth.error());
    }

    domain::ColorMetadata colorMetadata{
        .matrix =
            height.value() >= 720U ? domain::ColorMatrix::kBt709 : domain::ColorMatrix::kBt601,
        .range = domain::ColorRange::kLimited,
        .matrixInferred = true,
    };
    if (const auto colorIterator = descriptor.find("color"); colorIterator != descriptor.end()) {
        if (!colorIterator->is_object()) {
            return invalidSchema<domain::MediaDescriptor>(sourceId,
                                                          "Color metadata must be an object.");
        }
        auto matrixId = stringMember(*colorIterator, "matrix", sourceId);
        if (!matrixId) {
            return domain::Result<domain::MediaDescriptor>::failure(matrixId.error());
        }
        const auto matrix = colorMatrixFromId(matrixId.value());
        if (!matrix.has_value()) {
            return invalidSchema<domain::MediaDescriptor>(sourceId, "Color matrix is unknown.");
        }
        auto rangeId = stringMember(*colorIterator, "range", sourceId);
        if (!rangeId) {
            return domain::Result<domain::MediaDescriptor>::failure(rangeId.error());
        }
        const auto range = colorRangeFromId(rangeId.value());
        if (!range.has_value()) {
            return invalidSchema<domain::MediaDescriptor>(sourceId, "Color range is unknown.");
        }
        auto matrixInferred = boolMember(*colorIterator, "matrixInferred", sourceId);
        if (!matrixInferred) {
            return domain::Result<domain::MediaDescriptor>::failure(matrixInferred.error());
        }
        colorMetadata = domain::ColorMetadata{
            .matrix = *matrix,
            .range = *range,
            .matrixInferred = matrixInferred.value(),
        };
    }

    auto capabilitiesDocument = objectMember(descriptor, "decodeCapabilities", sourceId);
    if (!capabilitiesDocument) {
        return domain::Result<domain::MediaDescriptor>::failure(capabilitiesDocument.error());
    }
    auto softwareDecode = boolMember(*capabilitiesDocument.value(), "softwareDecode", sourceId);
    if (!softwareDecode) {
        return domain::Result<domain::MediaDescriptor>::failure(softwareDecode.error());
    }
    auto d3d11VaDecode = boolMember(*capabilitiesDocument.value(), "d3d11VaDecode", sourceId);
    if (!d3d11VaDecode) {
        return domain::Result<domain::MediaDescriptor>::failure(d3d11VaDecode.error());
    }

    auto timingConfidenceIdValue = stringMember(descriptor, "timingConfidence", sourceId);
    if (!timingConfidenceIdValue) {
        return domain::Result<domain::MediaDescriptor>::failure(timingConfidenceIdValue.error());
    }
    const auto timingConfidence = timingConfidenceFromId(timingConfidenceIdValue.value());
    if (!timingConfidence.has_value()) {
        return invalidSchema<domain::MediaDescriptor>(sourceId, "Timing confidence is unknown.");
    }

    domain::MediaDescriptor decoded{
        .normalizedPath = std::move(normalizedPath).value(),
        .extent = domain::MediaExtent{.width = width.value(), .height = height.value()},
        .frameRate = std::move(frameRate).value(),
        .frameCount =
            domain::FrameCountInfo{
                .value = frameCount.value(),
                .origin = *frameCountOrigin,
            },
        .duration = domain::MediaTime{duration.value()},
        .codecId = std::move(codecId).value(),
        .pixelFormatId = std::move(pixelFormatId).value(),
        .bitDepth = bitDepth.value(),
        .colorMetadata = colorMetadata,
        .decodeCapabilities =
            domain::DecodeCapabilities{
                .softwareDecode = softwareDecode.value(),
                .d3d11VaDecode = d3d11VaDecode.value(),
            },
        .timingConfidence = *timingConfidence,
        .sourceIdentity = std::move(identity).value(),
    };
    auto validated = domain::validateMediaDescriptor(std::move(decoded));
    if (!validated) {
        return invalidSchema<domain::MediaDescriptor>(sourceId, "Source descriptor is invalid.");
    }
    return validated;
}

[[nodiscard]] domain::Result<Json> encodeSource(const domain::ComparisonSource& source,
                                                const std::filesystem::path& projectDirectory) {
    const auto& descriptor = source.descriptor;
    if (!descriptor.sourceIdentity.has_value() || !descriptor.sourceIdentity->isComplete()) {
        return invalidSchema<Json>(
            source.id, "Schema-2 persistence requires a complete source file identity.");
    }
    auto storedPath = pathForDocument(descriptor.normalizedPath, projectDirectory, source.id);
    if (!storedPath) {
        return domain::Result<Json>::failure(storedPath.error());
    }

    const domain::SourceFileIdentity& identity = *descriptor.sourceIdentity;
    return domain::Result<Json>::success(Json{
        {"id", source.id},
        {"role", source.role == domain::ComparisonRole::kReference ? "reference" : "prediction"},
        {"displayName", source.displayName},
        {"path", std::move(storedPath).value()},
        {"identity",
         Json{
             {"byteSize", identity.byteSize},
             {"modifiedUtcMilliseconds", identity.modifiedUtcMilliseconds},
             {"fingerprintSha256", identity.fingerprintSha256},
         }},
        {"descriptor", encodeDescriptor(descriptor)},
    });
}

[[nodiscard]] domain::Result<std::optional<domain::FrameId>>
decodeNullableFrame(const Json& document, const std::string_view field) {
    auto value = requiredMember(document, field, std::nullopt);
    if (!value) {
        return domain::Result<std::optional<domain::FrameId>>::failure(value.error());
    }
    if (value.value()->is_null()) {
        return domain::Result<std::optional<domain::FrameId>>::success(std::nullopt);
    }
    auto frameValue = int64Value(*value.value(), std::nullopt, field);
    if (!frameValue) {
        return domain::Result<std::optional<domain::FrameId>>::failure(frameValue.error());
    }
    const domain::FrameId frameId{frameValue.value()};
    if (!frameId.isValid()) {
        return invalidSchema<std::optional<domain::FrameId>>(
            std::nullopt, "Persisted frame ID is outside the canonical range.");
    }
    return domain::Result<std::optional<domain::FrameId>>::success(frameId);
}

[[nodiscard]] domain::Result<domain::WorkspaceState> decodeWorkspace(const Json& document) {
    if (!document.is_object()) {
        return invalidSchema<domain::WorkspaceState>(
            std::nullopt, "Workspace must be a JSON object of string values.");
    }

    domain::WorkspaceState workspace;
    for (auto iterator = document.begin(); iterator != document.end(); ++iterator) {
        if (iterator.key().empty() || !iterator.value().is_string()) {
            return invalidSchema<domain::WorkspaceState>(
                std::nullopt,
                "Workspace keys must be non-empty and workspace values must be strings.");
        }
        workspace.emplace(iterator.key(), iterator.value().get<std::string>());
    }
    return domain::Result<domain::WorkspaceState>::success(std::move(workspace));
}

[[nodiscard]] domain::Result<Json> encodeWorkspace(const domain::WorkspaceState& workspace) {
    Json document = Json::object();
    for (const auto& [key, value] : workspace) {
        if (key.empty()) {
            return invalidSchema<Json>(std::nullopt,
                                       "Workspace keys must be non-empty.");
        }
        document[key] = value;
    }
    return domain::Result<Json>::success(std::move(document));
}

[[nodiscard]] domain::Result<Json> encodeDocument(const domain::Project& project,
                                                  const std::filesystem::path& projectPath) {
    auto projectDirectory = projectDirectoryFor(projectPath);
    if (!projectDirectory) {
        return domain::Result<Json>::failure(projectDirectory.error());
    }

    Json sourcesArray = Json::array();
    for (const auto& source : project.sources().sources()) {
        auto encoded = encodeSource(source, projectDirectory.value());
        if (!encoded) {
            return domain::Result<Json>::failure(encoded.error());
        }
        sourcesArray.push_back(std::move(encoded).value());
    }

    auto workspace = encodeWorkspace(project.workspaceState());
    if (!workspace) {
        return domain::Result<Json>::failure(workspace.error());
    }

    Json inMark = nullptr;
    if (project.inMark().has_value()) {
        inMark = project.inMark()->value();
    }
    Json outMark = nullptr;
    if (project.outMark().has_value()) {
        outMark = project.outMark()->value();
    }

    Json referenceSourceId = nullptr;
    if (project.sources().referenceSourceId().has_value()) {
        referenceSourceId = *project.sources().referenceSourceId();
    }

    return domain::Result<Json>::success(Json{
        {"schemaVersion", kSchemaVersion},
        {"project",
         Json{
             {"id", project.id().value()},
             {"displayName", project.displayName()},
         }},
        {"sources", std::move(sourcesArray)},
        {"referenceSourceId", referenceSourceId},
        {"marks",
         Json{
             {"inFrame", std::move(inMark)},
             {"outFrame", std::move(outMark)},
         }},
        {"lastDisplayedFrame", project.lastDisplayedFrame().value()},
        {"workspace", std::move(workspace).value()},
    });
}

[[nodiscard]] domain::Result<domain::Project>
decodeDocument(const Json& document, const std::filesystem::path& projectPath) {
    if (!document.is_object()) {
        return invalidSchema<domain::Project>(std::nullopt, "Project document must be an object.");
    }

    auto schemaVersion = int64Member(document, "schemaVersion", std::nullopt);
    if (!schemaVersion) {
        return domain::Result<domain::Project>::failure(schemaVersion.error());
    }
    if (schemaVersion.value() == 1) {
        return domain::Result<domain::Project>::failure(
            persistenceError(domain::MediaErrorCode::kUnsupportedProjectSchema,
                             std::nullopt,
                             "Legacy A/B schema-1 documents are not migrated."));
    }
    if (schemaVersion.value() != kSchemaVersion) {
        return domain::Result<domain::Project>::failure(
            persistenceError(domain::MediaErrorCode::kUnsupportedProjectSchema,
                             std::nullopt,
                             "Only project schema version 2 is supported."));
    }

    auto projectDirectory = projectDirectoryFor(projectPath);
    if (!projectDirectory) {
        return domain::Result<domain::Project>::failure(projectDirectory.error());
    }
    auto projectDocument = objectMember(document, "project", std::nullopt);
    if (!projectDocument) {
        return domain::Result<domain::Project>::failure(projectDocument.error());
    }
    auto id = stringMember(*projectDocument.value(), "id", std::nullopt);
    if (!id) {
        return domain::Result<domain::Project>::failure(id.error());
    }
    auto displayName = stringMember(*projectDocument.value(), "displayName", std::nullopt);
    if (!displayName) {
        return domain::Result<domain::Project>::failure(displayName.error());
    }

    auto sourcesArray = arrayMember(document, "sources", std::nullopt);
    if (!sourcesArray) {
        return domain::Result<domain::Project>::failure(sourcesArray.error());
    }
    const auto& sourcesList = *sourcesArray.value();
    if (sourcesList.size() < 2 || sourcesList.size() > 3) {
        return invalidSchema<domain::Project>(
            std::nullopt,
            "Sources array must contain 2-3 entries; got " +
                std::to_string(sourcesList.size()) + ".");
    }

    std::optional<domain::SourceId> referenceSourceId;
    auto refIdMember = requiredMember(document, "referenceSourceId", std::nullopt);
    if (!refIdMember) {
        return domain::Result<domain::Project>::failure(refIdMember.error());
    }
    if (refIdMember.value()->is_null()) {
        referenceSourceId = std::nullopt;
    } else {
        auto refId = uint32Member(document, "referenceSourceId", std::nullopt);
        if (!refId) {
            return domain::Result<domain::Project>::failure(refId.error());
        }
        referenceSourceId = refId.value();
    }

    std::vector<domain::ComparisonSource> sources;
    bool foundReferenceMatch = false;
    for (std::size_t i = 0; i < sourcesList.size(); ++i) {
        const auto& entry = sourcesList[i];
        std::optional<domain::SourceId> sid{static_cast<domain::SourceId>(i)};

        auto idVal = uint32Member(entry, "id", sid);
        if (!idVal) {
            return domain::Result<domain::Project>::failure(idVal.error());
        }
        auto roleId = stringMember(entry, "role", sid);
        if (!roleId) {
            return domain::Result<domain::Project>::failure(roleId.error());
        }
        domain::ComparisonRole role;
        if (roleId.value() == "reference") {
            role = domain::ComparisonRole::kReference;
        } else if (roleId.value() == "prediction") {
            role = domain::ComparisonRole::kPrediction;
        } else {
            return invalidSchema<domain::Project>(
                sid, "Unknown comparison role: " + roleId.value() + ".");
        }
        auto dn = stringMember(entry, "displayName", sid);
        if (!dn) {
            return domain::Result<domain::Project>::failure(dn.error());
        }

        auto descriptor = decodeSource(entry, projectDirectory.value(), sid);
        if (!descriptor) {
            return domain::Result<domain::Project>::failure(descriptor.error());
        }

        if (referenceSourceId.has_value() && *referenceSourceId == idVal.value()) {
            foundReferenceMatch = true;
            role = domain::ComparisonRole::kReference;
        }

        sources.push_back(domain::ComparisonSource{
            .id = idVal.value(),
            .role = role,
            .descriptor = std::move(descriptor).value(),
            .displayName = std::move(dn).value(),
        });
    }

    if (referenceSourceId.has_value() && !foundReferenceMatch) {
        return invalidSchema<domain::Project>(
            std::nullopt,
            "referenceSourceId " + std::to_string(*referenceSourceId) +
                " does not match any source entry id.");
    }

    auto validated = domain::ComparisonValidator::validate(std::move(sources));
    if (!validated) {
        return domain::Result<domain::Project>::failure(validated.error());
    }

    auto marksDocument = objectMember(document, "marks", std::nullopt);
    if (!marksDocument) {
        return domain::Result<domain::Project>::failure(marksDocument.error());
    }
    auto inMark = decodeNullableFrame(*marksDocument.value(), "inFrame");
    if (!inMark) {
        return domain::Result<domain::Project>::failure(inMark.error());
    }
    auto outMark = decodeNullableFrame(*marksDocument.value(), "outFrame");
    if (!outMark) {
        return domain::Result<domain::Project>::failure(outMark.error());
    }
    auto lastDisplayedFrame = int64Member(document, "lastDisplayedFrame", std::nullopt);
    if (!lastDisplayedFrame) {
        return domain::Result<domain::Project>::failure(lastDisplayedFrame.error());
    }
    const domain::FrameId lastFrame{lastDisplayedFrame.value()};
    if (!lastFrame.isValid()) {
        return invalidSchema<domain::Project>(
            std::nullopt, "Last displayed frame is outside the canonical range.");
    }

    auto workspaceDocument = objectMember(document, "workspace", std::nullopt);
    if (!workspaceDocument) {
        return domain::Result<domain::Project>::failure(workspaceDocument.error());
    }
    auto workspace = decodeWorkspace(*workspaceDocument.value());
    if (!workspace) {
        return domain::Result<domain::Project>::failure(workspace.error());
    }

    domain::ProjectState state{
        .id = domain::ProjectId{std::move(id).value()},
        .displayName = std::move(displayName).value(),
        .sources = std::move(validated).value().set,
        .inMark = std::move(inMark).value(),
        .outMark = std::move(outMark).value(),
        .lastDisplayedFrame = lastFrame,
        .workspaceState = std::move(workspace).value(),
    };
    return domain::Project::restorePersisted(std::move(state));
}

} // namespace

domain::Result<std::string> ProjectJson::encodeText(const domain::Project& project,
                                                    const std::filesystem::path& projectPath) {
    auto document = encodeDocument(project, projectPath);
    if (!document) {
        return domain::Result<std::string>::failure(document.error());
    }

    try {
        std::string text = std::move(document).value().dump(2);
        text.push_back('\n');
        return domain::Result<std::string>::success(std::move(text));
    } catch (const std::exception&) {
        return domain::Result<std::string>::failure(
            persistenceError(domain::MediaErrorCode::kProjectFileIo,
                             std::nullopt,
                             "Could not serialize project JSON."));
    }
}

domain::Result<domain::Project> ProjectJson::decodeText(const std::string_view documentText,
                                                        const std::filesystem::path& projectPath) {
    try {
        return decodeDocument(Json::parse(std::string{documentText}), projectPath);
    } catch (const std::exception&) {
        return invalidSchema<domain::Project>(std::nullopt,
                                              "Project document is not valid UTF-8 JSON.");
    }
}

} // namespace dvs::persistence
