#pragma once

#include "dvs/domain/Clip.h"
#include "dvs/domain/SourcePairValidator.h"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dvs::domain {

// Schema-1 preserves all non-global workspace keys verbatim so later UI revisions can add keys
// without requiring the domain aggregate to understand view-specific presentation values.
using WorkspaceState = std::map<std::string, std::string>;

struct ExportRecord final {
    ExportRecordId id;
    ClipId clipId;
    ExportJobState state = ExportJobState::kPending;
    std::string outputReference;
    std::optional<MediaError> error;
};

// Persistence adapters deserialize this value and hand it to Project::restorePersisted. The
// aggregate deliberately normalizes an interrupted process rather than JSON code.
struct ProjectState final {
    ProjectId id;
    std::string displayName;
    ValidatedSourcePair sources;
    std::vector<Clip> clips;
    std::vector<ExportRecord> exportRecords;
    std::optional<FrameId> inMark;
    std::optional<FrameId> outMark;
    FrameId lastDisplayedFrame;
    WorkspaceState workspaceState;
};

class Project final {
public:
    [[nodiscard]] static Result<Project>
    create(ProjectId id, std::string displayName, ValidatedSourcePair sources);
    // This entry point is exclusively for persisted state. An export saved as running was
    // interrupted by process exit, so it is normalized to interrupted while restoring.
    [[nodiscard]] static Result<Project> restorePersisted(ProjectState persisted);
    // A fresh media probe supplies a new validated pair. Existing in-memory job state is retained
    // exactly, including running exports, and the original aggregate is never modified.
    [[nodiscard]] Result<Project> replaceSources(ValidatedSourcePair sources) const;

    [[nodiscard]] const ProjectId& id() const noexcept;
    [[nodiscard]] const std::string& displayName() const noexcept;
    [[nodiscard]] const ValidatedSourcePair& sources() const noexcept;
    [[nodiscard]] std::span<const Clip> clips() const noexcept;
    [[nodiscard]] std::span<const ExportRecord> exportRecords() const noexcept;
    [[nodiscard]] const std::optional<FrameId>& inMark() const noexcept;
    [[nodiscard]] const std::optional<FrameId>& outMark() const noexcept;
    [[nodiscard]] FrameId lastDisplayedFrame() const noexcept;
    [[nodiscard]] const WorkspaceState& workspaceState() const noexcept;

    // Marks are independently editable. A reversed pair remains visible to the user, but clip
    // creation rejects it without mutating either mark.
    [[nodiscard]] Status setInMark(FrameId frameId);
    [[nodiscard]] Status setOutMark(FrameId frameId);
    void clearMarks() noexcept;
    [[nodiscard]] Result<ClipId> addClipFromMarks(ClipId id, std::string name, std::string note);
    [[nodiscard]] Status updateClip(Clip clip);
    [[nodiscard]] Status removeClip(const ClipId& id);
    [[nodiscard]] Status addExportRecord(ExportRecord record);
    [[nodiscard]] Status setLastDisplayedFrame(FrameId frameId);
    void setWorkspaceState(WorkspaceState workspaceState);

private:
    Project(ProjectId id,
            std::string displayName,
            ValidatedSourcePair sources,
            std::vector<Clip> clips,
            std::vector<ExportRecord> exportRecords,
            std::optional<FrameId> inMark,
            std::optional<FrameId> outMark,
            FrameId lastDisplayedFrame,
            WorkspaceState workspaceState);

    [[nodiscard]] Status validateFrame(FrameId frameId, MediaOperation operation) const;
    [[nodiscard]] Status validateClip(const Clip& clip) const;
    [[nodiscard]] bool containsClip(const ClipId& id) const noexcept;
    [[nodiscard]] bool containsExportRecord(const ExportRecordId& id) const noexcept;
    [[nodiscard]] static Result<Project> rebuild(ProjectState state,
                                                 bool normalizePersistedRunningExports);

    ProjectId id_;
    std::string displayName_;
    ValidatedSourcePair sources_;
    std::vector<Clip> clips_;
    std::vector<ExportRecord> exportRecords_;
    std::optional<FrameId> inMark_;
    std::optional<FrameId> outMark_;
    FrameId lastDisplayedFrame_;
    WorkspaceState workspaceState_;
};

} // namespace dvs::domain
