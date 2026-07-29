#include "FrameSetAssembler.h"

#include <algorithm>
#include <utility>

namespace dvs::media::internal {

FrameSetAssembler::FrameSetAssembler(const domain::FrameId canonicalFrameId,
                                     const domain::MediaTime canonicalTime,
                                     std::vector<domain::SourceId> sourceOrder)
    : canonicalFrameId_(canonicalFrameId), canonicalTime_(canonicalTime),
      sourceOrder_(std::move(sourceOrder)), slots_(sourceOrder_.size()) {}

bool FrameSetAssembler::complete(application::MappedSourceFrame entry) {
    const auto source = std::find(sourceOrder_.begin(), sourceOrder_.end(), entry.sourceId);
    if (source == sourceOrder_.end()) {
        return false;
    }
    const std::size_t slot = static_cast<std::size_t>(std::distance(sourceOrder_.begin(), source));
    if (slots_[slot].has_value()) {
        return false;
    }
    slots_[slot] = std::move(entry);
    return true;
}

std::size_t FrameSetAssembler::completedSlotCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        slots_.begin(), slots_.end(), [](const auto& slot) { return slot.has_value(); }));
}

std::optional<application::FrameSet> FrameSetAssembler::finish() {
    if (completedSlotCount() != slots_.size()) {
        return std::nullopt;
    }
    std::vector<application::MappedSourceFrame> entries;
    entries.reserve(slots_.size());
    for (auto& slot : slots_) {
        entries.push_back(std::move(*slot));
    }
    return application::FrameSet::create(canonicalFrameId_, canonicalTime_, std::move(entries));
}

} // namespace dvs::media::internal
