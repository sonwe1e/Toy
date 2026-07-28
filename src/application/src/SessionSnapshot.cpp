#include "dvs/application/SessionSnapshot.h"

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

    switch (sessionState) {
    case domain::SessionState::kEmpty:
        return playbackState == domain::PlaybackState::kPaused && !activeFrameSource.has_value() &&
               !displayedFrame.has_value() && !requestedFrame.has_value() &&
               canonicalFrameCount == 0U;
    case domain::SessionState::kLoading:
        return !activeFrameSource.has_value() && !displayedFrame.has_value();
    case domain::SessionState::kReady:
        return activeFrameSource.has_value() && canonicalFrameCount != 0U;
    case domain::SessionState::kInvalid:
    case domain::SessionState::kError:
        return playbackState == domain::PlaybackState::kPaused && !activeFrameSource.has_value();
    }
    return false;
}

} // namespace dvs::application
