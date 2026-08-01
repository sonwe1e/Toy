#pragma once

#include "dvs/application/Commands.h"
#include "dvs/application/Ports.h"
#include "dvs/application/SessionSnapshot.h"
#include "dvs/domain/Project.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dvs::application {

struct WorkspaceSnapshot final {
    bool busy = false;
    bool dirty = false;
    bool hasProject = false;
    std::filesystem::path projectPath;
    std::string displayName;
    SourceRevalidationDiagnostics sourceDiagnostics;
    std::optional<domain::ProjectViewState> restoredViewState;
    std::optional<domain::FrameId> restoredInMark;
    std::optional<domain::FrameId> restoredOutMark;
    std::optional<domain::MediaError> lastError;

    [[nodiscard]] bool operator==(const WorkspaceSnapshot&) const = default;
};

// Coordinates recoverable project I/O around PlaybackCoordinator without moving persistence into
// the real-time playback state machine. All public methods are called by the GUI thread; adapter
// completions arrive through an independently owned, thread-safe event inbox.
class WorkspaceCoordinator final {
public:
    struct Dependencies final {
        std::shared_ptr<IProjectRepository> projectRepository;
        std::function<PortSubmitResult(PlaybackCommand)> submitPlayback;
        std::function<std::shared_ptr<const SessionSnapshot>()> playbackSnapshot;
        std::function<std::vector<CommandTerminal>()> takePlaybackTerminals;
        std::function<std::shared_ptr<const std::vector<SequenceAlignmentResult>>()>
            acceptedSequenceAlignments;
        std::function<domain::ProjectId()> createProjectId;
        std::function<void()> statePublished;
    };

    [[nodiscard]] static std::unique_ptr<WorkspaceCoordinator> create(Dependencies dependencies);
    ~WorkspaceCoordinator();

    WorkspaceCoordinator(const WorkspaceCoordinator&) = delete;
    WorkspaceCoordinator& operator=(const WorkspaceCoordinator&) = delete;
    WorkspaceCoordinator(WorkspaceCoordinator&&) = delete;
    WorkspaceCoordinator& operator=(WorkspaceCoordinator&&) = delete;

    [[nodiscard]] PortSubmitResult submitPlayback(PlaybackCommand command);
    [[nodiscard]] std::shared_ptr<const SessionSnapshot> playbackSnapshot();
    [[nodiscard]] std::vector<CommandTerminal> takeCompletedPlaybackCommands();

    [[nodiscard]] PortSubmitResult openProject(const std::filesystem::path& projectPath);
    [[nodiscard]] PortSubmitResult closeReview();
    [[nodiscard]] PortSubmitResult
    saveProject(const std::filesystem::path& projectPath,
                std::string displayName,
                domain::ProjectViewState viewState,
                std::optional<domain::FrameId> inMark = std::nullopt,
                std::optional<domain::FrameId> outMark = std::nullopt);
    [[nodiscard]] PortSubmitResult relinkSource(domain::SourceId sourceId,
                                                const std::filesystem::path& sourcePath);

    [[nodiscard]] std::shared_ptr<const WorkspaceSnapshot> snapshot();
    void markViewDirty();
    void shutdown() noexcept;

private:
    explicit WorkspaceCoordinator(Dependencies dependencies);

    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::application
