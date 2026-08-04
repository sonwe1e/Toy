#pragma once

#include "dvs/application/RequestContext.h"

#include <cstdint>

namespace dvs::application {

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

} // namespace dvs::application
