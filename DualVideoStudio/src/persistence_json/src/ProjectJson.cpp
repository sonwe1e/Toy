#include "dvs/persistence/ProjectJson.h"

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

constexpr std::int64_t kSchemaVersion = 1;

[[nodiscard]] domain::MediaError persistenceError(const domain::MediaErrorCode code,
                                                  const domain::SourceRole sourceRole,
                                                  std::string technicalDetail) {
    return domain::makeMediaError(code,
                                  domain::MediaOperation::kProjectPersistence,
                                  sourceRole,
                                  false,
                                  std::move(technicalDetail));
}

template <typename TValue>
[[nodiscard]] domain::Result<TValue> invalidSchema(const domain::SourceRole sourceRole,
                                                   std::string technicalDetail) {
    return domain::Result<TValue>::failure(persistenceError(
        domain::MediaErrorCode::kInvalidProjectSchema, sourceRole, std::move(technicalDetail)));
}

[[nodiscard]] domain::Result<const Json*> requiredMember(const Json& object,
                                                         const std::string_view field,
                                                         const domain::SourceRole sourceRole) {
    if (!object.is_object()) {
        return invalidSchema<const Json*>(sourceRole, "Expected a JSON object.");
    }

    const auto iterator = object.find(std::string{field});
    if (iterator == object.end()) {
        return invalidSchema<const Json*>(sourceRole,
                                          "Missing required field: " + std::string{field} + ".");
    }
    return domain::Result<const Json*>::success(std::addressof(*iterator));
}

[[nodiscard]] domain::Result<const Json*> objectMember(const Json& object,
                                                       const std::string_view field,
                                                       const domain::SourceRole sourceRole) {
    auto member = requiredMember(object, field, sourceRole);
    if (!member) {
        return domain::Result<const Json*>::failure(member.error());
    }
    if (!member.value()->is_object()) {
        return invalidSchema<const Json*>(
            sourceRole, "Field must be a JSON object: " + std::string{field} + ".");
    }
    return member;
}

[[nodiscard]] domain::Result<const Json*>
arrayMember(const Json& object, const std::string_view field, const domain::SourceRole sourceRole) {
    auto member = requiredMember(object, field, sourceRole);
    if (!member) {
        return domain::Result<const Json*>::failure(member.error());
    }
    if (!member.value()->is_array()) {
        return invalidSchema<const Json*>(
            sourceRole, "Field must be a JSON array: " + std::string{field} + ".");
    }
    return member;
}

[[nodiscard]] domain::Result<std::string> stringMember(const Json& object,
                                                       const std::string_view field,
                                                       const domain::SourceRole sourceRole) {
    auto member = requiredMember(object, field, sourceRole);
    if (!member) {
        return domain::Result<std::string>::failure(member.error());
    }
    if (!member.value()->is_string()) {
        return invalidSchema<std::string>(sourceRole,
                                          "Field must be a string: " + std::string{field} + ".");
    }
    return domain::Result<std::string>::success(member.value()->get<std::string>());
}

[[nodiscard]] domain::Result<bool>
boolMember(const Json& object, const std::string_view field, const domain::SourceRole sourceRole) {
    auto member = requiredMember(object, field, sourceRole);
    if (!member) {
        return domain::Result<bool>::failure(member.error());
    }
    if (!member.value()->is_boolean()) {
        return invalidSchema<bool>(sourceRole,
                                   "Field must be a boolean: " + std::string{field} + ".");
    }
    return domain::Result<bool>::success(member.value()->get<bool>());
}

[[nodiscard]] domain::Result<std::int64_t>
int64Value(const Json& value, const domain::SourceRole sourceRole, const std::string_view field) {
    if (value.is_number_unsigned()) {
        const std::uint64_t unsignedValue = value.get<std::uint64_t>();
        if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return invalidSchema<std::int64_t>(
                sourceRole, "Integer value exceeds int64 range: " + std::string{field} + ".");
        }
        return domain::Result<std::int64_t>::success(static_cast<std::int64_t>(unsignedValue));
    }
    if (value.is_number_integer()) {
        return domain::Result<std::int64_t>::success(value.get<std::int64_t>());
    }
    return invalidSchema<std::int64_t>(sourceRole,
                                       "Field must be an integer: " + std::string{field} + ".");
}

[[nodiscard]] domain::Result<std::uint64_t>
uint64Value(const Json& value, const domain::SourceRole sourceRole, const std::string_view field) {
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
        sourceRole, "Field must be an unsigned integer: " + std::string{field} + ".");
}

[[nodiscard]] domain::Result<std::int64_t>
int64Member(const Json& object, const std::string_view field, const domain::SourceRole sourceRole) {
    auto member = requiredMember(object, field, sourceRole);
    if (!member) {
        return domain::Result<std::int64_t>::failure(member.error());
    }
    return int64Value(*member.value(), sourceRole, field);
}

[[nodiscard]] domain::Result<std::uint64_t> uint64Member(const Json& object,
                                                         const std::string_view field,
                                                         const domain::SourceRole sourceRole) {
    auto member = requiredMember(object, field, sourceRole);
    if (!member) {
        return domain::Result<std::uint64_t>::failure(member.error());
    }
    return uint64Value(*member.value(), sourceRole, field);
}

[[nodiscard]] domain::Result<std::uint32_t> uint32Member(const Json& object,
                                                         const std::string_view field,
                                                         const domain::SourceRole sourceRole) {
    auto value = uint64Member(object, field, sourceRole);
    if (!value) {
        return domain::Result<std::uint32_t>::failure(value.error());
    }
    if (value.value() > std::numeric_limits<std::uint32_t>::max()) {
        return invalidSchema<std::uint32_t>(
            sourceRole, "Integer value exceeds uint32 range: " + std::string{field} + ".");
    }
    return domain::Result<std::uint32_t>::success(static_cast<std::uint32_t>(value.value()));
}

[[nodiscard]] domain::Result<std::uint8_t>
uint8Member(const Json& object, const std::string_view field, const domain::SourceRole sourceRole) {
    auto value = uint64Member(object, field, sourceRole);
    if (!value) {
        return domain::Result<std::uint8_t>::failure(value.error());
    }
    if (value.value() > std::numeric_limits<std::uint8_t>::max()) {
        return invalidSchema<std::uint8_t>(
            sourceRole, "Integer value exceeds uint8 range: " + std::string{field} + ".");
    }
    return domain::Result<std::uint8_t>::success(static_cast<std::uint8_t>(value.value()));
}

[[nodiscard]] domain::Result<std::filesystem::path>
projectDirectoryFor(const std::filesystem::path& projectPath) {
    if (projectPath.empty()) {
        return invalidSchema<std::filesystem::path>(domain::SourceRole::kProject,
                                                    "Project JSON requires a destination path.");
    }

    std::error_code errorCode;
    const std::filesystem::path absoluteProjectPath =
        std::filesystem::absolute(projectPath, errorCode);
    if (errorCode) {
        return invalidSchema<std::filesystem::path>(
            domain::SourceRole::kProject, "Could not resolve the project destination path.");
    }

    const std::filesystem::path directory = absoluteProjectPath.parent_path();
    if (directory.empty()) {
        return invalidSchema<std::filesystem::path>(domain::SourceRole::kProject,
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
                    const domain::SourceRole sourceRole) {
    if (sourcePath.empty()) {
        return invalidSchema<std::filesystem::path>(sourceRole, "Source path is empty.");
    }
    if (sourcePath.is_relative()) {
        return domain::Result<std::filesystem::path>::success(
            (projectDirectory / sourcePath).lexically_normal());
    }

    std::error_code errorCode;
    const std::filesystem::path absoluteSourcePath =
        std::filesystem::absolute(sourcePath, errorCode);
    if (errorCode) {
        return invalidSchema<std::filesystem::path>(sourceRole, "Could not resolve source path.");
    }
    return domain::Result<std::filesystem::path>::success(absoluteSourcePath.lexically_normal());
}

[[nodiscard]] domain::Result<std::string>
pathForDocument(const std::filesystem::path& sourcePath,
                const std::filesystem::path& projectDirectory,
                const domain::SourceRole sourceRole) {
    auto normalizedSourcePath = normalizeSourcePath(sourcePath, projectDirectory, sourceRole);
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
                 const domain::SourceRole sourceRole) {
    if (storedPath.empty() || storedPath.find('\0') != std::string_view::npos) {
        return invalidSchema<std::filesystem::path>(sourceRole,
                                                    "Persisted source path is invalid.");
    }

    const std::filesystem::path path{std::string{storedPath}};
    if (path.is_relative() && hasParentReference(path)) {
        return invalidSchema<std::filesystem::path>(
            sourceRole, "Relative source paths may not escape the project directory.");
    }
    return normalizeSourcePath(path, projectDirectory, sourceRole);
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

[[nodiscard]] std::optional<domain::ExportJobState>
exportJobStateFromId(const std::string_view identifier) noexcept {
    if (identifier == "pending") {
        return domain::ExportJobState::kPending;
    }
    if (identifier == "running") {
        return domain::ExportJobState::kRunning;
    }
    if (identifier == "succeeded") {
        return domain::ExportJobState::kSucceeded;
    }
    if (identifier == "failed") {
        return domain::ExportJobState::kFailed;
    }
    if (identifier == "canceled") {
        return domain::ExportJobState::kCanceled;
    }
    if (identifier == "interrupted") {
        return domain::ExportJobState::kInterrupted;
    }
    return std::nullopt;
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
    if (identifier == "invalid-frame-range") {
        return Code::kInvalidFrameRange;
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
    if (identifier == "duplicate-identifier") {
        return Code::kDuplicateIdentifier;
    }
    if (identifier == "marks-incomplete") {
        return Code::kMarksIncomplete;
    }
    if (identifier == "marks-reversed") {
        return Code::kMarksReversed;
    }
    if (identifier == "clip-out-of-range") {
        return Code::kClipOutOfRange;
    }
    if (identifier == "clip-not-found") {
        return Code::kClipNotFound;
    }
    if (identifier == "export-record-not-found") {
        return Code::kExportRecordNotFound;
    }
    if (identifier == "duplicate-clip-selection") {
        return Code::kDuplicateClipSelection;
    }
    if (identifier == "invalid-export-mode") {
        return Code::kInvalidExportMode;
    }
    if (identifier == "invalid-export-geometry") {
        return Code::kInvalidExportGeometry;
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
    if (identifier == "clip-mutation") {
        return Operation::kClipMutation;
    }
    if (identifier == "export-plan-build") {
        return Operation::kExportPlanBuild;
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

[[nodiscard]] std::optional<domain::SourceRole>
sourceRoleFromId(const std::string_view identifier) noexcept {
    using Role = domain::SourceRole;
    if (identifier == "none") {
        return Role::kNone;
    }
    if (identifier == "a") {
        return Role::kA;
    }
    if (identifier == "b") {
        return Role::kB;
    }
    if (identifier == "pair") {
        return Role::kPair;
    }
    if (identifier == "project") {
        return Role::kProject;
    }
    if (identifier == "clip") {
        return Role::kClip;
    }
    if (identifier == "export") {
        return Role::kExport;
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
                                                              const domain::SourceRole sourceRole) {
    auto numerator = int64Member(document, "numerator", sourceRole);
    if (!numerator) {
        return domain::Result<domain::RationalRate>::failure(numerator.error());
    }
    auto denominator = int64Member(document, "denominator", sourceRole);
    if (!denominator) {
        return domain::Result<domain::RationalRate>::failure(denominator.error());
    }

    auto rate = domain::RationalRate::create(numerator.value(), denominator.value());
    if (!rate) {
        return invalidSchema<domain::RationalRate>(
            sourceRole, "Frame-rate numerator and denominator must form a positive rational rate.");
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
    const Json& document, const std::string_view field, const domain::SourceRole sourceRole) {
    auto member = requiredMember(document, field, sourceRole);
    if (!member) {
        return domain::Result<std::optional<domain::RationalRate>>::failure(member.error());
    }
    if (member.value()->is_null()) {
        return domain::Result<std::optional<domain::RationalRate>>::success(std::nullopt);
    }
    auto rate = decodeRate(*member.value(), sourceRole);
    if (!rate) {
        return domain::Result<std::optional<domain::RationalRate>>::failure(rate.error());
    }
    return domain::Result<std::optional<domain::RationalRate>>::success(
        std::optional<domain::RationalRate>{std::move(rate).value()});
}

[[nodiscard]] Json encodeMediaError(const domain::MediaError& error) {
    Json requestId = nullptr;
    if (error.requestId.has_value()) {
        requestId = error.requestId->value();
    }
    return Json{
        {"code", std::string{domain::stableId(error.code)}},
        {"operation", std::string{domain::stableId(error.operation)}},
        {"sourceRole", std::string{domain::stableId(error.sourceRole)}},
        {"requestId", std::move(requestId)},
        {"recoverable", error.recoverable},
        {"userMessageKey", error.userMessageKey},
        {"technicalDetail", error.technicalDetail},
    };
}

[[nodiscard]] domain::Result<domain::MediaError> decodeMediaError(const Json& document) {
    constexpr domain::SourceRole kErrorRole = domain::SourceRole::kExport;
    if (!document.is_object()) {
        return invalidSchema<domain::MediaError>(kErrorRole, "Export error must be a JSON object.");
    }

    auto codeId = stringMember(document, "code", kErrorRole);
    if (!codeId) {
        return domain::Result<domain::MediaError>::failure(codeId.error());
    }
    const auto code = mediaErrorCodeFromId(codeId.value());
    if (!code.has_value()) {
        return invalidSchema<domain::MediaError>(kErrorRole, "Export error code is unknown.");
    }

    auto operationId = stringMember(document, "operation", kErrorRole);
    if (!operationId) {
        return domain::Result<domain::MediaError>::failure(operationId.error());
    }
    const auto operation = mediaOperationFromId(operationId.value());
    if (!operation.has_value()) {
        return invalidSchema<domain::MediaError>(kErrorRole, "Export error operation is unknown.");
    }

    auto sourceRoleId = stringMember(document, "sourceRole", kErrorRole);
    if (!sourceRoleId) {
        return domain::Result<domain::MediaError>::failure(sourceRoleId.error());
    }
    const auto sourceRole = sourceRoleFromId(sourceRoleId.value());
    if (!sourceRole.has_value()) {
        return invalidSchema<domain::MediaError>(kErrorRole,
                                                 "Export error source role is unknown.");
    }

    auto requestIdDocument = requiredMember(document, "requestId", kErrorRole);
    if (!requestIdDocument) {
        return domain::Result<domain::MediaError>::failure(requestIdDocument.error());
    }
    std::optional<domain::RequestId> requestId;
    if (!requestIdDocument.value()->is_null()) {
        auto requestIdValue = uint64Value(*requestIdDocument.value(), kErrorRole, "requestId");
        if (!requestIdValue) {
            return domain::Result<domain::MediaError>::failure(requestIdValue.error());
        }
        requestId = domain::RequestId{requestIdValue.value()};
    }

    auto recoverable = boolMember(document, "recoverable", kErrorRole);
    if (!recoverable) {
        return domain::Result<domain::MediaError>::failure(recoverable.error());
    }
    auto userMessageKey = stringMember(document, "userMessageKey", kErrorRole);
    if (!userMessageKey) {
        return domain::Result<domain::MediaError>::failure(userMessageKey.error());
    }
    auto technicalDetail = stringMember(document, "technicalDetail", kErrorRole);
    if (!technicalDetail) {
        return domain::Result<domain::MediaError>::failure(technicalDetail.error());
    }

    return domain::Result<domain::MediaError>::success(domain::MediaError{
        .code = *code,
        .operation = *operation,
        .sourceRole = *sourceRole,
        .requestId = requestId,
        .recoverable = recoverable.value(),
        .userMessageKey = std::move(userMessageKey).value(),
        .technicalDetail = std::move(technicalDetail).value(),
    });
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
decodeSourceIdentity(const Json& document, const domain::SourceRole sourceRole) {
    auto byteSize = uint64Member(document, "byteSize", sourceRole);
    if (!byteSize) {
        return domain::Result<domain::SourceFileIdentity>::failure(byteSize.error());
    }
    auto modifiedUtcMilliseconds = int64Member(document, "modifiedUtcMilliseconds", sourceRole);
    if (!modifiedUtcMilliseconds) {
        return domain::Result<domain::SourceFileIdentity>::failure(modifiedUtcMilliseconds.error());
    }
    auto fingerprint = stringMember(document, "fingerprintSha256", sourceRole);
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
            sourceRole,
            "Source identity must contain a non-zero size and a 64-character SHA-256 value.");
    }
    return domain::Result<domain::SourceFileIdentity>::success(std::move(identity));
}

[[nodiscard]] domain::Result<domain::MediaDescriptor>
decodeSource(const Json& document,
             const std::filesystem::path& projectDirectory,
             const domain::SourceRole sourceRole) {
    if (!document.is_object()) {
        return invalidSchema<domain::MediaDescriptor>(sourceRole,
                                                      "Source entry must be an object.");
    }

    auto storedPath = stringMember(document, "path", sourceRole);
    if (!storedPath) {
        return domain::Result<domain::MediaDescriptor>::failure(storedPath.error());
    }
    auto normalizedPath = pathFromDocument(storedPath.value(), projectDirectory, sourceRole);
    if (!normalizedPath) {
        return domain::Result<domain::MediaDescriptor>::failure(normalizedPath.error());
    }

    auto identityDocument = objectMember(document, "identity", sourceRole);
    if (!identityDocument) {
        return domain::Result<domain::MediaDescriptor>::failure(identityDocument.error());
    }
    auto identity = decodeSourceIdentity(*identityDocument.value(), sourceRole);
    if (!identity) {
        return domain::Result<domain::MediaDescriptor>::failure(identity.error());
    }

    auto descriptorDocument = objectMember(document, "descriptor", sourceRole);
    if (!descriptorDocument) {
        return domain::Result<domain::MediaDescriptor>::failure(descriptorDocument.error());
    }
    const Json& descriptor = *descriptorDocument.value();

    auto extentDocument = objectMember(descriptor, "extent", sourceRole);
    if (!extentDocument) {
        return domain::Result<domain::MediaDescriptor>::failure(extentDocument.error());
    }
    auto width = uint32Member(*extentDocument.value(), "width", sourceRole);
    if (!width) {
        return domain::Result<domain::MediaDescriptor>::failure(width.error());
    }
    auto height = uint32Member(*extentDocument.value(), "height", sourceRole);
    if (!height) {
        return domain::Result<domain::MediaDescriptor>::failure(height.error());
    }

    auto frameRate = decodeOptionalRate(descriptor, "frameRate", sourceRole);
    if (!frameRate) {
        return domain::Result<domain::MediaDescriptor>::failure(frameRate.error());
    }

    auto frameCountDocument = objectMember(descriptor, "frameCount", sourceRole);
    if (!frameCountDocument) {
        return domain::Result<domain::MediaDescriptor>::failure(frameCountDocument.error());
    }
    auto frameCount = int64Member(*frameCountDocument.value(), "value", sourceRole);
    if (!frameCount) {
        return domain::Result<domain::MediaDescriptor>::failure(frameCount.error());
    }
    auto frameCountOriginIdValue = stringMember(*frameCountDocument.value(), "origin", sourceRole);
    if (!frameCountOriginIdValue) {
        return domain::Result<domain::MediaDescriptor>::failure(frameCountOriginIdValue.error());
    }
    const auto frameCountOrigin = frameCountOriginFromId(frameCountOriginIdValue.value());
    if (!frameCountOrigin.has_value()) {
        return invalidSchema<domain::MediaDescriptor>(sourceRole, "Frame-count origin is unknown.");
    }

    auto duration = int64Member(descriptor, "durationMicroseconds", sourceRole);
    if (!duration) {
        return domain::Result<domain::MediaDescriptor>::failure(duration.error());
    }
    auto codecId = stringMember(descriptor, "codecId", sourceRole);
    if (!codecId) {
        return domain::Result<domain::MediaDescriptor>::failure(codecId.error());
    }
    auto pixelFormatId = stringMember(descriptor, "pixelFormatId", sourceRole);
    if (!pixelFormatId) {
        return domain::Result<domain::MediaDescriptor>::failure(pixelFormatId.error());
    }
    auto bitDepth = uint8Member(descriptor, "bitDepth", sourceRole);
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
            return invalidSchema<domain::MediaDescriptor>(sourceRole,
                                                          "Color metadata must be an object.");
        }
        auto matrixId = stringMember(*colorIterator, "matrix", sourceRole);
        if (!matrixId) {
            return domain::Result<domain::MediaDescriptor>::failure(matrixId.error());
        }
        const auto matrix = colorMatrixFromId(matrixId.value());
        if (!matrix.has_value()) {
            return invalidSchema<domain::MediaDescriptor>(sourceRole, "Color matrix is unknown.");
        }
        auto rangeId = stringMember(*colorIterator, "range", sourceRole);
        if (!rangeId) {
            return domain::Result<domain::MediaDescriptor>::failure(rangeId.error());
        }
        const auto range = colorRangeFromId(rangeId.value());
        if (!range.has_value()) {
            return invalidSchema<domain::MediaDescriptor>(sourceRole, "Color range is unknown.");
        }
        auto matrixInferred = boolMember(*colorIterator, "matrixInferred", sourceRole);
        if (!matrixInferred) {
            return domain::Result<domain::MediaDescriptor>::failure(matrixInferred.error());
        }
        colorMetadata = domain::ColorMetadata{
            .matrix = *matrix,
            .range = *range,
            .matrixInferred = matrixInferred.value(),
        };
    }

    auto capabilitiesDocument = objectMember(descriptor, "decodeCapabilities", sourceRole);
    if (!capabilitiesDocument) {
        return domain::Result<domain::MediaDescriptor>::failure(capabilitiesDocument.error());
    }
    auto softwareDecode = boolMember(*capabilitiesDocument.value(), "softwareDecode", sourceRole);
    if (!softwareDecode) {
        return domain::Result<domain::MediaDescriptor>::failure(softwareDecode.error());
    }
    auto d3d11VaDecode = boolMember(*capabilitiesDocument.value(), "d3d11VaDecode", sourceRole);
    if (!d3d11VaDecode) {
        return domain::Result<domain::MediaDescriptor>::failure(d3d11VaDecode.error());
    }

    auto timingConfidenceIdValue = stringMember(descriptor, "timingConfidence", sourceRole);
    if (!timingConfidenceIdValue) {
        return domain::Result<domain::MediaDescriptor>::failure(timingConfidenceIdValue.error());
    }
    const auto timingConfidence = timingConfidenceFromId(timingConfidenceIdValue.value());
    if (!timingConfidence.has_value()) {
        return invalidSchema<domain::MediaDescriptor>(sourceRole, "Timing confidence is unknown.");
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
        return invalidSchema<domain::MediaDescriptor>(sourceRole, "Source descriptor is invalid.");
    }
    return validated;
}

[[nodiscard]] domain::Result<Json> encodeSource(const domain::MediaDescriptor& descriptor,
                                                const std::filesystem::path& projectDirectory,
                                                const domain::SourceRole sourceRole) {
    if (!descriptor.sourceIdentity.has_value() || !descriptor.sourceIdentity->isComplete()) {
        return invalidSchema<Json>(
            sourceRole, "Schema-1 persistence requires a complete source file identity.");
    }
    auto storedPath = pathForDocument(descriptor.normalizedPath, projectDirectory, sourceRole);
    if (!storedPath) {
        return domain::Result<Json>::failure(storedPath.error());
    }

    const domain::SourceFileIdentity& identity = *descriptor.sourceIdentity;
    return domain::Result<Json>::success(Json{
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

[[nodiscard]] Json encodeClip(const domain::Clip& clip) {
    return Json{
        {"id", clip.id.value()},
        {"name", clip.name},
        {"note", clip.note},
        {"inFrame", clip.range.first().value()},
        {"outFrame", clip.range.last().value()},
    };
}

[[nodiscard]] domain::Result<domain::Clip> decodeClip(const Json& document) {
    constexpr domain::SourceRole kClipRole = domain::SourceRole::kClip;
    if (!document.is_object()) {
        return invalidSchema<domain::Clip>(kClipRole, "Clip entry must be a JSON object.");
    }
    auto id = stringMember(document, "id", kClipRole);
    if (!id) {
        return domain::Result<domain::Clip>::failure(id.error());
    }
    auto name = stringMember(document, "name", kClipRole);
    if (!name) {
        return domain::Result<domain::Clip>::failure(name.error());
    }
    auto note = stringMember(document, "note", kClipRole);
    if (!note) {
        return domain::Result<domain::Clip>::failure(note.error());
    }
    auto inFrame = int64Member(document, "inFrame", kClipRole);
    if (!inFrame) {
        return domain::Result<domain::Clip>::failure(inFrame.error());
    }
    auto outFrame = int64Member(document, "outFrame", kClipRole);
    if (!outFrame) {
        return domain::Result<domain::Clip>::failure(outFrame.error());
    }

    auto range = domain::FrameRange::inclusive(domain::FrameId{inFrame.value()},
                                               domain::FrameId{outFrame.value()});
    if (!range) {
        return invalidSchema<domain::Clip>(kClipRole, "Clip frames must form an inclusive range.");
    }
    return domain::Result<domain::Clip>::success(domain::Clip{
        .id = domain::ClipId{std::move(id).value()},
        .name = std::move(name).value(),
        .note = std::move(note).value(),
        .range = std::move(range).value(),
    });
}

[[nodiscard]] Json encodeExportRecord(const domain::ExportRecord& record) {
    Json error = nullptr;
    if (record.error.has_value()) {
        error = encodeMediaError(*record.error);
    }
    return Json{
        {"id", record.id.value()},
        {"clipId", record.clipId.value()},
        {"state", std::string{domain::stableId(record.state)}},
        {"outputReference", record.outputReference},
        {"error", std::move(error)},
    };
}

[[nodiscard]] domain::Result<domain::ExportRecord> decodeExportRecord(const Json& document) {
    constexpr domain::SourceRole kExportRole = domain::SourceRole::kExport;
    if (!document.is_object()) {
        return invalidSchema<domain::ExportRecord>(kExportRole,
                                                   "Export entry must be a JSON object.");
    }
    auto id = stringMember(document, "id", kExportRole);
    if (!id) {
        return domain::Result<domain::ExportRecord>::failure(id.error());
    }
    auto clipId = stringMember(document, "clipId", kExportRole);
    if (!clipId) {
        return domain::Result<domain::ExportRecord>::failure(clipId.error());
    }
    auto stateId = stringMember(document, "state", kExportRole);
    if (!stateId) {
        return domain::Result<domain::ExportRecord>::failure(stateId.error());
    }
    const auto state = exportJobStateFromId(stateId.value());
    if (!state.has_value()) {
        return invalidSchema<domain::ExportRecord>(kExportRole, "Export state is unknown.");
    }
    auto outputReference = stringMember(document, "outputReference", kExportRole);
    if (!outputReference) {
        return domain::Result<domain::ExportRecord>::failure(outputReference.error());
    }
    auto errorDocument = requiredMember(document, "error", kExportRole);
    if (!errorDocument) {
        return domain::Result<domain::ExportRecord>::failure(errorDocument.error());
    }

    std::optional<domain::MediaError> error;
    if (!errorDocument.value()->is_null()) {
        auto decodedError = decodeMediaError(*errorDocument.value());
        if (!decodedError) {
            return domain::Result<domain::ExportRecord>::failure(decodedError.error());
        }
        error = std::move(decodedError).value();
    }

    return domain::Result<domain::ExportRecord>::success(domain::ExportRecord{
        .id = domain::ExportRecordId{std::move(id).value()},
        .clipId = domain::ClipId{std::move(clipId).value()},
        .state = *state,
        .outputReference = std::move(outputReference).value(),
        .error = std::move(error),
    });
}

[[nodiscard]] domain::Result<std::optional<domain::FrameId>>
decodeNullableFrame(const Json& document, const std::string_view field) {
    constexpr domain::SourceRole kProjectRole = domain::SourceRole::kProject;
    auto value = requiredMember(document, field, kProjectRole);
    if (!value) {
        return domain::Result<std::optional<domain::FrameId>>::failure(value.error());
    }
    if (value.value()->is_null()) {
        return domain::Result<std::optional<domain::FrameId>>::success(std::nullopt);
    }
    auto frameValue = int64Value(*value.value(), kProjectRole, field);
    if (!frameValue) {
        return domain::Result<std::optional<domain::FrameId>>::failure(frameValue.error());
    }
    const domain::FrameId frameId{frameValue.value()};
    if (!frameId.isValid()) {
        return invalidSchema<std::optional<domain::FrameId>>(
            kProjectRole, "Persisted frame ID is outside the canonical range.");
    }
    return domain::Result<std::optional<domain::FrameId>>::success(frameId);
}

[[nodiscard]] domain::Result<domain::WorkspaceState> decodeWorkspace(const Json& document) {
    constexpr domain::SourceRole kProjectRole = domain::SourceRole::kProject;
    if (!document.is_object()) {
        return invalidSchema<domain::WorkspaceState>(
            kProjectRole, "Workspace must be a JSON object of string values.");
    }

    domain::WorkspaceState workspace;
    for (auto iterator = document.begin(); iterator != document.end(); ++iterator) {
        if (iterator.key().empty() || !iterator.value().is_string()) {
            return invalidSchema<domain::WorkspaceState>(
                kProjectRole,
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
            return invalidSchema<Json>(domain::SourceRole::kProject,
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

    auto sourceA =
        encodeSource(project.sources().sourceA(), projectDirectory.value(), domain::SourceRole::kA);
    if (!sourceA) {
        return domain::Result<Json>::failure(sourceA.error());
    }
    auto sourceB =
        encodeSource(project.sources().sourceB(), projectDirectory.value(), domain::SourceRole::kB);
    if (!sourceB) {
        return domain::Result<Json>::failure(sourceB.error());
    }
    auto workspace = encodeWorkspace(project.workspaceState());
    if (!workspace) {
        return domain::Result<Json>::failure(workspace.error());
    }

    Json clips = Json::array();
    for (const domain::Clip& clip : project.clips()) {
        clips.push_back(encodeClip(clip));
    }
    Json exports = Json::array();
    for (const domain::ExportRecord& record : project.exportRecords()) {
        exports.push_back(encodeExportRecord(record));
    }

    Json inMark = nullptr;
    if (project.inMark().has_value()) {
        inMark = project.inMark()->value();
    }
    Json outMark = nullptr;
    if (project.outMark().has_value()) {
        outMark = project.outMark()->value();
    }

    return domain::Result<Json>::success(Json{
        {"schemaVersion", kSchemaVersion},
        {"project",
         Json{
             {"id", project.id().value()},
             {"displayName", project.displayName()},
         }},
        {"sources",
         Json{
             {"a", std::move(sourceA).value()},
             {"b", std::move(sourceB).value()},
         }},
        {"canonicalTimeline",
         Json{
             {"frameRate", encodeOptionalRate(project.sources().canonicalRate())},
             {"frameCount", project.sources().canonicalFrameCount()},
         }},
        {"clips", std::move(clips)},
        {"exports", std::move(exports)},
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
    constexpr domain::SourceRole kProjectRole = domain::SourceRole::kProject;
    if (!document.is_object()) {
        return invalidSchema<domain::Project>(kProjectRole, "Project document must be an object.");
    }

    auto schemaVersion = int64Member(document, "schemaVersion", kProjectRole);
    if (!schemaVersion) {
        return domain::Result<domain::Project>::failure(schemaVersion.error());
    }
    if (schemaVersion.value() != kSchemaVersion) {
        return domain::Result<domain::Project>::failure(
            persistenceError(domain::MediaErrorCode::kUnsupportedProjectSchema,
                             kProjectRole,
                             "Only project schema version 1 is supported."));
    }

    auto projectDirectory = projectDirectoryFor(projectPath);
    if (!projectDirectory) {
        return domain::Result<domain::Project>::failure(projectDirectory.error());
    }
    auto projectDocument = objectMember(document, "project", kProjectRole);
    if (!projectDocument) {
        return domain::Result<domain::Project>::failure(projectDocument.error());
    }
    auto id = stringMember(*projectDocument.value(), "id", kProjectRole);
    if (!id) {
        return domain::Result<domain::Project>::failure(id.error());
    }
    auto displayName = stringMember(*projectDocument.value(), "displayName", kProjectRole);
    if (!displayName) {
        return domain::Result<domain::Project>::failure(displayName.error());
    }

    auto sourcesDocument = objectMember(document, "sources", kProjectRole);
    if (!sourcesDocument) {
        return domain::Result<domain::Project>::failure(sourcesDocument.error());
    }
    auto sourceADocument = objectMember(*sourcesDocument.value(), "a", domain::SourceRole::kA);
    if (!sourceADocument) {
        return domain::Result<domain::Project>::failure(sourceADocument.error());
    }
    auto sourceBDocument = objectMember(*sourcesDocument.value(), "b", domain::SourceRole::kB);
    if (!sourceBDocument) {
        return domain::Result<domain::Project>::failure(sourceBDocument.error());
    }
    auto sourceA =
        decodeSource(*sourceADocument.value(), projectDirectory.value(), domain::SourceRole::kA);
    if (!sourceA) {
        return domain::Result<domain::Project>::failure(sourceA.error());
    }
    auto sourceB =
        decodeSource(*sourceBDocument.value(), projectDirectory.value(), domain::SourceRole::kB);
    if (!sourceB) {
        return domain::Result<domain::Project>::failure(sourceB.error());
    }
    auto sources = domain::SourcePairValidator::validate(sourceA.value(), sourceB.value());
    if (!sources) {
        return domain::Result<domain::Project>::failure(sources.error());
    }

    auto canonicalTimeline = objectMember(document, "canonicalTimeline", kProjectRole);
    if (!canonicalTimeline) {
        return domain::Result<domain::Project>::failure(canonicalTimeline.error());
    }
    auto canonicalRate =
        decodeOptionalRate(*canonicalTimeline.value(), "frameRate", domain::SourceRole::kPair);
    if (!canonicalRate) {
        return domain::Result<domain::Project>::failure(canonicalRate.error());
    }
    auto canonicalFrameCount =
        int64Member(*canonicalTimeline.value(), "frameCount", domain::SourceRole::kPair);
    if (!canonicalFrameCount) {
        return domain::Result<domain::Project>::failure(canonicalFrameCount.error());
    }
    if (sources.value().canonicalRate() != canonicalRate.value() ||
        sources.value().canonicalFrameCount() != canonicalFrameCount.value()) {
        return invalidSchema<domain::Project>(
            domain::SourceRole::kPair,
            "Canonical timeline does not match the validated source descriptors.");
    }

    auto clipsDocument = arrayMember(document, "clips", kProjectRole);
    if (!clipsDocument) {
        return domain::Result<domain::Project>::failure(clipsDocument.error());
    }
    std::vector<domain::Clip> clips;
    clips.reserve(clipsDocument.value()->size());
    for (const Json& clipDocument : *clipsDocument.value()) {
        auto clip = decodeClip(clipDocument);
        if (!clip) {
            return domain::Result<domain::Project>::failure(clip.error());
        }
        clips.push_back(std::move(clip).value());
    }

    auto exportsDocument = arrayMember(document, "exports", kProjectRole);
    if (!exportsDocument) {
        return domain::Result<domain::Project>::failure(exportsDocument.error());
    }
    std::vector<domain::ExportRecord> exports;
    exports.reserve(exportsDocument.value()->size());
    for (const Json& exportDocument : *exportsDocument.value()) {
        auto record = decodeExportRecord(exportDocument);
        if (!record) {
            return domain::Result<domain::Project>::failure(record.error());
        }
        exports.push_back(std::move(record).value());
    }

    auto marksDocument = objectMember(document, "marks", kProjectRole);
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
    auto lastDisplayedFrame = int64Member(document, "lastDisplayedFrame", kProjectRole);
    if (!lastDisplayedFrame) {
        return domain::Result<domain::Project>::failure(lastDisplayedFrame.error());
    }
    const domain::FrameId lastFrame{lastDisplayedFrame.value()};
    if (!lastFrame.isValid()) {
        return invalidSchema<domain::Project>(
            kProjectRole, "Last displayed frame is outside the canonical range.");
    }

    auto workspaceDocument = objectMember(document, "workspace", kProjectRole);
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
        .sources = std::move(sources).value(),
        .clips = std::move(clips),
        .exportRecords = std::move(exports),
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
                             domain::SourceRole::kProject,
                             "Could not serialize project JSON."));
    }
}

domain::Result<domain::Project> ProjectJson::decodeText(const std::string_view documentText,
                                                        const std::filesystem::path& projectPath) {
    try {
        return decodeDocument(Json::parse(std::string{documentText}), projectPath);
    } catch (const std::exception&) {
        return invalidSchema<domain::Project>(domain::SourceRole::kProject,
                                              "Project document is not valid UTF-8 JSON.");
    }
}

} // namespace dvs::persistence
