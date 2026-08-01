#pragma once

#include "dvs/application/Commands.h"
#include "dvs/application/Ports.h"
#include "dvs/application/SessionSnapshot.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dvs::application {

struct WorkspaceSnapshot final {
    bool busy = false;
    std::string displayName;
    SourceRevalidationDiagnostics sourceDiagnostics;
    std::optional<domain::MediaError> lastError;

    [[nodiscard]] bool operator==(const WorkspaceSnapshot&) const = default;
};

// Coordinates review-session lifecycle (open/close) around PlaybackCoordinator. It owns no
// recoverable project persistence; all session state lives in the running playback session.
// All public methods are called by the GUI thread; adapter completions arrive through an
// independently owned, thread-safe event inbox.
class WorkspaceCoordinator final {
public:
    struct Dependencies final {
        std::function<PortSubmitResult(PlaybackCommand)> submitPlayback;
        std::function<std::shared_ptr<const SessionSnapshot>()> playbackSnapshot;
        std::function<std::vector<CommandTerminal>()> takePlaybackTerminals;
        std::function<std::shared_ptr<const std::vector<SequenceAlignmentResult>>()>
            acceptedSequenceAlignments;
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

    [[nodiscard]] PortSubmitResult closeReview();

    [[nodiscard]] std::shared_ptr<const WorkspaceSnapshot> snapshot();
    void shutdown() noexcept;

private:
    explicit WorkspaceCoordinator(Dependencies dependencies);

    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::application
