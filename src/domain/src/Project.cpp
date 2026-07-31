#include "dvs/domain/Project.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string_view>
#include <utility>

namespace dvs::domain {
namespace {

[[nodiscard]] Status projectFailure(const MediaErrorCode code,
                                    const MediaOperation operation,
                                    const std::optional<SourceId> source,
                                    std::string technicalDetail) {
    return Status::failure(
        makeMediaError(code, operation, source, false, std::move(technicalDetail)));
}

} // namespace

Project::Project(ProjectId id,
                 std::string displayName,
                 ValidatedComparisonSet sources,
                 std::optional<FrameId> inMark,
                 std::optional<FrameId> outMark,
                 const FrameId lastDisplayedFrame,
                 WorkspaceState workspaceState,
                 ProjectAlignmentState alignmentState,
                 ProjectViewState viewState)
    : id_(std::move(id)), displayName_(std::move(displayName)), sources_(std::move(sources)),
      inMark_(inMark), outMark_(outMark), lastDisplayedFrame_(lastDisplayedFrame),
      workspaceState_(std::move(workspaceState)), alignmentState_(std::move(alignmentState)),
      viewState_(viewState) {}

Result<Project>
Project::create(ProjectId id, std::string displayName, ValidatedComparisonSet sources) {
    if (!id.isValid()) {
        return Result<Project>::failure(makeMediaError(MediaErrorCode::kInvalidArgument,
                                                       MediaOperation::kProjectMutation,
                                                       std::nullopt,
                                                       false,
                                                       "Project ID must be non-empty."));
    }
    if (displayName.empty()) {
        return Result<Project>::failure(makeMediaError(MediaErrorCode::kInvalidArgument,
                                                       MediaOperation::kProjectMutation,
                                                       std::nullopt,
                                                       false,
                                                       "Project display name must be non-empty."));
    }
    const auto projectSources = sources.sources();
    for (std::size_t index = 0U; index < projectSources.size(); ++index) {
        if (projectSources[index].id != static_cast<SourceId>(index)) {
            return Result<Project>::failure(
                makeMediaError(MediaErrorCode::kInvalidArgument,
                               MediaOperation::kProjectMutation,
                               projectSources[index].id,
                               false,
                               "Project source IDs must be contiguous display-order indices."));
        }
    }

    ProjectViewState viewState;
    if (projectSources.size() == 1U) {
        viewState.layout = ProjectViewLayout::kSingle;
    } else {
        viewState.differenceEdge = std::array<SourceId, 2U>{
            projectSources[0].id,
            projectSources[1].id,
        };
    }
    return Result<Project>::success(Project{
        std::move(id),
        std::move(displayName),
        std::move(sources),
        std::nullopt,
        std::nullopt,
        FrameId{0},
        {},
        {},
        viewState,
    });
}

Result<Project> Project::restorePersisted(ProjectState persisted) {
    return rebuild(std::move(persisted));
}

Result<Project> Project::replaceSources(ValidatedComparisonSet sources) const {
    return rebuild(ProjectState{
        .id = id_,
        .displayName = displayName_,
        .sources = std::move(sources),
        .inMark = inMark_,
        .outMark = outMark_,
        .lastDisplayedFrame = lastDisplayedFrame_,
        .workspaceState = workspaceState_,
        .alignmentState = alignmentState_,
        .viewState = viewState_,
    });
}

Result<Project> Project::rebuild(ProjectState state) {
    auto created =
        create(std::move(state.id), std::move(state.displayName), std::move(state.sources));
    if (!created) {
        return Result<Project>::failure(created.error());
    }

    Project project = std::move(created).value();
    project.workspaceState_ = std::move(state.workspaceState);
    auto status = project.setLastDisplayedFrame(state.lastDisplayedFrame);
    if (!status) {
        return Result<Project>::failure(status.error());
    }
    if (state.inMark.has_value()) {
        status = project.setInMark(*state.inMark);
        if (!status) {
            return Result<Project>::failure(status.error());
        }
    }
    if (state.outMark.has_value()) {
        status = project.setOutMark(*state.outMark);
        if (!status) {
            return Result<Project>::failure(status.error());
        }
    }
    status = project.setAlignmentState(std::move(state.alignmentState));
    if (!status) {
        return Result<Project>::failure(status.error());
    }
    status = project.setViewState(state.viewState);
    if (!status) {
        return Result<Project>::failure(status.error());
    }

    return Result<Project>::success(std::move(project));
}

const ProjectId& Project::id() const noexcept {
    return id_;
}

const std::string& Project::displayName() const noexcept {
    return displayName_;
}

const ValidatedComparisonSet& Project::sources() const noexcept {
    return sources_;
}

const std::optional<FrameId>& Project::inMark() const noexcept {
    return inMark_;
}

const std::optional<FrameId>& Project::outMark() const noexcept {
    return outMark_;
}

FrameId Project::lastDisplayedFrame() const noexcept {
    return lastDisplayedFrame_;
}

const WorkspaceState& Project::workspaceState() const noexcept {
    return workspaceState_;
}

const ProjectAlignmentState& Project::alignmentState() const noexcept {
    return alignmentState_;
}

const ProjectViewState& Project::viewState() const noexcept {
    return viewState_;
}

Status Project::setInMark(const FrameId frameId) {
    auto status = validateFrame(frameId, MediaOperation::kProjectMutation);
    if (!status) {
        return status;
    }
    inMark_ = frameId;
    return Status::success();
}

Status Project::setOutMark(const FrameId frameId) {
    auto status = validateFrame(frameId, MediaOperation::kProjectMutation);
    if (!status) {
        return status;
    }
    outMark_ = frameId;
    return Status::success();
}

void Project::clearMarks() noexcept {
    inMark_.reset();
    outMark_.reset();
}

Status Project::setLastDisplayedFrame(const FrameId frameId) {
    auto status = validateFrame(frameId, MediaOperation::kProjectMutation);
    if (!status) {
        return status;
    }
    lastDisplayedFrame_ = frameId;
    return Status::success();
}

void Project::setWorkspaceState(WorkspaceState workspaceState) {
    workspaceState_ = std::move(workspaceState);
}

Status Project::setAlignmentState(ProjectAlignmentState alignmentState) {
    auto status = validateAlignmentState(alignmentState);
    if (!status) {
        return status;
    }
    alignmentState_ = std::move(alignmentState);
    return Status::success();
}

Status Project::setViewState(const ProjectViewState viewState) {
    auto status = validateViewState(viewState);
    if (!status) {
        return status;
    }
    viewState_ = viewState;
    return Status::success();
}

Status Project::validateFrame(const FrameId frameId, const MediaOperation operation) const {
    if (!frameId.isValid() || frameId.value() >= sources_.canonicalFrameCount()) {
        return projectFailure(MediaErrorCode::kFrameOutOfRange,
                              operation,
                              std::nullopt,
                              "Frame ID lies outside the canonical source timeline.");
    }
    return Status::success();
}

Status Project::validateAlignmentState(const ProjectAlignmentState& alignmentState) const {
    constexpr std::size_t kMaximumPersistedAnchors = 4'096U;
    constexpr std::size_t kMaximumAnalysisCacheKeyLength = 512U;
    const bool validMode = alignmentState.mode == ProjectAlignmentMode::kStrictIndex ||
                           alignmentState.mode == ProjectAlignmentMode::kGlobalOffsets ||
                           alignmentState.mode == ProjectAlignmentMode::kManualAnchors ||
                           alignmentState.mode == ProjectAlignmentMode::kAutomaticSequence;
    if (!validMode || alignmentState.anchors.size() > kMaximumPersistedAnchors ||
        (alignmentState.mode == ProjectAlignmentMode::kStrictIndex &&
         (!alignmentState.offsets.empty() || !alignmentState.anchors.empty() ||
          alignmentState.analysisCacheKey.has_value())) ||
        (alignmentState.mode == ProjectAlignmentMode::kGlobalOffsets &&
         !alignmentState.anchors.empty()) ||
        (alignmentState.mode == ProjectAlignmentMode::kManualAnchors &&
         alignmentState.anchors.empty()) ||
        (alignmentState.mode == ProjectAlignmentMode::kAutomaticSequence &&
         !alignmentState.analysisCacheKey.has_value())) {
        return projectFailure(MediaErrorCode::kInvalidArgument,
                              MediaOperation::kProjectMutation,
                              std::nullopt,
                              "Persisted alignment mode and payload are inconsistent.");
    }
    if (alignmentState.analysisCacheKey.has_value() &&
        (alignmentState.analysisCacheKey->empty() ||
         alignmentState.analysisCacheKey->size() > kMaximumAnalysisCacheKeyLength)) {
        return projectFailure(MediaErrorCode::kInvalidArgument,
                              MediaOperation::kProjectMutation,
                              std::nullopt,
                              "Alignment cache key is empty or exceeds the persisted bound.");
    }

    for (std::size_t index = 0U; index < alignmentState.offsets.size(); ++index) {
        const PersistedAlignmentOffset& offset = alignmentState.offsets[index];
        if (sources_.find(offset.sourceId) == nullptr ||
            (offset.sourceId == sources_.canonicalSourceId() && offset.frames != 0) ||
            std::any_of(alignmentState.offsets.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                        alignmentState.offsets.end(),
                        [&offset](const PersistedAlignmentOffset& other) {
                            return other.sourceId == offset.sourceId;
                        })) {
            return projectFailure(MediaErrorCode::kInvalidArgument,
                                  MediaOperation::kProjectMutation,
                                  offset.sourceId,
                                  "Persisted alignment offset is unknown, duplicated, or moves "
                                  "the canonical source.");
        }
    }

    std::map<SourceId, std::pair<FrameId, FrameId>> previousAnchor;
    for (const PersistedAlignmentAnchor& anchor : alignmentState.anchors) {
        const ComparisonSource* const source = sources_.find(anchor.sourceId);
        if (source == nullptr || anchor.sourceId == sources_.canonicalSourceId() ||
            !anchor.canonicalFrame.isValid() || !anchor.sourceFrame.isValid() ||
            anchor.canonicalFrame.value() >= sources_.canonicalFrameCount() ||
            anchor.sourceFrame.value() >= source->descriptor.frameCount.value) {
            return projectFailure(MediaErrorCode::kInvalidArgument,
                                  MediaOperation::kProjectMutation,
                                  anchor.sourceId,
                                  "Persisted alignment anchor names an invalid source or frame.");
        }
        const auto previous = previousAnchor.find(anchor.sourceId);
        if (previous != previousAnchor.end() && (anchor.canonicalFrame <= previous->second.first ||
                                                 anchor.sourceFrame <= previous->second.second)) {
            return projectFailure(MediaErrorCode::kInvalidArgument,
                                  MediaOperation::kProjectMutation,
                                  anchor.sourceId,
                                  "Persisted alignment anchors must be strictly monotonic.");
        }
        previousAnchor.insert_or_assign(anchor.sourceId,
                                        std::pair{anchor.canonicalFrame, anchor.sourceFrame});
    }
    return Status::success();
}

Status Project::validateViewState(const ProjectViewState& viewState) const {
    const bool validLayout = viewState.layout == ProjectViewLayout::kSideBySide ||
                             viewState.layout == ProjectViewLayout::kThreeUp ||
                             viewState.layout == ProjectViewLayout::kReferenceFocus ||
                             viewState.layout == ProjectViewLayout::kDifference ||
                             viewState.layout == ProjectViewLayout::kAnalysisGrid ||
                             viewState.layout == ProjectViewLayout::kWipe ||
                             viewState.layout == ProjectViewLayout::kSingle;
    const bool validMetric = viewState.differenceMetric == ProjectDifferenceMetric::kRgbAbsolute ||
                             viewState.differenceMetric == ProjectDifferenceMetric::kLuma ||
                             viewState.differenceMetric == ProjectDifferenceMetric::kChroma ||
                             viewState.differenceMetric == ProjectDifferenceMetric::kHeatmap ||
                             viewState.differenceMetric == ProjectDifferenceMetric::kExactPlanes;
    const bool validGain = viewState.gain == 1U || viewState.gain == 2U || viewState.gain == 4U ||
                           viewState.gain == 8U || viewState.gain == 16U;
    const bool validFilter = viewState.differenceFilter == ProjectDifferenceFilter::kNearest ||
                             viewState.differenceFilter == ProjectDifferenceFilter::kBilinear ||
                             viewState.differenceFilter == ProjectDifferenceFilter::kBicubic;
    const bool validViewport =
        std::isfinite(viewState.viewport.centerX) && std::isfinite(viewState.viewport.centerY) &&
        std::isfinite(viewState.viewport.scale) && viewState.viewport.centerX >= 0.0F &&
        viewState.viewport.centerX <= 1.0F && viewState.viewport.centerY >= 0.0F &&
        viewState.viewport.centerY <= 1.0F && viewState.viewport.scale >= 1.0F &&
        viewState.viewport.scale <= 64.0F;
    const bool validRoi =
        !viewState.roi.has_value() ||
        (std::isfinite(viewState.roi->left) && std::isfinite(viewState.roi->top) &&
         std::isfinite(viewState.roi->right) && std::isfinite(viewState.roi->bottom) &&
         viewState.roi->left >= 0.0F && viewState.roi->top >= 0.0F &&
         viewState.roi->right <= 1.0F && viewState.roi->bottom <= 1.0F &&
         viewState.roi->left < viewState.roi->right && viewState.roi->top < viewState.roi->bottom);
    const bool validEdge = !viewState.differenceEdge.has_value() ||
                           ((*viewState.differenceEdge)[0] != (*viewState.differenceEdge)[1] &&
                            sources_.find((*viewState.differenceEdge)[0]) != nullptr &&
                            sources_.find((*viewState.differenceEdge)[1]) != nullptr);
    const bool comparisonLayout = viewState.layout == ProjectViewLayout::kDifference ||
                                  viewState.layout == ProjectViewLayout::kAnalysisGrid ||
                                  viewState.layout == ProjectViewLayout::kWipe;
    const bool singleSource = sources_.sourceCount() == 1U;
    if (!validLayout || !validMetric || !validFilter || !validGain || !validViewport || !validRoi ||
        !validEdge || !std::isfinite(viewState.wipePosition) || viewState.wipePosition < 0.0F ||
        viewState.wipePosition > 1.0F || !std::isfinite(viewState.threshold) ||
        viewState.threshold < 0.0F || viewState.threshold > 1.0F ||
        (comparisonLayout && !viewState.differenceEdge.has_value()) ||
        (singleSource != (viewState.layout == ProjectViewLayout::kSingle)) ||
        (singleSource && viewState.differenceEdge.has_value()) ||
        ((viewState.layout == ProjectViewLayout::kThreeUp ||
          viewState.layout == ProjectViewLayout::kAnalysisGrid) &&
         sources_.sourceCount() != 3U)) {
        return projectFailure(MediaErrorCode::kInvalidArgument,
                              MediaOperation::kProjectMutation,
                              std::nullopt,
                              "Persisted view state is incompatible with the project sources.");
    }
    return Status::success();
}

} // namespace dvs::domain
