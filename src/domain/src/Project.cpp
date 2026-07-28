#include "dvs/domain/Project.h"

#include <utility>

namespace dvs::domain {
namespace {

[[nodiscard]] Status projectFailure(const MediaErrorCode code,
                                    const MediaOperation operation,
                                    const SourceRole sourceRole,
                                    std::string technicalDetail) {
    return Status::failure(
        makeMediaError(code, operation, sourceRole, false, std::move(technicalDetail)));
}

} // namespace

Project::Project(ProjectId id,
                 std::string displayName,
                 ValidatedSourcePair sources,
                 std::optional<FrameId> inMark,
                 std::optional<FrameId> outMark,
                 const FrameId lastDisplayedFrame,
                 WorkspaceState workspaceState)
    : id_(std::move(id)), displayName_(std::move(displayName)), sources_(std::move(sources)),
      inMark_(inMark), outMark_(outMark), lastDisplayedFrame_(lastDisplayedFrame),
      workspaceState_(std::move(workspaceState)) {}

Result<Project>
Project::create(ProjectId id, std::string displayName, ValidatedSourcePair sources) {
    if (!id.isValid()) {
        return Result<Project>::failure(makeMediaError(MediaErrorCode::kInvalidArgument,
                                                       MediaOperation::kProjectMutation,
                                                       SourceRole::kProject,
                                                       false,
                                                       "Project ID must be non-empty."));
    }
    if (displayName.empty()) {
        return Result<Project>::failure(makeMediaError(MediaErrorCode::kInvalidArgument,
                                                       MediaOperation::kProjectMutation,
                                                       SourceRole::kProject,
                                                       false,
                                                       "Project display name must be non-empty."));
    }

    return Result<Project>::success(Project{
        std::move(id),
        std::move(displayName),
        std::move(sources),
        std::nullopt,
        std::nullopt,
        FrameId{0},
        {},
    });
}

Result<Project> Project::restorePersisted(ProjectState persisted) {
    return rebuild(std::move(persisted));
}

Result<Project> Project::replaceSources(ValidatedSourcePair sources) const {
    return rebuild(ProjectState{
        .id = id_,
        .displayName = displayName_,
        .sources = std::move(sources),
        .inMark = inMark_,
        .outMark = outMark_,
        .lastDisplayedFrame = lastDisplayedFrame_,
        .workspaceState = workspaceState_,
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

    return Result<Project>::success(std::move(project));
}

const ProjectId& Project::id() const noexcept {
    return id_;
}

const std::string& Project::displayName() const noexcept {
    return displayName_;
}

const ValidatedSourcePair& Project::sources() const noexcept {
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

Status Project::validateFrame(const FrameId frameId, const MediaOperation operation) const {
    if (!frameId.isValid() || frameId.value() >= sources_.canonicalFrameCount()) {
        return projectFailure(MediaErrorCode::kFrameOutOfRange,
                              operation,
                              SourceRole::kProject,
                              "Frame ID lies outside the canonical source timeline.");
    }
    return Status::success();
}

} // namespace dvs::domain
