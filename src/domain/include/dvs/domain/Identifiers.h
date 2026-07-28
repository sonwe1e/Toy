#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace dvs::domain {

// FrameId is the zero-based display-order ordinal on the canonical timeline. It is independent of
// source timestamps. INT64_MAX is reserved as the exclusive end boundary for a final valid frame,
// so it is never itself a valid frame ID.
class FrameId final {
public:
    constexpr explicit FrameId(const std::int64_t value) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::int64_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return value_ >= 0 && value_ < std::numeric_limits<std::int64_t>::max();
    }

    [[nodiscard]] constexpr auto operator<=>(const FrameId&) const noexcept = default;

private:
    std::int64_t value_;
};

// MediaTime is a signed microsecond value. Negative values remain valid for adapter PTS data;
// timeline conversion explicitly rejects them where a FrameId is required.
class MediaTime final {
public:
    constexpr explicit MediaTime(const std::int64_t microseconds) noexcept
        : microseconds_(microseconds) {}

    [[nodiscard]] constexpr std::int64_t microseconds() const noexcept {
        return microseconds_;
    }

    [[nodiscard]] constexpr auto operator<=>(const MediaTime&) const noexcept = default;

private:
    std::int64_t microseconds_;
};

template <typename Tag> class CounterId final {
public:
    constexpr explicit CounterId(const std::uint64_t value) noexcept : value_(value) {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    [[nodiscard]] constexpr auto operator<=>(const CounterId&) const noexcept = default;

private:
    std::uint64_t value_;
};

struct SessionIdTag;
struct SessionEpochTag;
struct PlaybackGenerationTag;
struct DeviceGenerationTag;
struct RequestIdTag;
struct CommandIdTag;
struct JobIdTag;
struct JobAttemptTag;
struct ProjectRevisionTag;

using SessionId = CounterId<SessionIdTag>;
using SessionEpoch = CounterId<SessionEpochTag>;
using PlaybackGeneration = CounterId<PlaybackGenerationTag>;
using DeviceGeneration = CounterId<DeviceGenerationTag>;
using RequestId = CounterId<RequestIdTag>;
using CommandId = CounterId<CommandIdTag>;
using JobId = CounterId<JobIdTag>;
using JobAttempt = CounterId<JobAttemptTag>;
using ProjectRevision = CounterId<ProjectRevisionTag>;

// Persistent IDs are supplied by a caller (normally a UUID-producing adapter). The domain only
// requires a stable non-empty value and enforces uniqueness in the aggregate.
template <typename Tag> class PersistentId final {
public:
    explicit PersistentId(std::string value) : value_(std::move(value)) {}

    [[nodiscard]] const std::string& value() const noexcept {
        return value_;
    }

    [[nodiscard]] bool isValid() const noexcept {
        return !value_.empty();
    }

    [[nodiscard]] auto operator<=>(const PersistentId&) const = default;

private:
    std::string value_;
};

struct ProjectIdTag;
struct ClipIdTag;
struct ExportRecordIdTag;

using ProjectId = PersistentId<ProjectIdTag>;
using ClipId = PersistentId<ClipIdTag>;
using ExportRecordId = PersistentId<ExportRecordIdTag>;

} // namespace dvs::domain
