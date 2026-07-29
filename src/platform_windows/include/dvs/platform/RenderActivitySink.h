#pragma once

namespace dvs::platform {

// Non-blocking wake-up boundary shared by the GPU-transfer actor and render thread. Implementations
// may coalesce notifications; neither caller waits for GUI work or application event capacity.
class IRenderActivitySink {
public:
    virtual ~IRenderActivitySink() = default;

    virtual void notifyFramePublished() noexcept = 0;
    virtual void notifyFrameRenderStarted() noexcept = 0;
    virtual void notifyAckPublished() noexcept = 0;
    virtual void notifyAckBackpressured() noexcept = 0;
};

} // namespace dvs::platform
