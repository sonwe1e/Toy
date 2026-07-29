#pragma once

#include "dvs/application/Alignment.h"
#include "dvs/application/RequestContext.h"
#include "dvs/domain/ComparisonSource.h"
#include "dvs/domain/MediaDescriptor.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace dvs::application {

// Commands are immutable values submitted to the coordinator. The caller supplies a context from
// its last snapshot so commands queued for an older source epoch are rejected deterministically.

// One path the UI/controller wants compared. The coordinator owns probing and set validation, so
// unprobed paths never masquerade as media descriptors at this boundary.
struct OpenComparisonSource final {
    std::filesystem::path path;
    domain::ComparisonRole role = domain::ComparisonRole::kPrediction;
    std::string displayName;
};

// Opens 2-3 sources in one atomic session operation. Source ids are assigned in submission
// order (0, 1, 2).
struct OpenComparisonCommand final {
    CommandContext context;
    std::vector<OpenComparisonSource> sources;
};

// Direct descriptor open bypasses probing (CLI diagnostics and tests). Descriptors must already
// carry unique source ids; the coordinator validates the set exactly like a probed open.
struct OpenDirectComparisonCommand final {
    CommandContext context;
    std::vector<domain::ComparisonSource> sources;
};

struct SeekFrameCommand final {
    CommandContext context;
    domain::FrameId frameId;
};

struct StepFramesCommand final {
    CommandContext context;
    std::int64_t delta = 0;
};

struct FirstFrameCommand final {
    CommandContext context;
};

struct LastFrameCommand final {
    CommandContext context;
};

struct PlayCommand final {
    CommandContext context;
};

struct PauseCommand final {
    CommandContext context;
};

// Applies one explicit global frame offset per named source without reopening decoders. Omitted
// sources reset to strict-index offset zero. The canonical source must remain at zero.
struct SetAlignmentOffsetsCommand final {
    CommandContext context;
    std::vector<SourceFrameOffset> sourceOffsets;
};

struct EstimateAlignmentCommand final {
    CommandContext context;
};

struct AnalyzeSequenceAlignmentCommand final {
    CommandContext context;
};

struct CancelAlignmentAnalysisCommand final {
    CommandContext context;
};

struct SetManualAlignmentAnchorCommand final {
    CommandContext context;
    domain::SourceId sourceId = 0;
    ManualAlignmentAnchor anchor;
};

struct ClearManualAlignmentAnchorsCommand final {
    CommandContext context;
};

struct CloseSessionCommand final {
    CommandContext context;
};

using PlaybackCommand = std::variant<OpenComparisonCommand,
                                     OpenDirectComparisonCommand,
                                     SeekFrameCommand,
                                     StepFramesCommand,
                                     FirstFrameCommand,
                                     LastFrameCommand,
                                     PlayCommand,
                                     PauseCommand,
                                     SetAlignmentOffsetsCommand,
                                     EstimateAlignmentCommand,
                                     AnalyzeSequenceAlignmentCommand,
                                     CancelAlignmentAnalysisCommand,
                                     SetManualAlignmentAnchorCommand,
                                     ClearManualAlignmentAnchorsCommand,
                                     CloseSessionCommand>;

[[nodiscard]] inline const CommandContext& commandContext(const PlaybackCommand& command) noexcept {
    return std::visit([](const auto& value) -> const CommandContext& { return value.context; },
                      command);
}

} // namespace dvs::application
