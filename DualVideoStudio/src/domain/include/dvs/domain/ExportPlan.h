#pragma once

#include "dvs/domain/Project.h"

#include <cstdint>
#include <span>
#include <vector>

namespace dvs::domain {

enum class ExportMode {
    kSeparateAB,
    kSideBySide,
};

struct PixelRect final {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return width != 0 && height != 0;
    }
};

// One operation is intentionally source-neutral: adapters translate it into their own native
// pipeline, while this layer preserves inclusive frame semantics and native content geometry.
struct SourceTrimOperation final {
    SourceRole sourceRole = SourceRole::kNone;
    FrameSpan frames;
    bool resetTimestamps = true;
    MediaExtent nativeExtent;
    PixelRect destination;
};

struct ExportOutputPlan final {
    MediaExtent canvas;
    std::vector<SourceTrimOperation> operations;
};

struct PlannedClipExport final {
    ClipId clipId;
    FrameRange inclusiveRange;
    std::vector<ExportOutputPlan> outputs;
};

struct ExportPlan final {
    ExportMode mode = ExportMode::kSeparateAB;
    RationalRate rate;
    std::vector<PlannedClipExport> clips;
};

class ExportPlanBuilder final {
public:
    [[nodiscard]] Result<ExportPlan>
    build(const Project& project, std::span<const ClipId> clipIds, ExportMode mode) const;
};

} // namespace dvs::domain
