#include "dvs/application/Alignment.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace dvs::application {
namespace {

[[nodiscard]] float normalizedLumaDifference(const FrameLumaSignature& left,
                                             const FrameLumaSignature& right) noexcept {
    std::uint64_t total = 0U;
    for (std::size_t index = 0U; index < left.luma.size(); ++index) {
        total += static_cast<std::uint64_t>(
            std::abs(static_cast<int>(left.luma[index]) - static_cast<int>(right.luma[index])));
    }
    return static_cast<float>(total) /
           static_cast<float>(left.luma.size() * static_cast<std::size_t>(255U));
}

[[nodiscard]] float structuralDistance(const FrameLumaSignature& left,
                                       const FrameLumaSignature& right) noexcept {
    constexpr double kC1 = 6.5025;
    constexpr double kC2 = 58.5225;
    const double inverseCount = 1.0 / static_cast<double>(left.luma.size());
    const double leftMean = std::accumulate(left.luma.begin(), left.luma.end(), 0.0) * inverseCount;
    const double rightMean =
        std::accumulate(right.luma.begin(), right.luma.end(), 0.0) * inverseCount;

    double leftVariance = 0.0;
    double rightVariance = 0.0;
    double covariance = 0.0;
    for (std::size_t index = 0U; index < left.luma.size(); ++index) {
        const double centeredLeft = static_cast<double>(left.luma[index]) - leftMean;
        const double centeredRight = static_cast<double>(right.luma[index]) - rightMean;
        leftVariance += centeredLeft * centeredLeft;
        rightVariance += centeredRight * centeredRight;
        covariance += centeredLeft * centeredRight;
    }
    leftVariance *= inverseCount;
    rightVariance *= inverseCount;
    covariance *= inverseCount;
    const double numerator = ((2.0 * leftMean * rightMean) + kC1) * ((2.0 * covariance) + kC2);
    const double denominator = ((leftMean * leftMean) + (rightMean * rightMean) + kC1) *
                               (leftVariance + rightVariance + kC2);
    if (denominator <= 0.0) {
        return 1.0F;
    }
    const double similarity = std::clamp(numerator / denominator, 0.0, 1.0);
    return static_cast<float>(1.0 - similarity);
}

[[nodiscard]] float gradientDistance(const FrameLumaSignature& left,
                                     const FrameLumaSignature& right) noexcept {
    std::uint64_t total = 0U;
    std::size_t edgeCount = 0U;
    for (std::size_t y = 0U; y < kAlignmentSignatureHeight; ++y) {
        for (std::size_t x = 0U; x < kAlignmentSignatureWidth; ++x) {
            const std::size_t index = (y * kAlignmentSignatureWidth) + x;
            if (x + 1U < kAlignmentSignatureWidth) {
                const int leftGradient =
                    static_cast<int>(left.luma[index + 1U]) - static_cast<int>(left.luma[index]);
                const int rightGradient =
                    static_cast<int>(right.luma[index + 1U]) - static_cast<int>(right.luma[index]);
                total += static_cast<std::uint64_t>(std::abs(leftGradient - rightGradient));
                ++edgeCount;
            }
            if (y + 1U < kAlignmentSignatureHeight) {
                const std::size_t below = index + kAlignmentSignatureWidth;
                const int leftGradient =
                    static_cast<int>(left.luma[below]) - static_cast<int>(left.luma[index]);
                const int rightGradient =
                    static_cast<int>(right.luma[below]) - static_cast<int>(right.luma[index]);
                total += static_cast<std::uint64_t>(std::abs(leftGradient - rightGradient));
                ++edgeCount;
            }
        }
    }
    return edgeCount == 0U ? 1.0F
                           : static_cast<float>(total) /
                                 static_cast<float>(edgeCount * static_cast<std::size_t>(510U));
}

[[nodiscard]] float signatureDistance(const FrameLumaSignature& left,
                                      const FrameLumaSignature& right) noexcept {
    const float hashDistance =
        static_cast<float>(std::popcount(left.perceptualHash ^ right.perceptualHash)) / 64.0F;
    return (0.50F * structuralDistance(left, right)) + (0.35F * gradientDistance(left, right)) +
           (0.10F * hashDistance) + (0.05F * normalizedLumaDifference(left, right));
}

[[nodiscard]] const FrameLumaSignature*
findSignature(const std::span<const FrameLumaSignature> signatures,
              const std::int64_t frameId) noexcept {
    const auto found =
        std::find_if(signatures.begin(), signatures.end(), [frameId](const auto& signature) {
            return signature.frameId.value() == frameId;
        });
    return found == signatures.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<const FrameLumaSignature*>
selectEvidence(const std::span<const FrameLumaSignature> reference,
               const std::size_t selectedCount) {
    struct RankedSignature final {
        const FrameLumaSignature* signature = nullptr;
        float activity = 0.0F;
    };
    std::vector<RankedSignature> ranked;
    ranked.reserve(reference.size());
    for (std::size_t index = 0U; index < reference.size(); ++index) {
        float activity = gradientDistance(reference[index],
                                          FrameLumaSignature{
                                              .frameId = reference[index].frameId,
                                          });
        if (index > 0U) {
            activity += normalizedLumaDifference(reference[index - 1U], reference[index]);
        }
        if (index + 1U < reference.size()) {
            activity += normalizedLumaDifference(reference[index], reference[index + 1U]);
        }
        ranked.push_back(RankedSignature{
            .signature = &reference[index],
            .activity = activity,
        });
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        return left.activity > right.activity;
    });
    const std::size_t count = std::min(selectedCount, ranked.size());
    std::vector<const FrameLumaSignature*> selected;
    selected.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        selected.push_back(ranked[index].signature);
    }
    return selected;
}

[[nodiscard]] float median(std::vector<float> values) {
    const std::size_t middle = values.size() / 2U;
    std::nth_element(
        values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    if ((values.size() % 2U) != 0U) {
        return values[middle];
    }
    const float upper = values[middle];
    const float lower =
        *std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
    return (lower + upper) * 0.5F;
}

} // namespace

bool FrameLumaSignature::isValid() const noexcept {
    return frameId.isValid();
}

bool GlobalOffsetEstimationOptions::isValid() const noexcept {
    return minimumOffset <= maximumOffset && minimumOffset >= -64 && maximumOffset <= 64 &&
           selectedSampleCount > 0U && selectedSampleCount <= 64U && minimumEvidence > 0U &&
           minimumEvidence <= selectedSampleCount && std::isfinite(minimumConfidence) &&
           minimumConfidence >= 0.0F && minimumConfidence <= 1.0F &&
           std::isfinite(maximumBestCost) && maximumBestCost >= 0.0F && maximumBestCost <= 1.0F;
}

FrameLumaSignature makeFrameLumaSignature(
    const domain::FrameId frameId,
    const std::span<const std::uint8_t, kAlignmentSignaturePixels> luma) noexcept {
    FrameLumaSignature signature{
        .frameId = frameId,
    };
    std::copy(luma.begin(), luma.end(), signature.luma.begin());

    std::uint64_t sum = 0U;
    for (std::size_t y = 0U; y < 8U; ++y) {
        for (std::size_t x = 0U; x < 8U; ++x) {
            const std::size_t sourceX = (x * kAlignmentSignatureWidth) / 8U;
            const std::size_t sourceY = (y * kAlignmentSignatureHeight) / 8U;
            sum += signature.luma[(sourceY * kAlignmentSignatureWidth) + sourceX];
        }
    }
    const std::uint8_t mean = static_cast<std::uint8_t>(sum / 64U);
    for (std::size_t y = 0U; y < 8U; ++y) {
        for (std::size_t x = 0U; x < 8U; ++x) {
            const std::size_t sourceX = (x * kAlignmentSignatureWidth) / 8U;
            const std::size_t sourceY = (y * kAlignmentSignatureHeight) / 8U;
            const std::size_t bit = (y * 8U) + x;
            if (signature.luma[(sourceY * kAlignmentSignatureWidth) + sourceX] >= mean) {
                signature.perceptualHash |= std::uint64_t{1} << bit;
            }
        }
    }
    return signature;
}

std::optional<GlobalOffsetEstimate>
estimateGlobalOffset(const domain::SourceId targetSourceId,
                     const std::span<const FrameLumaSignature> reference,
                     const std::span<const FrameLumaSignature> target,
                     const GlobalOffsetEstimationOptions& options) noexcept {
    try {
        if (!options.isValid() || reference.size() < options.minimumEvidence ||
            target.size() < options.minimumEvidence ||
            std::any_of(reference.begin(),
                        reference.end(),
                        [](const auto& value) { return !value.isValid(); }) ||
            std::any_of(
                target.begin(), target.end(), [](const auto& value) { return !value.isValid(); })) {
            return std::nullopt;
        }

        const std::vector<const FrameLumaSignature*> selected =
            selectEvidence(reference, options.selectedSampleCount);
        struct Candidate final {
            std::int64_t offset = 0;
            float cost = 1.0F;
            std::size_t evidence = 0U;
        };
        std::vector<Candidate> candidates;
        for (std::int64_t offset = options.minimumOffset; offset <= options.maximumOffset;
             ++offset) {
            std::vector<float> costs;
            costs.reserve(selected.size());
            for (const FrameLumaSignature* const referenceSignature : selected) {
                const std::int64_t referenceFrame = referenceSignature->frameId.value();
                const bool underflow = offset < 0 && offset < -referenceFrame;
                const bool overflow =
                    offset > 0 &&
                    referenceFrame > (std::numeric_limits<std::int64_t>::max)() - offset;
                if (underflow || overflow) {
                    continue;
                }
                const FrameLumaSignature* const targetSignature =
                    findSignature(target, referenceFrame + offset);
                if (targetSignature != nullptr) {
                    costs.push_back(signatureDistance(*referenceSignature, *targetSignature));
                }
            }
            if (costs.size() >= options.minimumEvidence) {
                const std::size_t evidence = costs.size();
                candidates.push_back(Candidate{
                    .offset = offset,
                    .cost = median(std::move(costs)),
                    .evidence = evidence,
                });
            }
            if (offset == options.maximumOffset) {
                break;
            }
        }
        if (candidates.empty()) {
            return std::nullopt;
        }
        std::stable_sort(
            candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
                if (left.cost != right.cost) {
                    return left.cost < right.cost;
                }
                return std::abs(left.offset) < std::abs(right.offset);
            });
        const Candidate& best = candidates.front();
        const float runnerUp = candidates.size() > 1U ? candidates[1U].cost : 1.0F;
        const float denominator = std::max(runnerUp, 0.000001F);
        const float confidence = runnerUp <= best.cost
                                     ? 0.0F
                                     : std::clamp((runnerUp - best.cost) / denominator, 0.0F, 1.0F);
        return GlobalOffsetEstimate{
            .sourceId = targetSourceId,
            .bestOffset = best.offset,
            .bestCost = best.cost,
            .runnerUpCost = runnerUp,
            .confidence = confidence,
            .evidenceCount = best.evidence,
            .autoApplicable = best.evidence >= options.minimumEvidence &&
                              best.cost <= options.maximumBestCost &&
                              confidence >= options.minimumConfidence,
        };
    } catch (...) {
        return std::nullopt;
    }
}

bool SequenceAlignmentOptions::isValid() const noexcept {
    return bandWidth > 0U && bandWidth <= 64U &&
           expectedOffset >= -static_cast<std::int64_t>(bandWidth) &&
           expectedOffset <= static_cast<std::int64_t>(bandWidth) && std::isfinite(gapPenalty) &&
           gapPenalty > 0.0F && gapPenalty <= 1.0F && std::isfinite(duplicateDistance) &&
           duplicateDistance >= 0.0F && duplicateDistance <= 1.0F &&
           std::isfinite(minimumConfidence) && minimumConfidence >= 0.0F &&
           minimumConfidence <= 1.0F && std::isfinite(maximumMeanMatchCost) &&
           maximumMeanMatchCost >= 0.0F && maximumMeanMatchCost <= 1.0F;
}

std::optional<SequenceAlignmentResult>
alignFrameSequences(const domain::SourceId targetSourceId,
                    const std::span<const FrameLumaSignature> reference,
                    const std::span<const FrameLumaSignature> target,
                    const SequenceAlignmentOptions& options) noexcept {
    try {
        if (!options.isValid() || reference.empty() || target.empty() ||
            reference.size() >
                static_cast<std::size_t>((std::numeric_limits<std::int64_t>::max)()) ||
            target.size() > static_cast<std::size_t>((std::numeric_limits<std::int64_t>::max)())) {
            return std::nullopt;
        }
        const auto isContiguous = [](const std::span<const FrameLumaSignature> sequence) {
            for (std::size_t index = 0U; index < sequence.size(); ++index) {
                if (!sequence[index].isValid() ||
                    sequence[index].frameId.value() != static_cast<std::int64_t>(index)) {
                    return false;
                }
            }
            return true;
        };
        if (!isContiguous(reference) || !isContiguous(target)) {
            return std::nullopt;
        }

        enum class Step : std::uint8_t {
            None,
            Match,
            TargetMissing,
            TargetExtra,
        };
        struct Cell final {
            float cost = (std::numeric_limits<float>::infinity)();
            Step step = Step::None;
        };
        struct Row final {
            std::size_t firstColumn = 0U;
            std::vector<Cell> cells;

            [[nodiscard]] const Cell* find(const std::size_t column) const noexcept {
                if (column < firstColumn || column - firstColumn >= cells.size()) {
                    return nullptr;
                }
                return &cells[column - firstColumn];
            }

            [[nodiscard]] Cell* find(const std::size_t column) noexcept {
                return const_cast<Cell*>(std::as_const(*this).find(column));
            }
        };

        const std::int64_t targetCount = static_cast<std::int64_t>(target.size());
        const auto boundsFor = [&](const std::size_t referencePrefix) {
            const std::int64_t center =
                static_cast<std::int64_t>(referencePrefix) + options.expectedOffset;
            const std::int64_t first =
                std::max<std::int64_t>(0, center - static_cast<std::int64_t>(options.bandWidth));
            const std::int64_t last = std::min<std::int64_t>(
                targetCount, center + static_cast<std::int64_t>(options.bandWidth));
            return std::pair{static_cast<std::size_t>(first),
                             last < first ? static_cast<std::size_t>(first - 1)
                                          : static_cast<std::size_t>(last)};
        };

        std::vector<Row> rows(reference.size() + 1U);
        for (std::size_t rowIndex = 0U; rowIndex < rows.size(); ++rowIndex) {
            const auto [first, last] = boundsFor(rowIndex);
            if (last < first) {
                return std::nullopt;
            }
            rows[rowIndex].firstColumn = first;
            rows[rowIndex].cells.resize(last - first + 1U);
        }
        Cell* const start = rows.front().find(0U);
        if (start == nullptr) {
            return std::nullopt;
        }
        start->cost = 0.0F;

        for (std::size_t i = 0U; i < rows.size(); ++i) {
            Row& row = rows[i];
            for (std::size_t local = 0U; local < row.cells.size(); ++local) {
                const std::size_t j = row.firstColumn + local;
                if (i == 0U && j == 0U) {
                    continue;
                }
                Cell& cell = row.cells[local];
                if (i > 0U && j > 0U) {
                    if (const Cell* const diagonal = rows[i - 1U].find(j - 1U);
                        diagonal != nullptr && std::isfinite(diagonal->cost)) {
                        cell.cost =
                            diagonal->cost + signatureDistance(reference[i - 1U], target[j - 1U]);
                        cell.step = Step::Match;
                    }
                }
                if (i > 0U) {
                    if (const Cell* const above = rows[i - 1U].find(j);
                        above != nullptr && std::isfinite(above->cost)) {
                        const float cost = above->cost + options.gapPenalty;
                        if (cost < cell.cost) {
                            cell.cost = cost;
                            cell.step = Step::TargetMissing;
                        }
                    }
                }
                if (j > 0U) {
                    if (const Cell* const left = row.find(j - 1U);
                        left != nullptr && std::isfinite(left->cost)) {
                        const float cost = left->cost + options.gapPenalty;
                        if (cost < cell.cost) {
                            cell.cost = cost;
                            cell.step = Step::TargetExtra;
                        }
                    }
                }
            }
        }

        const Cell* const end = rows.back().find(target.size());
        if (end == nullptr || !std::isfinite(end->cost)) {
            return std::nullopt;
        }
        std::vector<std::optional<std::size_t>> mapping(reference.size());
        std::vector<SequenceAlignmentAnomaly> anomalies;
        std::size_t i = reference.size();
        std::size_t j = target.size();
        while (i != 0U || j != 0U) {
            const Cell* const cell = rows[i].find(j);
            if (cell == nullptr || cell->step == Step::None) {
                return std::nullopt;
            }
            switch (cell->step) {
            case Step::Match:
                mapping[i - 1U] = j - 1U;
                --i;
                --j;
                break;
            case Step::TargetMissing:
                mapping[i - 1U] = std::nullopt;
                anomalies.push_back(SequenceAlignmentAnomaly{
                    .kind = SequenceAlignmentAnomalyKind::TargetFrameMissing,
                    .canonicalFrameId = reference[i - 1U].frameId,
                    .sourceFrameId = std::nullopt,
                });
                --i;
                break;
            case Step::TargetExtra: {
                const std::size_t targetIndex = j - 1U;
                float previousDistance = 1.0F;
                float nextDistance = 1.0F;
                if (i > 0U) {
                    previousDistance = signatureDistance(reference[i - 1U], target[targetIndex]);
                }
                if (i < reference.size()) {
                    nextDistance = signatureDistance(reference[i], target[targetIndex]);
                }
                const bool duplicate =
                    std::min(previousDistance, nextDistance) <= options.duplicateDistance;
                std::optional<domain::FrameId> canonicalFrame;
                if (i > 0U && (i == reference.size() || previousDistance <= nextDistance)) {
                    canonicalFrame = reference[i - 1U].frameId;
                } else if (i < reference.size()) {
                    canonicalFrame = reference[i].frameId;
                }
                anomalies.push_back(SequenceAlignmentAnomaly{
                    .kind = duplicate ? SequenceAlignmentAnomalyKind::TargetFrameDuplicate
                                      : SequenceAlignmentAnomalyKind::TargetFrameExtra,
                    .canonicalFrameId = canonicalFrame,
                    .sourceFrameId = target[targetIndex].frameId,
                });
                --j;
                break;
            }
            case Step::None:
                return std::nullopt;
            }
        }
        std::reverse(anomalies.begin(), anomalies.end());

        SequenceAlignmentResult result{
            .sourceId = targetSourceId,
            .anomalies = std::move(anomalies),
            .totalCost = end->cost,
        };
        result.entries.reserve(reference.size());
        float totalMatchCost = 0.0F;
        float totalConfidence = 0.0F;
        std::size_t matchCount = 0U;
        for (std::size_t referenceIndex = 0U; referenceIndex < mapping.size(); ++referenceIndex) {
            if (!mapping[referenceIndex].has_value()) {
                result.entries.push_back(SequenceAlignmentEntry{
                    .canonicalFrameId = reference[referenceIndex].frameId,
                    .sourceFrameId = std::nullopt,
                    .matchKind = FrameMatchKind::Missing,
                    .confidence = 0.0F,
                });
                continue;
            }

            const std::size_t targetIndex = *mapping[referenceIndex];
            const float cost = signatureDistance(reference[referenceIndex], target[targetIndex]);
            float runnerUp = 1.0F;
            const std::int64_t center =
                static_cast<std::int64_t>(referenceIndex) + options.expectedOffset;
            const std::int64_t first =
                std::max<std::int64_t>(0, center - static_cast<std::int64_t>(options.bandWidth));
            const std::int64_t last = std::min<std::int64_t>(
                targetCount - 1, center + static_cast<std::int64_t>(options.bandWidth));
            for (std::int64_t candidate = first; candidate <= last; ++candidate) {
                if (static_cast<std::size_t>(candidate) == targetIndex) {
                    continue;
                }
                runnerUp = std::min(runnerUp,
                                    signatureDistance(reference[referenceIndex],
                                                      target[static_cast<std::size_t>(candidate)]));
            }
            const float margin =
                runnerUp <= cost
                    ? 0.0F
                    : std::clamp((runnerUp - cost) / std::max(runnerUp, 0.000001F), 0.0F, 1.0F);
            const float confidence = margin * (1.0F - cost);
            result.entries.push_back(SequenceAlignmentEntry{
                .canonicalFrameId = reference[referenceIndex].frameId,
                .sourceFrameId = target[targetIndex].frameId,
                .matchKind = FrameMatchKind::AutoAligned,
                .confidence = confidence,
            });
            totalMatchCost += cost;
            totalConfidence += confidence;
            ++matchCount;
        }
        if (matchCount == 0U) {
            return std::nullopt;
        }
        result.meanMatchCost = totalMatchCost / static_cast<float>(matchCount);
        result.confidence = totalConfidence / static_cast<float>(matchCount);
        result.autoApplicable = result.meanMatchCost <= options.maximumMeanMatchCost &&
                                result.confidence >= options.minimumConfidence;
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

bool SourceAlignmentAnchors::isValid() const noexcept {
    if (anchors.empty()) {
        return false;
    }
    for (std::size_t index = 0U; index < anchors.size(); ++index) {
        const ManualAlignmentAnchor& anchor = anchors[index];
        if (!anchor.canonicalFrameId.isValid() || !anchor.sourceFrameId.isValid()) {
            return false;
        }
        if (index > 0U && (anchors[index - 1U].canonicalFrameId >= anchor.canonicalFrameId ||
                           anchors[index - 1U].sourceFrameId > anchor.sourceFrameId)) {
            return false;
        }
    }
    return true;
}

std::optional<SourceFrameOffset> mapFrameWithAnchors(const SourceAlignmentAnchors& anchors,
                                                     const domain::FrameId canonicalFrame,
                                                     const std::int64_t sourceFrameCount) noexcept {
    if (!anchors.isValid() || !canonicalFrame.isValid() || sourceFrameCount <= 0) {
        return std::nullopt;
    }

    std::int64_t mappedFrame = -1;
    const ManualAlignmentAnchor& first = anchors.anchors.front();
    const ManualAlignmentAnchor& last = anchors.anchors.back();
    if (canonicalFrame <= first.canonicalFrameId) {
        const std::int64_t delta = canonicalFrame.value() - first.canonicalFrameId.value();
        mappedFrame =
            delta < -first.sourceFrameId.value() ? -1 : first.sourceFrameId.value() + delta;
    } else if (canonicalFrame >= last.canonicalFrameId) {
        const std::int64_t delta = canonicalFrame.value() - last.canonicalFrameId.value();
        mappedFrame =
            delta > (std::numeric_limits<std::int64_t>::max)() - last.sourceFrameId.value()
                ? -1
                : last.sourceFrameId.value() + delta;
    } else {
        const auto upper =
            std::upper_bound(anchors.anchors.begin(),
                             anchors.anchors.end(),
                             canonicalFrame,
                             [](const domain::FrameId frame, const ManualAlignmentAnchor& anchor) {
                                 return frame < anchor.canonicalFrameId;
                             });
        if (upper == anchors.anchors.begin() || upper == anchors.anchors.end()) {
            return std::nullopt;
        }
        const ManualAlignmentAnchor& right = *upper;
        const ManualAlignmentAnchor& left = *(upper - 1);
        const std::int64_t canonicalSpan =
            right.canonicalFrameId.value() - left.canonicalFrameId.value();
        const std::int64_t sourceSpan = right.sourceFrameId.value() - left.sourceFrameId.value();
        const std::int64_t position = canonicalFrame.value() - left.canonicalFrameId.value();
        if (sourceSpan != 0 && position > (std::numeric_limits<std::int64_t>::max)() / sourceSpan) {
            return std::nullopt;
        }
        const std::int64_t scaled = ((position * sourceSpan) + (canonicalSpan / 2)) / canonicalSpan;
        mappedFrame = left.sourceFrameId.value() + scaled;
    }

    if (mappedFrame < 0 || mappedFrame >= sourceFrameCount) {
        return SourceFrameOffset{
            .sourceId = anchors.sourceId,
            .frames = 0,
            .matchKind = FrameMatchKind::Missing,
            .confidence = 1.0F,
        };
    }
    return SourceFrameOffset{
        .sourceId = anchors.sourceId,
        .frames = mappedFrame - canonicalFrame.value(),
        .matchKind = FrameMatchKind::ManualAnchor,
        .confidence = 1.0F,
    };
}

} // namespace dvs::application
