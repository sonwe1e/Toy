#include "dvs/domain/Project.h"

#include <algorithm>
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
                 std::vector<Clip> clips,
                 std::vector<ExportRecord> exportRecords,
                 std::optional<FrameId> inMark,
                 std::optional<FrameId> outMark,
                 const FrameId lastDisplayedFrame,
                 WorkspaceState workspaceState)
    : id_(std::move(id)), displayName_(std::move(displayName)), sources_(std::move(sources)),
      clips_(std::move(clips)), exportRecords_(std::move(exportRecords)), inMark_(inMark),
      outMark_(outMark), lastDisplayedFrame_(lastDisplayedFrame),
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
        {},
        {},
        std::nullopt,
        std::nullopt,
        FrameId{0},
        {},
    });
}

Result<Project> Project::restorePersisted(ProjectState persisted) {
    return rebuild(std::move(persisted), true);
}

Result<Project> Project::replaceSources(ValidatedSourcePair sources) const {
    return rebuild(
        ProjectState{
            .id = id_,
            .displayName = displayName_,
            .sources = std::move(sources),
            .clips = std::vector<Clip>{clips_.begin(), clips_.end()},
            .exportRecords =
                std::vector<ExportRecord>{exportRecords_.begin(), exportRecords_.end()},
            .inMark = inMark_,
            .outMark = outMark_,
            .lastDisplayedFrame = lastDisplayedFrame_,
            .workspaceState = workspaceState_,
        },
        false);
}

Result<Project> Project::rebuild(ProjectState state, const bool normalizePersistedRunningExports) {
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

    for (const Clip& clip : state.clips) {
        status = project.validateClip(clip);
        if (!status) {
            return Result<Project>::failure(status.error());
        }
        if (project.containsClip(clip.id)) {
            return Result<Project>::failure(
                makeMediaError(MediaErrorCode::kDuplicateIdentifier,
                               MediaOperation::kProjectMutation,
                               SourceRole::kClip,
                               false,
                               "Persisted project contains duplicate clip IDs."));
        }
        project.clips_.push_back(clip);
    }

    for (ExportRecord record : state.exportRecords) {
        if (normalizePersistedRunningExports && record.state == ExportJobState::kRunning) {
            record.state = ExportJobState::kInterrupted;
        }
        status = project.addExportRecord(std::move(record));
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

std::span<const Clip> Project::clips() const noexcept {
    return clips_;
}

std::span<const ExportRecord> Project::exportRecords() const noexcept {
    return exportRecords_;
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
    auto status = validateFrame(frameId, MediaOperation::kClipMutation);
    if (!status) {
        return status;
    }
    inMark_ = frameId;
    return Status::success();
}

Status Project::setOutMark(const FrameId frameId) {
    auto status = validateFrame(frameId, MediaOperation::kClipMutation);
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

Result<ClipId> Project::addClipFromMarks(ClipId id, std::string name, std::string note) {
    if (!inMark_.has_value() || !outMark_.has_value()) {
        return Result<ClipId>::failure(
            makeMediaError(MediaErrorCode::kMarksIncomplete,
                           MediaOperation::kClipMutation,
                           SourceRole::kClip,
                           true,
                           "Both inclusive marks are required before creating a clip."));
    }
    if (*outMark_ < *inMark_) {
        return Result<ClipId>::failure(makeMediaError(MediaErrorCode::kMarksReversed,
                                                      MediaOperation::kClipMutation,
                                                      SourceRole::kClip,
                                                      true,
                                                      "The out mark precedes the in mark."));
    }

    auto range = FrameRange::inclusive(*inMark_, *outMark_);
    if (!range) {
        return Result<ClipId>::failure(range.error());
    }

    Clip clip{
        .id = std::move(id),
        .name = std::move(name),
        .note = std::move(note),
        .range = std::move(range).value(),
    };
    auto status = validateClip(clip);
    if (!status) {
        return Result<ClipId>::failure(status.error());
    }
    if (containsClip(clip.id)) {
        return Result<ClipId>::failure(makeMediaError(MediaErrorCode::kDuplicateIdentifier,
                                                      MediaOperation::kClipMutation,
                                                      SourceRole::kClip,
                                                      false,
                                                      "Clip ID already exists in this project."));
    }

    clips_.push_back(clip);
    return Result<ClipId>::success(clip.id);
}

Status Project::updateClip(Clip clip) {
    auto status = validateClip(clip);
    if (!status) {
        return status;
    }

    const auto iterator = std::find_if(clips_.begin(), clips_.end(), [&clip](const Clip& current) {
        return current.id == clip.id;
    });
    if (iterator == clips_.end()) {
        return projectFailure(MediaErrorCode::kClipNotFound,
                              MediaOperation::kClipMutation,
                              SourceRole::kClip,
                              "Cannot update an unknown clip ID.");
    }
    *iterator = std::move(clip);
    return Status::success();
}

Status Project::removeClip(const ClipId& id) {
    const auto iterator = std::find_if(
        clips_.begin(), clips_.end(), [&id](const Clip& current) { return current.id == id; });
    if (iterator == clips_.end()) {
        return projectFailure(MediaErrorCode::kClipNotFound,
                              MediaOperation::kClipMutation,
                              SourceRole::kClip,
                              "Cannot remove an unknown clip ID.");
    }
    clips_.erase(iterator);
    return Status::success();
}

Status Project::addExportRecord(ExportRecord record) {
    if (!record.id.isValid() || !record.clipId.isValid()) {
        return projectFailure(MediaErrorCode::kInvalidArgument,
                              MediaOperation::kProjectMutation,
                              SourceRole::kExport,
                              "Export and clip IDs must be non-empty.");
    }
    if (containsExportRecord(record.id)) {
        return projectFailure(MediaErrorCode::kDuplicateIdentifier,
                              MediaOperation::kProjectMutation,
                              SourceRole::kExport,
                              "Export record ID already exists in this project.");
    }
    exportRecords_.push_back(std::move(record));
    return Status::success();
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
        return projectFailure(MediaErrorCode::kClipOutOfRange,
                              operation,
                              SourceRole::kClip,
                              "Frame ID lies outside the canonical source timeline.");
    }
    return Status::success();
}

Status Project::validateClip(const Clip& clip) const {
    if (!clip.id.isValid() || clip.name.empty()) {
        return projectFailure(MediaErrorCode::kInvalidArgument,
                              MediaOperation::kClipMutation,
                              SourceRole::kClip,
                              "Clip ID and name must be non-empty.");
    }
    if (!clip.range.isWithin(sources_.canonicalFrameCount())) {
        return projectFailure(MediaErrorCode::kClipOutOfRange,
                              MediaOperation::kClipMutation,
                              SourceRole::kClip,
                              "Clip range lies outside the canonical source timeline.");
    }
    return Status::success();
}

bool Project::containsClip(const ClipId& id) const noexcept {
    return std::any_of(
        clips_.begin(), clips_.end(), [&id](const Clip& clip) { return clip.id == id; });
}

bool Project::containsExportRecord(const ExportRecordId& id) const noexcept {
    return std::any_of(exportRecords_.begin(),
                       exportRecords_.end(),
                       [&id](const ExportRecord& record) { return record.id == id; });
}

} // namespace dvs::domain
