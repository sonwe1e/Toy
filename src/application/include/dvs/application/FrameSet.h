#pragma once

#include "dvs/application/Alignment.h"
#include "dvs/application/FrameHandle.h"
#include "dvs/domain/ComparisonSource.h"
#include "dvs/domain/Identifiers.h"

#include <algorithm>
#include <optional>
#include <span>
#include <vector>

namespace dvs::application {

// One source's contribution to a canonical frame position. A missing entry carries neither a
// frame nor a source frame id; it is never silently replaced by a neighbor frame.
struct MappedSourceFrame final {
    domain::SourceId sourceId = 0;
    std::optional<domain::FrameId> sourceFrameId;
    std::optional<FrameHandle> frame;
    domain::MediaTime presentationTime{0};
    FrameMatchKind matchKind = FrameMatchKind::ExactIndex;
    float alignmentConfidence = 1.0F;

    [[nodiscard]] bool hasFrame() const noexcept {
        return frame.has_value();
    }
};

// Every source's frames for one canonical frame position, published atomically. The set always
// carries one entry per loaded source; an incomplete set is still published with explicit
// Missing entries instead of being dropped or partially advanced.
class FrameSet final {
public:
    [[nodiscard]] static std::optional<FrameSet>
    create(domain::FrameId canonicalFrameId,
           domain::MediaTime canonicalTime,
           std::vector<MappedSourceFrame> sources) noexcept;

    [[nodiscard]] const domain::FrameId& canonicalFrameId() const noexcept;
    [[nodiscard]] const domain::MediaTime& canonicalTime() const noexcept;
    [[nodiscard]] std::span<const MappedSourceFrame> sources() const noexcept;
    [[nodiscard]] const MappedSourceFrame* find(domain::SourceId sourceId) const noexcept;
    [[nodiscard]] bool isComplete() const noexcept;

private:
    FrameSet(domain::FrameId canonicalFrameId,
             domain::MediaTime canonicalTime,
             std::vector<MappedSourceFrame> sources) noexcept;

    domain::FrameId canonicalFrameId_;
    domain::MediaTime canonicalTime_;
    std::vector<MappedSourceFrame> sources_;
};

inline std::optional<FrameSet> FrameSet::create(const domain::FrameId canonicalFrameId,
                                                const domain::MediaTime canonicalTime,
                                                std::vector<MappedSourceFrame> sources) noexcept {
    if (!canonicalFrameId.isValid() || canonicalTime.microseconds() < 0 || sources.size() < 2U) {
        return std::nullopt;
    }

    for (std::size_t index = 0; index < sources.size(); ++index) {
        const MappedSourceFrame& entry = sources[index];
        for (std::size_t other = index + 1; other < sources.size(); ++other) {
            if (sources[other].sourceId == entry.sourceId) {
                return std::nullopt;
            }
        }
        if (entry.hasFrame() != entry.sourceFrameId.has_value()) {
            return std::nullopt;
        }
        if (entry.hasFrame()) {
            if (!entry.frame->isValid() || entry.presentationTime.microseconds() < 0 ||
                !entry.sourceFrameId->isValid() || entry.matchKind == FrameMatchKind::Missing) {
                return std::nullopt;
            }
        } else if (entry.matchKind != FrameMatchKind::Missing) {
            return std::nullopt;
        }
    }

    return FrameSet{canonicalFrameId, canonicalTime, std::move(sources)};
}

inline FrameSet::FrameSet(const domain::FrameId canonicalFrameId,
                          const domain::MediaTime canonicalTime,
                          std::vector<MappedSourceFrame> sources) noexcept
    : canonicalFrameId_(canonicalFrameId), canonicalTime_(canonicalTime),
      sources_(std::move(sources)) {}

inline const domain::FrameId& FrameSet::canonicalFrameId() const noexcept {
    return canonicalFrameId_;
}

inline const domain::MediaTime& FrameSet::canonicalTime() const noexcept {
    return canonicalTime_;
}

inline std::span<const MappedSourceFrame> FrameSet::sources() const noexcept {
    return sources_;
}

inline const MappedSourceFrame* FrameSet::find(const domain::SourceId sourceId) const noexcept {
    const auto iterator =
        std::find_if(sources_.begin(), sources_.end(), [sourceId](const MappedSourceFrame& entry) {
            return entry.sourceId == sourceId;
        });
    if (iterator == sources_.end()) {
        return nullptr;
    }
    return &*iterator;
}

inline bool FrameSet::isComplete() const noexcept {
    return std::all_of(sources_.begin(), sources_.end(), [](const MappedSourceFrame& entry) {
        return entry.hasFrame();
    });
}

} // namespace dvs::application
