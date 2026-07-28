#pragma once

#include "dvs/application/RequestContext.h"
#include "dvs/domain/MediaDescriptor.h"

#include <cstdint>
#include <filesystem>
#include <variant>

namespace dvs::application {

// Commands are immutable values submitted to the coordinator. The caller supplies a context from
// its last snapshot so commands queued for an older source epoch are rejected deterministically.
struct OpenDirectSourcesCommand final {
    CommandContext context;
    domain::MediaDescriptor sourceA;
    domain::MediaDescriptor sourceB;
};

// The UI/controller accepts local URLs and normalizes them before submitting this value. The
// coordinator owns probing and pair validation, so unprobed paths never masquerade as media
// descriptors at this boundary.
struct OpenSourcePathsCommand final {
    CommandContext context;
    std::filesystem::path sourceAPath;
    std::filesystem::path sourceBPath;
};

struct SeekFrameCommand final {
    CommandContext context;
    domain::FrameId frameId;
};

struct StepFramesCommand final {
    CommandContext context;
    std::int64_t delta = 0;
};

struct FirstFrameCommand final {
    CommandContext context;
};

struct LastFrameCommand final {
    CommandContext context;
};

struct PlayCommand final {
    CommandContext context;
};

struct PauseCommand final {
    CommandContext context;
};

struct CloseSessionCommand final {
    CommandContext context;
};

using PlaybackCommand = std::variant<OpenDirectSourcesCommand,
                                     OpenSourcePathsCommand,
                                     SeekFrameCommand,
                                     StepFramesCommand,
                                     FirstFrameCommand,
                                     LastFrameCommand,
                                     PlayCommand,
                                     PauseCommand,
                                     CloseSessionCommand>;

[[nodiscard]] inline const CommandContext& commandContext(const PlaybackCommand& command) noexcept {
    return std::visit([](const auto& value) -> const CommandContext& { return value.context; },
                      command);
}

} // namespace dvs::application
