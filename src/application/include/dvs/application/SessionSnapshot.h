#pragma once

#include "dvs/application/FrameSet.h"
#include "dvs/application/RequestContext.h"
#include "dvs/domain/MediaError.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace dvs::application {

struct PresentedSourceState final {
    domain::SourceId sourceId = 0;
    std::optional<domain::FrameId> sourceFrameId;
    FrameMatchKind matchKind = FrameMatchKind::ExactIndex;
    float alignmentConfidence = 1.0F;
    std::optional<MissingReason> missingReason;

    [[nodiscard]] bool operator==(const PresentedSourceState&) const = default;
};

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
    std::vector<PresentedSourceState> presentedSources;
    std::vector<GlobalOffsetEstimate> alignmentEstimates;
    std::uint64_t alignmentRevision = 0U;
    std::vector<SequenceAlignmentSummary> sequenceAlignments;
    std::optional<AlignmentAnalysisJobId> alignmentAnalysisJobId;
    std::optional<AlignmentAnalysisKind> alignmentAnalysisKind;
    std::uint64_t alignmentAnalysisCompletedFrames = 0U;
    std::uint64_t alignmentAnalysisTotalFrames = 0U;
    std::vector<SourceAlignmentAnchors> manualAlignmentAnchors;
    std::vector<domain::MediaErrorCode> compatibilityWarnings;
    std::optional<domain::MediaError> lastError;

    [[nodiscard]] bool isConsistent() const noexcept;
};

} // namespace dvs::application
