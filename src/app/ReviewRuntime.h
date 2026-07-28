#pragma once

#include <memory>

namespace dvs::ui {
class ComparisonSurface;
class ReviewController;
class ReviewPreferencesController;
} // namespace dvs::ui

namespace dvs::app {

// Owns the complete direct-review adapter graph. The desktop host releases the QML scene graph
// between prepareForSceneGraphRelease() and shutdownAfterSceneGraphRelease() so the render node
// drops its pinned front pair before the transfer actor is drained.
class ReviewRuntime final {
public:
    [[nodiscard]] static std::unique_ptr<ReviewRuntime> create();
    ~ReviewRuntime();

    ReviewRuntime(const ReviewRuntime&) = delete;
    ReviewRuntime& operator=(const ReviewRuntime&) = delete;
    ReviewRuntime(ReviewRuntime&&) = delete;
    ReviewRuntime& operator=(ReviewRuntime&&) = delete;

    [[nodiscard]] ui::ReviewController* controller() noexcept;
    [[nodiscard]] ui::ReviewPreferencesController* preferences() noexcept;
    [[nodiscard]] bool attachSurface(ui::ComparisonSurface& surface) noexcept;

    // GUI-thread phase. Stops UI ingress and detaches non-owning QML/render references without
    // waiting on media, render, or acknowledgement workers.
    void prepareForSceneGraphRelease() noexcept;

    // Called after the desktop host has destroyed its QML engine/window. Adapter teardown is
    // transferred to a detached control task and this call returns within the seven-second total
    // shutdown budget. Controller and relay QObjects remain owned by the creating GUI thread.
    [[nodiscard]] bool shutdownAfterSceneGraphRelease() noexcept;

private:
    ReviewRuntime();

    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::app
