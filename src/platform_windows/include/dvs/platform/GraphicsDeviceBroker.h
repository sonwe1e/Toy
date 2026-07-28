#pragma once

#include "dvs/application/Events.h"
#include "dvs/domain/Identifiers.h"

#include <cstdint>
#include <d3d11.h>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <wrl/client.h>

namespace dvs::platform {

enum class GraphicsDeviceBrokerResult {
    Ready,
    AlreadyReady,
    Unavailable,
    AlreadyUnavailable,
    Lost,
    Busy,
    StaleGeneration,
    NotificationQueueFull,
    Closed,
};

struct GraphicsDeviceLease final {
    domain::DeviceGeneration deviceGeneration{0U};
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediateContext;
};

enum class GraphicsDeviceLeaseStatus {
    Available,
    Unavailable,
    Busy,
    Closed,
};

struct GraphicsDeviceLeaseResult final {
    GraphicsDeviceLeaseStatus status = GraphicsDeviceLeaseStatus::Unavailable;
    std::optional<GraphicsDeviceLease> lease;
};

using GraphicsDeviceNotification = std::variant<application::GraphicsDeviceReady,
                                                application::GraphicsDeviceUnavailable,
                                                application::GraphicsDeviceLost>;

// The broker retains Qt Quick's existing D3D11 device and immediate context. It never creates a
// device. adoptQtDevice() is called from the scene-graph render thread; notification polling is a
// separate non-blocking path so GUI/render code never posts into the application critical queue.
// Under pressure, older queued transitions remain ordered and the latest authoritative state is
// coalesced; notification capacity can therefore never veto device invalidation.
class GraphicsDeviceBroker final {
public:
    GraphicsDeviceBroker();
    ~GraphicsDeviceBroker();

    GraphicsDeviceBroker(const GraphicsDeviceBroker&) = delete;
    GraphicsDeviceBroker& operator=(const GraphicsDeviceBroker&) = delete;
    GraphicsDeviceBroker(GraphicsDeviceBroker&&) = delete;
    GraphicsDeviceBroker& operator=(GraphicsDeviceBroker&&) = delete;

    [[nodiscard]] GraphicsDeviceBrokerResult
    adoptQtDevice(ID3D11Device* device, ID3D11DeviceContext* immediateContext) noexcept;

    [[nodiscard]] GraphicsDeviceBrokerResult
    reportUnavailable(std::string technicalDetail) noexcept;
    [[nodiscard]] GraphicsDeviceBrokerResult
    reportDeviceLost(domain::DeviceGeneration expectedGeneration, HRESULT reason) noexcept;

    [[nodiscard]] GraphicsDeviceLeaseResult tryLease() const noexcept;
    [[nodiscard]] std::optional<GraphicsDeviceNotification> tryConsumeNotification() noexcept;
    [[nodiscard]] domain::DeviceGeneration currentGeneration() const noexcept;

    // Shutdown is owned by the composition thread after render/transfer users are quiescent.
    // Already-published notifications remain drainable, but no later transition is admitted.
    void shutdown() noexcept;
    [[nodiscard]] bool isClosed() const noexcept;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::platform
