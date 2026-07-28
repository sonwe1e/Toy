#pragma once

#include "dvs/domain/RationalRate.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace dvs::domain {

enum class FrameCountOrigin {
    kReported,
    kEstimated,
    kIndexed,
};

enum class TimingConfidence {
    kDeclaredCfr,
    kVerifiedCfr,
    kVariableFrameRate,
};

struct FrameCountInfo final {
    std::int64_t value = 0;
    FrameCountOrigin origin = FrameCountOrigin::kEstimated;

    [[nodiscard]] bool isValid() const noexcept;
};

struct MediaExtent final {
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return width != 0 && height != 0;
    }

    [[nodiscard]] constexpr bool operator==(const MediaExtent&) const noexcept = default;
};

struct DecodeCapabilities final {
    bool softwareDecode = false;
    bool d3d11VaDecode = false;
};

// The media adapter normalizes FFmpeg colour declarations before they reach the domain. v1 only
// accepts SDR 8-bit 4:2:0 material with a BT.601 or BT.709 matrix.
enum class ColorMatrix {
    kBt601,
    kBt709,
};

enum class ColorRange {
    kLimited,
    kFull,
};

struct ColorMetadata final {
    ColorMatrix matrix = ColorMatrix::kBt601;
    ColorRange range = ColorRange::kLimited;
    bool matrixInferred = false;

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return (matrix == ColorMatrix::kBt601 || matrix == ColorMatrix::kBt709) &&
               (range == ColorRange::kLimited || range == ColorRange::kFull);
    }
};

// File identity is captured by a platform/persistence adapter. It is optional while a project is
// being assembled, but schema-1 persistence requires a complete value for both selected sources.
struct SourceFileIdentity final {
    std::uint64_t byteSize = 0;
    std::int64_t modifiedUtcMilliseconds = 0;
    std::string fingerprintSha256;

    [[nodiscard]] bool isComplete() const noexcept;
};

// This is normalized adapter data, not a probing API. Filesystem access and FFmpeg field
// interpretation stay in media_ffmpeg before a descriptor crosses this boundary.
struct MediaDescriptor final {
    std::filesystem::path normalizedPath;
    MediaExtent extent;
    std::optional<RationalRate> frameRate;
    FrameCountInfo frameCount;
    MediaTime duration;
    std::string codecId;
    std::string pixelFormatId;
    std::uint8_t bitDepth = 0;
    ColorMetadata colorMetadata;
    DecodeCapabilities decodeCapabilities;
    TimingConfidence timingConfidence = TimingConfidence::kDeclaredCfr;
    std::optional<SourceFileIdentity> sourceIdentity;

    [[nodiscard]] bool isValid() const noexcept;
};

[[nodiscard]] Result<MediaDescriptor> validateMediaDescriptor(MediaDescriptor descriptor);

} // namespace dvs::domain
