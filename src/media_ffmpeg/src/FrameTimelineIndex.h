#pragma once

#include "dvs/domain/ComparisonSource.h"
#include "dvs/domain/MediaError.h"
#include "dvs/domain/Result.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace dvs::media::internal {

struct TimelineCancellation final {
    using Predicate = bool (*)(const void*) noexcept;

    Predicate isRequested = nullptr;
    const void* context = nullptr;

    [[nodiscard]] bool requested() const noexcept {
        return isRequested != nullptr && isRequested(context);
    }
};

struct TimestampIndexRequest final {
    std::filesystem::path sourcePath;
    domain::SourceId sourceId = 0;
    domain::MediaOperation operation = domain::MediaOperation::kMediaProbe;
    std::optional<std::int64_t> expectedFrameCount;
    TimelineCancellation cancellation;
};

struct TimelineRational final {
    int numerator = 0;
    int denominator = 0;

    [[nodiscard]] constexpr bool operator==(const TimelineRational&) const noexcept = default;
};

struct FrameRateCandidate final {
    TimelineRational rate;
    bool guessed = false;
};

struct CfrVerificationRequest final {
    std::span<const std::int64_t> presentationTimestamps;
    TimelineRational timeBase;
    std::vector<FrameRateCandidate> candidates;
    domain::SourceId sourceId = 0;
    domain::MediaOperation operation = domain::MediaOperation::kMediaProbe;
    TimelineCancellation cancellation;
};

struct VerifiedCfrTiming final {
    TimelineRational frameRate;
};

// Packet PTS arrives in decode order for codecs with frame reordering. Sorting produces the
// display-order timestamps addressed by FrameId. A missing or duplicate PTS is rejected rather
// than collapsed because exact ordinal identity would otherwise be ambiguous.
[[nodiscard]] domain::Result<std::vector<std::int64_t>>
validatePresentationTimestamps(std::vector<std::int64_t> packetTimestamps,
                               bool missingTimestampSeen,
                               std::optional<std::int64_t> expectedFrameCount,
                               domain::SourceId sourceId,
                               domain::MediaOperation operation);

[[nodiscard]] domain::Result<std::vector<std::int64_t>>
buildPresentationTimestampIndex(const TimestampIndexRequest& request);

// Verifies a disputed rate declaration against display-order PTS. Frame zero anchors the
// sequence; every later timestamp must land on the floor or ceiling tick of its exact rational
// boundary. Invalid and equivalent candidates are normalized before deterministic selection.
[[nodiscard]] domain::Result<VerifiedCfrTiming>
verifyConstantFrameRate(const CfrVerificationRequest& request);

} // namespace dvs::media::internal
