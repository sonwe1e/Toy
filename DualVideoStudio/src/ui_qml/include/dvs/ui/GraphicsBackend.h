#pragma once

class QQuickWindow;

namespace dvs::platform {
class GraphicsDeviceBroker;
}

namespace dvs::ui {

enum class GraphicsBackendResult {
    Ready,
    AlreadyReady,
    UnsupportedGraphicsApi,
    MissingNativeResource,
    DeviceRejected,
    BrokerBusy,
    NotificationQueueFull,
    Closed,
};

// Must be called before QGuiApplication construction. This fixes Qt Quick to the public D3D11
// renderer backend for the process; per-window configuration can still prefer WARP in tests.
void configureGraphicsBackend() noexcept;

// Called only from QSGRenderNode::render(). The borrowed pointers returned by Qt are passed to the
// platform broker, which AddRefs and validates them. This function does not post application
// events and never calls beginExternalCommands()/endExternalCommands().
[[nodiscard]] GraphicsBackendResult
bindGraphicsBackendOnRenderThread(QQuickWindow& window,
                                  platform::GraphicsDeviceBroker& broker) noexcept;

} // namespace dvs::ui
