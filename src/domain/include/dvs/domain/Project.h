#pragma once

#include "dvs/domain/ValidatedComparisonSet.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dvs::domain {

// Schema-1 preserves all non-global workspace keys verbatim so later UI revisions can add keys
// without requiring the domain aggregate to understand view-specific presentation values.
using WorkspaceState = std::map<std::string, std::string>;

enum class ProjectAlignmentMode {
    kStrictIndex,
    kGlobalOffsets,
    kManualAnchors,
    kAutomaticSequence,
};

struct PersistedAlignmentOffset final {
    SourceId sourceId = 0U;
    std::int64_t frames = 0;

    [[nodiscard]] bool operator==(const PersistedAlignmentOffset&) const = default;
};

struct PersistedAlignmentAnchor final {
    SourceId sourceId = 0U;
    FrameId canonicalFrame;
    FrameId sourceFrame;

    [[nodiscard]] bool operator==(const PersistedAlignmentAnchor&) const = default;
};

struct ProjectAlignmentState final {
    ProjectAlignmentMode mode = ProjectAlignmentMode::kStrictIndex;
    std::vector<PersistedAlignmentOffset> offsets;
    std::vector<PersistedAlignmentAnchor> anchors;
    std::optional<std::string> analysisCacheKey;

    [[nodiscard]] bool operator==(const ProjectAlignmentState&) const = default;
};

enum class ProjectViewLayout {
    kSideBySide,
    kThreeUp,
    kReferenceFocus,
    kDifference,
};

enum class ProjectDifferenceMetric {
    kRgbAbsolute,
    kLuma,
    kChroma,
    kHeatmap,
    kExactPlanes,
};

struct ProjectViewState final {
    ProjectViewLayout layout = ProjectViewLayout::kSideBySide;
    std::array<SourceId, 2U> differenceEdge{0U, 1U};
    ProjectDifferenceMetric differenceMetric = ProjectDifferenceMetric::kRgbAbsolute;
    std::uint8_t gain = 1U;

    [[nodiscard]] bool operator==(const ProjectViewState&) const = default;
};

// Persistence adapters deserialize this value and hand it to Project::restorePersisted. The
// aggregate deliberately normalizes an interrupted process rather than JSON code.
struct ProjectState final {
    ProjectId id;
    std::string displayName;
    ValidatedComparisonSet sources;
    std::optional<FrameId> inMark;
    std::optional<FrameId> outMark;
    FrameId lastDisplayedFrame;
    WorkspaceState workspaceState;
    ProjectAlignmentState alignmentState;
    ProjectViewState viewState;
};

class Project final {
public:
    [[nodiscard]] static Result<Project>
    create(ProjectId id, std::string displayName, ValidatedComparisonSet sources);
    // This entry point is exclusively for persisted state.
    [[nodiscard]] static Result<Project> restorePersisted(ProjectState persisted);
    // A fresh media probe supplies a new validated comparison set and the original aggregate is
    // never modified.
    [[nodiscard]] Result<Project> replaceSources(ValidatedComparisonSet sources) const;

    [[nodiscard]] const ProjectId& id() const noexcept;
    [[nodiscard]] const std::string& displayName() const noexcept;
    [[nodiscard]] const ValidatedComparisonSet& sources() const noexcept;
    [[nodiscard]] const std::optional<FrameId>& inMark() const noexcept;
    [[nodiscard]] const std::optional<FrameId>& outMark() const noexcept;
    [[nodiscard]] FrameId lastDisplayedFrame() const noexcept;
    [[nodiscard]] const WorkspaceState& workspaceState() const noexcept;
    [[nodiscard]] const ProjectAlignmentState& alignmentState() const noexcept;
    [[nodiscard]] const ProjectViewState& viewState() const noexcept;

    // Marks are independently editable review positions on the canonical timeline. A reversed
    // pair remains visible to the user; timeline presentation decides how to render it.
    [[nodiscard]] Status setInMark(FrameId frameId);
    [[nodiscard]] Status setOutMark(FrameId frameId);
    void clearMarks() noexcept;
    [[nodiscard]] Status setLastDisplayedFrame(FrameId frameId);
    void setWorkspaceState(WorkspaceState workspaceState);
    [[nodiscard]] Status setAlignmentState(ProjectAlignmentState alignmentState);
    [[nodiscard]] Status setViewState(ProjectViewState viewState);

private:
    Project(ProjectId id,
            std::string displayName,
            ValidatedComparisonSet sources,
            std::optional<FrameId> inMark,
            std::optional<FrameId> outMark,
            FrameId lastDisplayedFrame,
            WorkspaceState workspaceState,
            ProjectAlignmentState alignmentState,
            ProjectViewState viewState);

    [[nodiscard]] Status validateFrame(FrameId frameId, MediaOperation operation) const;
    [[nodiscard]] Status validateAlignmentState(const ProjectAlignmentState& alignmentState) const;
    [[nodiscard]] Status validateViewState(const ProjectViewState& viewState) const;
    [[nodiscard]] static Result<Project> rebuild(ProjectState state);

    ProjectId id_;
    std::string displayName_;
    ValidatedComparisonSet sources_;
    std::optional<FrameId> inMark_;
    std::optional<FrameId> outMark_;
    FrameId lastDisplayedFrame_;
    WorkspaceState workspaceState_;
    ProjectAlignmentState alignmentState_;
    ProjectViewState viewState_;
};

} // namespace dvs::domain
