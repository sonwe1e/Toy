#pragma once

#include "dvs/application/Events.h"
#include "dvs/domain/FrameTimeline.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace dvs::application {

enum class PortSubmitResult {
    Accepted,
    Busy,
    Closed,
};

enum class FrameRequestPriority {
    Exact,
    Sequential,
    Prefetch,
};

enum class RenderPublishResult {
    Accepted,
    Replaced,
    Closed,
};

struct MediaProbeRequest final {
    RequestContext context;
    domain::SourceId sourceId;
    std::filesystem::path sourcePath;
};

struct FrameProviderOpenRequest final {
    PlaybackRequestContext context;
    // The session's compared sources, two or three entries in session order. The provider opens
    // one decode slot per entry and publishes one FrameSet entry per slot.
    std::vector<domain::ComparisonSource> sources;
    // The canonical timeline carries either a rational CFR rate or an immutable, normalized,
    // zero-anchored VFR display-order timeline. The provider and the coordinator share one
    // VFR timeline without persisting derived per-frame data for the whole session.
    domain::CanonicalTimeline timeline;
};

struct FrameRequest final {
    FrameRequestContext context;
    domain::FrameId frameId;
    FrameRequestPriority priority;
};

struct FrameProviderCloseRequest final {
    PlaybackRequestContext context;
};

struct ProjectLoadRequest final {
    RequestContext context;
    std::filesystem::path projectPath;
};

// Relinking is always explicit. Persistence only prepares a filesystem candidate; it never
// receives an editable project or claims the replacement has compatible media metadata.
struct ProjectRelinkRequest final {
    RequestContext context;
    domain::SourceId sourceId;
    std::filesystem::path newSourcePath;
};

struct ProjectSaveRequest final {
    SaveRequestContext context;
    // The coordinator chooses a project destination before persistence begins. Repositories must
    // never infer it from a source path or a previously loaded project, because Save As is an
    // explicit operation and new projects have no bound document yet.
    std::filesystem::path projectPath;
    domain::Project project;
};

struct SettingsLoadRequest final {
    RequestContext context;
};

struct SettingsSaveRequest final {
    RequestContext context;
    SettingsSnapshot settings;
};

struct DeadlineRequest final {
    PlaybackRequestContext context;
    std::uint64_t timerId;
    std::chrono::steady_clock::time_point due;
};

// Worker and relay threads use the critical lane for terminal, control, and exact-frame events.
// GUI and render threads must not call this interface directly; adapters relay those events on a
// non-render worker. Realtime publication is coalesced by implementation, never by callers.
class IApplicationEventSink {
public:
    virtual ~IApplicationEventSink() = default;

    [[nodiscard]] virtual EventPostResult postCritical(ApplicationEvent event) noexcept = 0;
    [[nodiscard]] virtual EventPostResult postRealtime(ApplicationEvent event) noexcept = 0;

    // The coordinator closes realtime ingress first. Critical ingress closes only after every
    // registered producer is quiescent and the critical queue has drained.
    virtual void closeRealtimeIngress() noexcept = 0;
    virtual void closeCriticalIngress() noexcept = 0;
};

class IMediaProbe {
public:
    virtual ~IMediaProbe() = default;

    // Probing is queued work. Adapters must downgrade this handle before retaining it so a
    // coordinator teardown cannot leave a delayed FFmpeg completion with a dangling sink.
    [[nodiscard]] virtual PortSubmitResult
    submit(const MediaProbeRequest& request, std::shared_ptr<IApplicationEventSink> events) = 0;
    virtual void cancel(const RequestContext& context) noexcept = 0;
};

class IFrameProvider {
public:
    virtual ~IFrameProvider() = default;

    // Frame requests may outlive a coordinator epoch. Retain only a weak sink after admission.
    [[nodiscard]] virtual PortSubmitResult
    submit(const FrameProviderOpenRequest& request,
           std::shared_ptr<IApplicationEventSink> events) = 0;
    [[nodiscard]] virtual PortSubmitResult
    submit(const FrameRequest& request, std::shared_ptr<IApplicationEventSink> events) = 0;
    [[nodiscard]] virtual PortSubmitResult
    submit(const FrameProviderCloseRequest& request,
           std::shared_ptr<IApplicationEventSink> events) = 0;
    virtual void cancel(const PlaybackRequestContext& context) noexcept = 0;
};

class IProjectRepository {
public:
    virtual ~IProjectRepository() = default;

    // Repository work is queued. Implementations downgrade this handle before queueing so an
    // expired coordinator sink cannot be called by delayed I/O completion.
    [[nodiscard]] virtual PortSubmitResult
    submit(const ProjectLoadRequest& request, std::shared_ptr<IApplicationEventSink> events) = 0;
    [[nodiscard]] virtual PortSubmitResult
    submit(const ProjectRelinkRequest& request, std::shared_ptr<IApplicationEventSink> events) = 0;
    [[nodiscard]] virtual PortSubmitResult
    submit(const ProjectSaveRequest& request, std::shared_ptr<IApplicationEventSink> events) = 0;
    virtual void cancel(const RequestContext& context) noexcept = 0;
};

class ISettingsRepository {
public:
    virtual ~ISettingsRepository() = default;

    // See IProjectRepository: queued persistence work must not retain a raw event-sink pointer.
    [[nodiscard]] virtual PortSubmitResult
    submit(const SettingsLoadRequest& request, std::shared_ptr<IApplicationEventSink> events) = 0;
    [[nodiscard]] virtual PortSubmitResult
    submit(const SettingsSaveRequest& request, std::shared_ptr<IApplicationEventSink> events) = 0;
    virtual void cancel(const RequestContext& context) noexcept = 0;
};

class ISteadyClock {
public:
    virtual ~ISteadyClock() = default;

    [[nodiscard]] virtual std::chrono::steady_clock::time_point now() const noexcept = 0;
};

class IDeadlineScheduler {
public:
    virtual ~IDeadlineScheduler() = default;

    [[nodiscard]] virtual PortSubmitResult
    schedule(const DeadlineRequest& request, std::shared_ptr<IApplicationEventSink> events) = 0;

    // A successful cancellation guarantees that this timer cannot post after return.
    [[nodiscard]] virtual bool cancel(std::uint64_t timerId) noexcept = 0;
};

// This is the separate, single-slot path for high-frequency complete frame sets. It deliberately
// has no QML/Qt type and no method for publishing one source of a set.
class IRenderChannel {
public:
    virtual ~IRenderChannel() = default;

    [[nodiscard]] virtual RenderPublishResult publish(const FrameRequestContext& context,
                                                      FrameSet set) noexcept = 0;
    virtual void clear(const PlaybackRequestContext& context) noexcept = 0;
};

} // namespace dvs::application
