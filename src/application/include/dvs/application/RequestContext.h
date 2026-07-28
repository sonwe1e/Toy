#pragma once

#include "dvs/domain/Identifiers.h"

namespace dvs::application {

// Identifies one unit of asynchronous work. Every port request and terminal event carries this
// context so the coordinator can discard work from a replaced source pair.
struct RequestContext final {
    domain::SessionId sessionId;
    domain::SessionEpoch sessionEpoch;
    domain::RequestId requestId;

    [[nodiscard]] constexpr bool operator==(const RequestContext&) const = default;
};

// Playback generations are deliberately scoped separately from source-pair epochs. A seek can
// invalidate frame work without invalidating a save or an export job.
struct PlaybackRequestContext final {
    RequestContext request;
    domain::PlaybackGeneration playbackGeneration;

    [[nodiscard]] constexpr bool operator==(const PlaybackRequestContext&) const = default;
};

struct FrameRequestContext final {
    PlaybackRequestContext playback;
    domain::DeviceGeneration deviceGeneration;

    [[nodiscard]] constexpr bool operator==(const FrameRequestContext&) const = default;
};

struct JobRequestContext final {
    RequestContext request;
    domain::JobId jobId;
    domain::JobAttempt jobAttempt;

    [[nodiscard]] constexpr bool operator==(const JobRequestContext&) const = default;
};

struct SaveRequestContext final {
    RequestContext request;
    domain::ProjectRevision projectRevision;

    [[nodiscard]] constexpr bool operator==(const SaveRequestContext&) const = default;
};

struct CommandContext final {
    domain::SessionId sessionId;
    domain::SessionEpoch sessionEpoch;
    domain::CommandId commandId;

    [[nodiscard]] constexpr bool operator==(const CommandContext&) const = default;
};

} // namespace dvs::application
