#pragma once

#include "dvs/domain/Identifiers.h"

#include <cstdint>
#include <string>

namespace dvs::media {

enum class DecoderBackend {
    Software,
    D3d11Va,
};

struct DecoderBackendStatus final {
    domain::SourceId sourceId = 0U;
    DecoderBackend backend = DecoderBackend::Software;
    std::string fallbackReason;
    domain::DeviceGeneration deviceGeneration{0U};
    std::uint64_t completedDecodeCount = 0U;
    std::uint64_t cacheHitCount = 0U;
    std::uint64_t exactSeekCount = 0U;
    std::uint64_t totalDecodeMicroseconds = 0U;
    std::uint64_t maximumDecodeMicroseconds = 0U;

    [[nodiscard]] bool operator==(const DecoderBackendStatus&) const = default;
};

} // namespace dvs::media
