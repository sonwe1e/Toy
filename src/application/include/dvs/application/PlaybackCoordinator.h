#pragma once

#include "dvs/application/Commands.h"
#include "dvs/application/Ports.h"
#include "dvs/application/SessionSnapshot.h"

#include <functional>
#include <memory>
#include <vector>

namespace dvs::application {

class PlaybackCoordinator final : public IApplicationEventSink {
public:
    struct Dependencies final {
        std::shared_ptr<IMediaProbe> mediaProbe;
        std::shared_ptr<IFrameProvider> directFrameProvider;
        std::shared_ptr<IAlignmentAnalysisService> alignmentAnalysisService;
        std::shared_ptr<IDeadlineScheduler> deadlineScheduler;
        std::shared_ptr<ISteadyClock> clock;
        std::shared_ptr<IRenderChannel> renderChannel;
        // Invoked after an immutable snapshot or command terminal is published. The callback may
        // run on the coordinator worker and must only enqueue GUI work.
        std::function<void()> statePublished;
    };

    [[nodiscard]] static std::shared_ptr<PlaybackCoordinator> create(domain::SessionId sessionId,
                                                                     Dependencies dependencies);
    ~PlaybackCoordinator() override;

    PlaybackCoordinator(const PlaybackCoordinator&) = delete;
    PlaybackCoordinator& operator=(const PlaybackCoordinator&) = delete;
    PlaybackCoordinator(PlaybackCoordinator&&) = delete;
    PlaybackCoordinator& operator=(PlaybackCoordinator&&) = delete;

    [[nodiscard]] PortSubmitResult submit(PlaybackCommand command);
    [[nodiscard]] std::shared_ptr<const SessionSnapshot> snapshot() const;

    // A thin UI adapter can drain exact-once command outcomes while it binds normal state through
    // snapshot(). The coordinator is the only producer of these terminals.
    [[nodiscard]] std::vector<CommandTerminal> takeCompletedCommands();

    [[nodiscard]] std::shared_ptr<const std::vector<SequenceAlignmentResult>>
    acceptedSequenceAlignments() const;

    // Async adapters receive this independently owned gate instead of retaining the coordinator.
    // The gate fails closed once shutdown begins, so a worker can never become the thread that
    // releases the coordinator's final strong reference.
    [[nodiscard]] std::shared_ptr<IApplicationEventSink> eventSink() const noexcept;
    void shutdown() noexcept;

    [[nodiscard]] EventPostResult postCritical(ApplicationEvent event) noexcept override;
    [[nodiscard]] EventPostResult postRealtime(ApplicationEvent event) noexcept override;
    void closeRealtimeIngress() noexcept override;
    void closeCriticalIngress() noexcept override;

private:
    class Impl;

    explicit PlaybackCoordinator(domain::SessionId sessionId, Dependencies dependencies);

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::application
