#pragma once

#include "dvs/platform/GraphicsDeviceBroker.h"
#include "dvs/ui/GraphicsBackend.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQuickGraphicsConfiguration>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRenderNode>
#include <QScreen>
#include <QThread>

#include <atomic>
#include <cstdint>

namespace dvs::test {

class BrokerRenderNode final : public QSGRenderNode {
public:
    BrokerRenderNode(QQuickWindow& window,
                     platform::GraphicsDeviceBroker& broker,
                     std::atomic<std::uint64_t>& renderCount,
                     std::atomic<ui::GraphicsBackendResult>& firstResult,
                     std::atomic<ui::GraphicsBackendResult>& latestResult) noexcept
        : window_(window), broker_(broker), renderCount_(renderCount), firstResult_(firstResult),
          latestResult_(latestResult) {}

    void render(const RenderState*) override {
        const ui::GraphicsBackendResult result =
            ui::bindGraphicsBackendOnRenderThread(window_, broker_);
        latestResult_.store(result, std::memory_order_relaxed);
        const std::uint64_t previous = renderCount_.load(std::memory_order_relaxed);
        if (previous == 0U) {
            firstResult_.store(result, std::memory_order_relaxed);
        }
        renderCount_.fetch_add(1U, std::memory_order_release);
    }

    [[nodiscard]] StateFlags changedStates() const override {
        return {};
    }

    [[nodiscard]] RenderingFlags flags() const override {
        return BoundedRectRendering;
    }

    [[nodiscard]] QRectF rect() const override {
        return QRectF{0.0, 0.0, 16.0, 16.0};
    }

private:
    QQuickWindow& window_;
    platform::GraphicsDeviceBroker& broker_;
    std::atomic<std::uint64_t>& renderCount_;
    std::atomic<ui::GraphicsBackendResult>& firstResult_;
    std::atomic<ui::GraphicsBackendResult>& latestResult_;
};

class BrokerRenderItem final : public QQuickItem {
public:
    BrokerRenderItem(platform::GraphicsDeviceBroker& broker,
                     std::atomic<std::uint64_t>& renderCount,
                     std::atomic<ui::GraphicsBackendResult>& firstResult,
                     std::atomic<ui::GraphicsBackendResult>& latestResult,
                     QQuickItem* parent)
        : QQuickItem(parent), broker_(broker), renderCount_(renderCount), firstResult_(firstResult),
          latestResult_(latestResult) {
        setFlag(ItemHasContents, true);
        setSize(QSizeF{16.0, 16.0});
    }

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override {
        if (oldNode != nullptr) {
            return oldNode;
        }
        return new BrokerRenderNode{*window(), broker_, renderCount_, firstResult_, latestResult_};
    }

private:
    platform::GraphicsDeviceBroker& broker_;
    std::atomic<std::uint64_t>& renderCount_;
    std::atomic<ui::GraphicsBackendResult>& firstResult_;
    std::atomic<ui::GraphicsBackendResult>& latestResult_;
};

// Boots the real Qt Quick scene graph through an exposed, transparent 16x16 top-level window.
// Recent Qt/DXGI versions reject exposed windows outside the virtual desktop, so the transparent
// window stays inside the primary screen while setPreferSoftwareDevice(true) selects WARP.
class MinimalQuickRenderHarness final {
public:
    explicit MinimalQuickRenderHarness(platform::GraphicsDeviceBroker& broker)
        : item_(broker, renderCount_, firstResult_, latestResult_, window_.contentItem()) {
        QQuickGraphicsConfiguration configuration;
        configuration.setPreferSoftwareDevice(true);
        window_.setGraphicsConfiguration(configuration);
        window_.setColor(Qt::black);
        window_.setFlags(Qt::FramelessWindowHint);
        window_.setGeometry(100, 100, 16, 16);
        window_.setOpacity(0.0);
        item_.setVisible(false);
    }

    ~MinimalQuickRenderHarness() {
        window_.hide();
        window_.releaseResources();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    MinimalQuickRenderHarness(const MinimalQuickRenderHarness&) = delete;
    MinimalQuickRenderHarness& operator=(const MinimalQuickRenderHarness&) = delete;

    [[nodiscard]] bool showAndWait(const int timeoutMilliseconds = 10'000) {
        window_.show();
        if (!waitUntil([this] { return window_.isExposed(); }, timeoutMilliseconds)) {
            return false;
        }

        QScreen* const screen = QGuiApplication::primaryScreen();
        if (screen == nullptr) {
            return false;
        }
        const QRect available = screen->availableGeometry();
        window_.setPosition(available.right() - window_.width() + 1,
                            available.bottom() - window_.height() + 1);
        item_.setVisible(true);
        window_.requestUpdate();
        item_.update();
        return waitUntil(
            [this] {
                return window_.isExposed() && isUnobtrusive() &&
                       renderCount_.load(std::memory_order_acquire) > 0U;
            },
            timeoutMilliseconds);
    }

    void requestRender() {
        item_.update();
        window_.requestUpdate();
    }

    [[nodiscard]] bool waitForRenderCount(const std::uint64_t count,
                                          const int timeoutMilliseconds = 10'000) {
        return waitUntil(
            [this, count] { return renderCount_.load(std::memory_order_acquire) >= count; },
            timeoutMilliseconds);
    }

    void hide() {
        window_.hide();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    [[nodiscard]] bool isExposed() const noexcept {
        return window_.isExposed();
    }

    [[nodiscard]] bool isUnobtrusive() const noexcept {
        return window_.opacity() == 0.0;
    }

    [[nodiscard]] std::uint64_t renderCount() const noexcept {
        return renderCount_.load(std::memory_order_acquire);
    }

    [[nodiscard]] ui::GraphicsBackendResult firstResult() const noexcept {
        return firstResult_.load(std::memory_order_acquire);
    }

    [[nodiscard]] ui::GraphicsBackendResult latestResult() const noexcept {
        return latestResult_.load(std::memory_order_acquire);
    }

private:
    template <typename Predicate>
    [[nodiscard]] static bool waitUntil(Predicate predicate, const int timeoutMilliseconds) {
        QElapsedTimer timer;
        timer.start();
        while (!predicate() && timer.elapsed() < timeoutMilliseconds) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::yieldCurrentThread();
        }
        return predicate();
    }

    QQuickWindow window_;
    std::atomic<std::uint64_t> renderCount_{0U};
    std::atomic<ui::GraphicsBackendResult> firstResult_{ui::GraphicsBackendResult::Closed};
    std::atomic<ui::GraphicsBackendResult> latestResult_{ui::GraphicsBackendResult::Closed};
    BrokerRenderItem item_;
};

} // namespace dvs::test
