#pragma once

#include "dvs/application/FrameSet.h"
#include "dvs/application/RequestContext.h"
#include "dvs/domain/MediaError.h"

#include <cstdint>
#include <optional>

namespace dvs::application {

// A complete immutable UI-facing state. Frame resources stay on IRenderChannel; this snapshot
// exposes only frame identities and media state so views cannot observe a partial A/B update.
struct SessionSnapshot final {
    domain::SessionId sessionId{0};
    domain::SessionEpoch sessionEpoch{0};
    domain::PlaybackGeneration playbackGeneration{0};
    domain::DeviceGeneration deviceGeneration{0};
    bool graphicsReady = false;
    domain::SessionState sessionState = domain::SessionState::kEmpty;
    domain::PlaybackState playbackState = domain::PlaybackState::kPaused;
    std::optional<domain::FrameId> displayedFrame;
    std::optional<domain::FrameId> requestedFrame;
    std::uint64_t canonicalFrameCount = 0;
    std::optional<domain::MediaError> lastError;

    [[nodiscard]] bool isConsistent() const noexcept;
};

} // namespace dvs::application
