#pragma once

#include "dvs/application/FrameHandle.h"
#include "dvs/domain/Identifiers.h"

#include <optional>

namespace dvs::application {

enum class ActiveFrameSource {
    Direct,
    Proxy,
};

// A pair is created atomically. The private constructor and non-null FrameHandle factory keep
// callers from representing a renderable pair with a missing A or B side.
class FramePair final {
public:
    [[nodiscard]] static std::optional<FramePair> create(domain::FrameId frameId,
                                                         domain::MediaTime canonicalTime,
                                                         const FrameHandle& frameA,
                                                         domain::MediaTime presentationTimeA,
                                                         const FrameHandle& frameB,
                                                         domain::MediaTime presentationTimeB,
                                                         ActiveFrameSource source) noexcept;

    [[nodiscard]] const domain::FrameId& frameId() const noexcept;
    [[nodiscard]] const domain::MediaTime& canonicalTime() const noexcept;
    [[nodiscard]] const FrameHandle& frameA() const noexcept;
    [[nodiscard]] const domain::MediaTime& presentationTimeA() const noexcept;
    [[nodiscard]] const FrameHandle& frameB() const noexcept;
    [[nodiscard]] const domain::MediaTime& presentationTimeB() const noexcept;
    [[nodiscard]] ActiveFrameSource source() const noexcept;

private:
    FramePair(domain::FrameId frameId,
              domain::MediaTime canonicalTime,
              const FrameHandle& frameA,
              domain::MediaTime presentationTimeA,
              const FrameHandle& frameB,
              domain::MediaTime presentationTimeB,
              ActiveFrameSource source) noexcept;

    domain::FrameId frameId_;
    domain::MediaTime canonicalTime_;
    FrameHandle frameA_;
    domain::MediaTime presentationTimeA_;
    FrameHandle frameB_;
    domain::MediaTime presentationTimeB_;
    ActiveFrameSource source_;
};

inline std::optional<FramePair> FramePair::create(const domain::FrameId frameId,
                                                  const domain::MediaTime canonicalTime,
                                                  const FrameHandle& frameA,
                                                  const domain::MediaTime presentationTimeA,
                                                  const FrameHandle& frameB,
                                                  const domain::MediaTime presentationTimeB,
                                                  const ActiveFrameSource source) noexcept {
    if (!frameId.isValid() || canonicalTime.microseconds() < 0 || !frameA.isValid() ||
        !frameB.isValid()) {
        return std::nullopt;
    }

    return FramePair{
        frameId,
        canonicalTime,
        frameA,
        presentationTimeA,
        frameB,
        presentationTimeB,
        source,
    };
}

inline FramePair::FramePair(const domain::FrameId frameId,
                            const domain::MediaTime canonicalTime,
                            const FrameHandle& frameA,
                            const domain::MediaTime presentationTimeA,
                            const FrameHandle& frameB,
                            const domain::MediaTime presentationTimeB,
                            const ActiveFrameSource source) noexcept
    : frameId_(frameId), canonicalTime_(canonicalTime), frameA_(frameA),
      presentationTimeA_(presentationTimeA), frameB_(frameB), presentationTimeB_(presentationTimeB),
      source_(source) {}

inline const domain::FrameId& FramePair::frameId() const noexcept {
    return frameId_;
}

inline const domain::MediaTime& FramePair::canonicalTime() const noexcept {
    return canonicalTime_;
}

inline const FrameHandle& FramePair::frameA() const noexcept {
    return frameA_;
}

inline const domain::MediaTime& FramePair::presentationTimeA() const noexcept {
    return presentationTimeA_;
}

inline const FrameHandle& FramePair::frameB() const noexcept {
    return frameB_;
}

inline const domain::MediaTime& FramePair::presentationTimeB() const noexcept {
    return presentationTimeB_;
}

inline ActiveFrameSource FramePair::source() const noexcept {
    return source_;
}

} // namespace dvs::application
