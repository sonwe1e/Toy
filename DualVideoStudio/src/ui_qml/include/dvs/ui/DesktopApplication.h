#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

class QUrl;

namespace dvs::ui {

class DualVideoSurface;
class ReviewController;
class ReviewPreferencesController;

struct DesktopApplicationOptions final {
    bool smokeMode = false;
    bool preferSoftwareDevice = false;
};

// Owns QGuiApplication, the QML engine, and the top-level window. The composition root is created
// only after this object so Qt's process-wide application state always outlives runtime QObjects.
class DesktopApplication final {
public:
    using SurfaceBinder = std::function<bool(DualVideoSurface&)>;

    DesktopApplication(int& argc,
                       char** argv,
                       DesktopApplicationOptions options = DesktopApplicationOptions{});
    ~DesktopApplication();

    DesktopApplication(const DesktopApplication&) = delete;
    DesktopApplication& operator=(const DesktopApplication&) = delete;
    DesktopApplication(DesktopApplication&&) = delete;
    DesktopApplication& operator=(DesktopApplication&&) = delete;

    [[nodiscard]] bool load(ReviewController& controller,
                            ReviewPreferencesController& preferences,
                            SurfaceBinder bindSurface);
    [[nodiscard]] int exec();
    void exit(int exitCode) noexcept;

    // Drives the same QML properties, button handlers, and window key events as a user. These
    // helpers keep the end-to-end smoke path on the declarative UI boundary instead of calling
    // the controller behind the controls.
    [[nodiscard]] bool setSelectedSourcesForAutomation(const QUrl& sourceA,
                                                       const QUrl& sourceB) noexcept;
    [[nodiscard]] bool clickControlForAutomation(std::string_view objectName) noexcept;
    [[nodiscard]] bool focusControlForAutomation(std::string_view objectName) noexcept;
    [[nodiscard]] bool clickTimelineForAutomation(double normalizedPosition) noexcept;
    [[nodiscard]] bool sendKeyForAutomation(int key) noexcept;
    [[nodiscard]] std::optional<std::string>
    objectStringPropertyForAutomation(std::string_view objectName,
                                      std::string_view propertyName) const;

    // Called after the runtime has detached its renderer services. Destroying the engine is the
    // scene-graph barrier that releases the render node's pinned GPU publication.
    void releaseSceneGraph() noexcept;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace dvs::ui
