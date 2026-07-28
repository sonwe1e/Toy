#pragma once

#include "dvs/application/FramePair.h"
#include "dvs/application/RequestContext.h"
#include "dvs/domain/FrameTimeline.h"
#include "dvs/domain/MediaDescriptor.h"
#include "dvs/domain/MediaError.h"
#include "dvs/domain/Project.h"
#include "dvs/domain/SourceRelinkCandidate.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>

namespace dvs::application {

inline constexpr std::size_t kCriticalEventCapacity = 256;
inline constexpr std::size_t kRealtimeEventCapacity = 32;
inline constexpr std::size_t kCommandIngressCapacity = 64;

enum class EventPostResult {
    Accepted,
    Closed,
};

enum class EventLane {
    Critical,
    Realtime,
};

enum class CancellationReason {
    Superseded,
    UserRequested,
    Preempted,
    Shutdown,
};

enum class CommandOutcome {
    Succeeded,
    Failed,
    Canceled,
    AcceptedJob,
    Busy,
    Closed,
    TooLate,
};

// A terminal retains the most-specific scope supplied by the originating port. The coordinator
// must compare all members of that scope before accepting an asynchronous result.
using EventContext = std::variant<RequestContext,
                                  PlaybackRequestContext,
                                  FrameRequestContext,
                                  SaveRequestContext>;

// Graphics device notifications deliberately have no session identity: a device can become
// available before a session starts, and a later loss must invalidate every session using it.
struct GraphicsEventContext final {
    domain::DeviceGeneration deviceGeneration;

    [[nodiscard]] constexpr bool operator==(const GraphicsEventContext&) const = default;
};

struct RequestSucceeded final {
    EventContext context;
};

struct RequestFailed final {
    EventContext context;
    domain::MediaError error;
};

struct RequestCanceled final {
    EventContext context;
    CancellationReason reason;
};

using RequestTerminal = std::variant<RequestSucceeded, RequestFailed, RequestCanceled>;

struct CommandTerminal final {
    CommandContext context;
    CommandOutcome outcome;
    std::optional<domain::MediaError> error;
};

struct ProbeCompleted final {
    RequestContext context;
    domain::SourceRole sourceRole;
    domain::MediaDescriptor descriptor;
    // VFR sources publish one normalized, zero-anchored display-order timeline that the
    // coordinator shares with the frame provider. CFR sources leave this nullopt.
    std::optional<std::shared_ptr<const domain::FrameTimeline>> timeline;
};

struct FramePairReady final {
    FrameRequestContext context;
    FramePair pair;
};

struct FramePairPresented final {
    FrameRequestContext context;
    domain::FrameId frameId;
};

struct GraphicsDeviceReady final {
    GraphicsEventContext context;
};

struct GraphicsDeviceUnavailable final {
    GraphicsEventContext context;
    domain::MediaError error;
};

struct GraphicsDeviceLost final {
    GraphicsEventContext context;
    domain::MediaError error;
};

struct DeadlineElapsed final {
    PlaybackRequestContext context;
    std::uint64_t timerId;
};

// Settings are intentionally key/value data at this boundary. JSON parsing and persistence stay
// in an adapter, and the coordinator can evolve the known key set without leaking a JSON type.
struct SettingsSnapshot final {
    std::map<std::string, std::string, std::less<>> values;
};

// A load always checks the persisted A and B identities in this order. A missing or changed
// source is reported here instead of rejecting an otherwise valid project, so the coordinator
// can keep its edits available while it offers explicit relink.
struct SourceRevalidationDiagnostic final {
    domain::SourceRole sourceRole;
    std::optional<domain::MediaError> error;
};

using SourceRevalidationDiagnostics = std::array<SourceRevalidationDiagnostic, 2>;

struct ProjectLoaded final {
    RequestContext context;
    domain::Project project;
    SourceRevalidationDiagnostics sourceDiagnostics;
};

// This only confirms filesystem path normalization and identity capture. It does not assert
// source compatibility or session readiness: the coordinator must run a fresh media probe and
// call Project::replaceSources with a new ValidatedSourcePair before committing the change.
struct SourceRelinkPrepared final {
    RequestContext context;
    domain::SourceRelinkCandidate candidate;
};

struct ProjectSaved final {
    SaveRequestContext context;
};

struct SettingsLoaded final {
    RequestContext context;
    SettingsSnapshot settings;
};

using ApplicationEvent = std::variant<RequestTerminal,
                                      CommandTerminal,
                                      ProbeCompleted,
                                      FramePairReady,
                                      FramePairPresented,
                                      GraphicsDeviceReady,
                                      GraphicsDeviceUnavailable,
                                      GraphicsDeviceLost,
                                      DeadlineElapsed,
                                      ProjectLoaded,
                                      SourceRelinkPrepared,
                                      ProjectSaved,
                                      SettingsLoaded>;

} // namespace dvs::application
