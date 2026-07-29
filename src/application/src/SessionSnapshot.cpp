#include "dvs/application/SessionSnapshot.h"

#include <cstddef>

namespace dvs::application {

bool SessionSnapshot::isConsistent() const noexcept {
    // Device availability is independent of session state: it may become ready before opening a
    // pair and may be revoked after a pair has been presented. The coordinator gates media
    // commands on graphicsReady while these structural snapshot checks remain valid during both
    // transitions.
    const auto validFrame = [this](const std::optional<domain::FrameId>& frame) {
        return !frame.has_value() ||
               (canonicalFrameCount != 0U && frame->isValid() &&
                static_cast<std::uint64_t>(frame->value()) < canonicalFrameCount);
    };
    if (!validFrame(displayedFrame) || !validFrame(requestedFrame)) {
        return false;
    }
    for (std::size_t index = 0U; index < presentedSources.size(); ++index) {
        const PresentedSourceState& source = presentedSources[index];
        if ((source.matchKind == FrameMatchKind::Missing) != !source.sourceFrameId.has_value() ||
            (source.sourceFrameId.has_value() && !source.sourceFrameId->isValid())) {
            return false;
        }
        for (std::size_t other = index + 1U; other < presentedSources.size(); ++other) {
            if (presentedSources[other].sourceId == source.sourceId) {
                return false;
            }
        }
    }
    if (!displayedFrame.has_value() && !presentedSources.empty()) {
        return false;
    }

    switch (sessionState) {
    case domain::SessionState::kEmpty:
        return playbackState == domain::PlaybackState::kPaused && !displayedFrame.has_value() &&
               !requestedFrame.has_value() && canonicalFrameCount == 0U;
    case domain::SessionState::kLoading:
        return !displayedFrame.has_value();
    case domain::SessionState::kReady:
        return canonicalFrameCount != 0U;
    case domain::SessionState::kInvalid:
    case domain::SessionState::kError:
        return playbackState == domain::PlaybackState::kPaused;
    }
    return false;
}

} // namespace dvs::application
