#include "RuntimeBridges.h"

#include "dvs/media/DecoderBackend.h"
#include "dvs/media/MultiSourceFrameProvider.h"

#include <QMetaObject>

#include <utility>

namespace dvs::app::detail {

void RenderActivityBridge::bind(std::shared_ptr<platform::IRenderActivitySink> sink) noexcept {
    sink_.store(std::move(sink), std::memory_order_release);
}

void RenderActivityBridge::unbind() noexcept {
    sink_.store({}, std::memory_order_release);
}

void RenderActivityBridge::notifyFramePublished() noexcept {
    if (const auto sink = sink_.load(std::memory_order_acquire)) {
        sink->notifyFramePublished();
    }
}

void RenderActivityBridge::notifyFrameRenderStarted() noexcept {
    if (const auto sink = sink_.load(std::memory_order_acquire)) {
        sink->notifyFrameRenderStarted();
    }
}

void RenderActivityBridge::notifyAckPublished() noexcept {
    if (const auto sink = sink_.load(std::memory_order_acquire)) {
        sink->notifyAckPublished();
    }
}

void RenderActivityBridge::notifyAckBackpressured() noexcept {
    if (const auto sink = sink_.load(std::memory_order_acquire)) {
        sink->notifyAckBackpressured();
    }
}

void ReviewProjectionBridge::bind(ui::ReviewController& controller) noexcept {
    std::scoped_lock lock(mutex_);
    controller_ = &controller;
}

void ReviewProjectionBridge::unbind() noexcept {
    std::scoped_lock lock(mutex_);
    controller_ = nullptr;
}

void ReviewProjectionBridge::notify() noexcept {
    std::scoped_lock lock(mutex_);
    if (controller_ == nullptr) {
        return;
    }
    ui::ReviewController* const controller = controller_;
    static_cast<void>(QMetaObject::invokeMethod(
        controller, [controller] { controller->refreshProjection(); }, Qt::QueuedConnection));
}

void DecoderBackendStateCache::refresh(
    const std::weak_ptr<media::MultiSourceFrameProvider>& weakProvider) noexcept {
    try {
        const auto provider = weakProvider.lock();
        if (!provider) {
            states_.store({}, std::memory_order_release);
            return;
        }

        const std::vector<media::DecoderBackendStatus> backendStatuses =
            provider->decoderBackendStatuses();
        std::vector<ui::ReviewController::DecoderBackendState> next;
        next.reserve(backendStatuses.size());
        for (const media::DecoderBackendStatus& status : backendStatuses) {
            const bool initialized = status.backend == media::DecoderBackend::D3d11Va ||
                                     status.deviceGeneration.value() != 0U ||
                                     !status.fallbackReason.empty() ||
                                     status.completedDecodeCount != 0U;
            if (!initialized) {
                continue;
            }
            next.push_back(ui::ReviewController::DecoderBackendState{
                .sourceId = status.sourceId,
                .d3d11Va = status.backend == media::DecoderBackend::D3d11Va,
                .fallbackReason = status.fallbackReason,
            });
        }
        states_.store(
            std::make_shared<const std::vector<ui::ReviewController::DecoderBackendState>>(
                std::move(next)),
            std::memory_order_release);
    } catch (...) {
        states_.store({}, std::memory_order_release);
    }
}

std::vector<ui::ReviewController::DecoderBackendState>
DecoderBackendStateCache::snapshot() const noexcept {
    try {
        const auto states = states_.load(std::memory_order_acquire);
        return states ? *states : std::vector<ui::ReviewController::DecoderBackendState>{};
    } catch (...) {
        return {};
    }
}

} // namespace dvs::app::detail
