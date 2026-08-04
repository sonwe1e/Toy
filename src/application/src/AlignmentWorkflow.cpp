#include "AlignmentWorkflow.h"

#include <algorithm>
#include <cstddef>

namespace dvs::application::detail {
namespace {

constexpr float kLowAlignmentConfidence = 0.30F;
constexpr std::size_t kMaximumSnapshotAlignmentMarkers = 256U;

} // namespace

SequenceAlignmentSummary summarizeSequenceAlignment(const SequenceAlignmentResult& result) {
    SequenceAlignmentSummary summary{
        .sourceId = result.sourceId,
        .anomalyCount = result.anomalies.size(),
        .totalCost = result.totalCost,
        .meanMatchCost = result.meanMatchCost,
        .confidence = result.confidence,
        .autoApplicable = result.autoApplicable,
    };
    const std::size_t anomalyLimit =
        std::min(result.anomalies.size(), kMaximumSnapshotAlignmentMarkers);
    summary.anomalies.assign(result.anomalies.begin(),
                             result.anomalies.begin() + static_cast<std::ptrdiff_t>(anomalyLimit));
    const std::size_t segmentLimit =
        std::min(result.segments.size(), kMaximumSnapshotAlignmentMarkers);
    summary.segments.assign(result.segments.begin(),
                            result.segments.begin() + static_cast<std::ptrdiff_t>(segmentLimit));

    bool inRun = false;
    SequenceAlignmentLowConfidenceRun run;
    for (const SequenceAlignmentEntry& entry : result.entries) {
        const bool lowConfidence =
            entry.sourceFrameId.has_value() && entry.confidence < kLowAlignmentConfidence;
        if (lowConfidence && !inRun) {
            run = SequenceAlignmentLowConfidenceRun{
                .firstCanonicalFrame = entry.canonicalFrameId,
                .lastCanonicalFrame = entry.canonicalFrameId,
                .minimumConfidence = entry.confidence,
            };
            inRun = true;
        } else if (lowConfidence) {
            run.lastCanonicalFrame = entry.canonicalFrameId;
            run.minimumConfidence = std::min(run.minimumConfidence, entry.confidence);
        }
        if (inRun && !lowConfidence) {
            summary.lowConfidenceRuns.push_back(run);
            inRun = false;
            if (summary.lowConfidenceRuns.size() >= kMaximumSnapshotAlignmentMarkers) {
                break;
            }
        }
    }
    if (inRun && summary.lowConfidenceRuns.size() < kMaximumSnapshotAlignmentMarkers) {
        summary.lowConfidenceRuns.push_back(run);
    }
    if (!result.autoApplicable && summary.lowConfidenceRuns.empty() && !result.entries.empty()) {
        summary.lowConfidenceRuns.push_back(SequenceAlignmentLowConfidenceRun{
            .firstCanonicalFrame = result.entries.front().canonicalFrameId,
            .lastCanonicalFrame = result.entries.back().canonicalFrameId,
            .minimumConfidence = result.confidence,
        });
    }
    return summary;
}

} // namespace dvs::application::detail
