#include "dvs/domain/ExportPlan.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace dvs::domain {
namespace {

[[nodiscard]] MediaError exportError(const MediaErrorCode code, std::string technicalDetail) {
    return makeMediaError(code,
                          MediaOperation::kExportPlanBuild,
                          SourceRole::kExport,
                          false,
                          std::move(technicalDetail));
}

[[nodiscard]] bool roundUpEven(const std::uint64_t input, std::uint32_t* const output) noexcept {
    if (input == 0 || input > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    const std::uint64_t rounded = (input % 2U) == 0U ? input : input + 1U;
    if (rounded > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    *output = static_cast<std::uint32_t>(rounded);
    return true;
}

[[nodiscard]] Result<ExportOutputPlan> makeSeparateOutput(const SourceRole sourceRole,
                                                          const MediaExtent nativeExtent,
                                                          const FrameSpan frames) {
    std::uint32_t canvasWidth = 0;
    std::uint32_t canvasHeight = 0;
    if (!roundUpEven(nativeExtent.width, &canvasWidth) ||
        !roundUpEven(nativeExtent.height, &canvasHeight)) {
        return Result<ExportOutputPlan>::failure(
            exportError(MediaErrorCode::kInvalidExportGeometry,
                        "Native source dimensions cannot be padded evenly."));
    }

    SourceTrimOperation operation{
        .sourceRole = sourceRole,
        .frames = frames,
        .resetTimestamps = true,
        .nativeExtent = nativeExtent,
        .destination =
            PixelRect{
                .x = 0,
                .y = 0,
                .width = nativeExtent.width,
                .height = nativeExtent.height,
            },
    };
    ExportOutputPlan output{
        .canvas = MediaExtent{.width = canvasWidth, .height = canvasHeight},
        .operations = {std::move(operation)},
    };
    return Result<ExportOutputPlan>::success(std::move(output));
}

[[nodiscard]] Result<ExportOutputPlan>
makeSideBySideOutput(const MediaExtent extentA, const MediaExtent extentB, const FrameSpan frames) {
    const std::uint64_t unpaddedWidth =
        static_cast<std::uint64_t>(extentA.width) + static_cast<std::uint64_t>(extentB.width);
    const std::uint64_t unpaddedHeight = std::max(extentA.height, extentB.height);
    std::uint32_t canvasWidth = 0;
    std::uint32_t canvasHeight = 0;
    if (!roundUpEven(unpaddedWidth, &canvasWidth) || !roundUpEven(unpaddedHeight, &canvasHeight)) {
        return Result<ExportOutputPlan>::failure(exportError(
            MediaErrorCode::kInvalidExportGeometry, "Side-by-side canvas dimensions overflow."));
    }

    const auto verticalOffsetA = (canvasHeight - extentA.height) / 2U;
    const auto verticalOffsetB = (canvasHeight - extentB.height) / 2U;
    SourceTrimOperation operationA{
        .sourceRole = SourceRole::kA,
        .frames = frames,
        .resetTimestamps = true,
        .nativeExtent = extentA,
        .destination =
            PixelRect{
                .x = 0,
                .y = verticalOffsetA,
                .width = extentA.width,
                .height = extentA.height,
            },
    };
    SourceTrimOperation operationB{
        .sourceRole = SourceRole::kB,
        .frames = frames,
        .resetTimestamps = true,
        .nativeExtent = extentB,
        .destination =
            PixelRect{
                .x = extentA.width,
                .y = verticalOffsetB,
                .width = extentB.width,
                .height = extentB.height,
            },
    };
    ExportOutputPlan output{
        .canvas = MediaExtent{.width = canvasWidth, .height = canvasHeight},
        .operations = {std::move(operationA), std::move(operationB)},
    };
    return Result<ExportOutputPlan>::success(std::move(output));
}

} // namespace

Result<ExportPlan> ExportPlanBuilder::build(const Project& project,
                                            const std::span<const ClipId> clipIds,
                                            const ExportMode mode) const {
    if (clipIds.empty()) {
        return Result<ExportPlan>::failure(
            exportError(MediaErrorCode::kInvalidArgument,
                        "An export plan requires at least one selected clip."));
    }

    std::vector<ClipId> seenIds;
    seenIds.reserve(clipIds.size());
    std::vector<PlannedClipExport> plannedClips;
    plannedClips.reserve(clipIds.size());
    for (const ClipId& requestedId : clipIds) {
        if (std::find(seenIds.begin(), seenIds.end(), requestedId) != seenIds.end()) {
            return Result<ExportPlan>::failure(
                exportError(MediaErrorCode::kDuplicateClipSelection,
                            "A clip may appear only once in an export request."));
        }
        seenIds.push_back(requestedId);

        const auto iterator =
            std::find_if(project.clips().begin(),
                         project.clips().end(),
                         [&requestedId](const Clip& clip) { return clip.id == requestedId; });
        if (iterator == project.clips().end()) {
            return Result<ExportPlan>::failure(exportError(
                MediaErrorCode::kClipNotFound, "Selected clip does not belong to this project."));
        }

        auto frames = iterator->range.toHalfOpen();
        if (!frames) {
            return Result<ExportPlan>::failure(frames.error());
        }

        PlannedClipExport plannedClip{
            .clipId = iterator->id,
            .inclusiveRange = iterator->range,
            .outputs = {},
        };
        const MediaExtent extentA = project.sources().sourceA().extent;
        const MediaExtent extentB = project.sources().sourceB().extent;
        switch (mode) {
        case ExportMode::kSeparateAB: {
            auto outputA = makeSeparateOutput(SourceRole::kA, extentA, frames.value());
            if (!outputA) {
                return Result<ExportPlan>::failure(outputA.error());
            }
            auto outputB = makeSeparateOutput(SourceRole::kB, extentB, frames.value());
            if (!outputB) {
                return Result<ExportPlan>::failure(outputB.error());
            }
            plannedClip.outputs.push_back(std::move(outputA).value());
            plannedClip.outputs.push_back(std::move(outputB).value());
            break;
        }
        case ExportMode::kSideBySide: {
            auto output = makeSideBySideOutput(extentA, extentB, frames.value());
            if (!output) {
                return Result<ExportPlan>::failure(output.error());
            }
            plannedClip.outputs.push_back(std::move(output).value());
            break;
        }
        default:
            return Result<ExportPlan>::failure(
                exportError(MediaErrorCode::kInvalidExportMode, "Unknown export mode."));
        }
        plannedClips.push_back(std::move(plannedClip));
    }

    const auto& sourceRate = project.sources().canonicalRate();
    if (!sourceRate.has_value()) {
        return Result<ExportPlan>::failure(
            exportError(MediaErrorCode::kInvalidCfrTiming,
                        "Variable-frame-rate sources cannot be exported; a constant frame rate is"
                        " required."));
    }

    ExportPlan plan{
        .mode = mode,
        .rate = *sourceRate,
        .clips = std::move(plannedClips),
    };
    return Result<ExportPlan>::success(std::move(plan));
}

} // namespace dvs::domain
