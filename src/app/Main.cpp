#include "dvs/ui/DesktopApplication.h"
#include "dvs/ui/GraphicsBackend.h"
#include "dvs/ui/ReviewController.h"
#include "dvs/ui/ReviewPreferencesController.h"

#include "ReviewRuntime.h"
#include "StartupFailureReporter.h"

#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

enum class SmokeStage {
    WaitingForGraphics,
    WaitingForFirstFrame,
    WaitingForNextFrame,
    WaitingForPreviousFrame,
    WaitingForLastFrame,
    WaitingForReturnToFirst,
    WaitingForPlaybackAdvance,
    WaitingForPlaybackPause,
    WaitingForPlaybackReturnToFirst,
    WaitingForFocusedComboShortcuts,
    WaitingForShortcutLast,
    WaitingForShortcutFirst,
    WaitingForShortcutNext,
    WaitingForShortcutPrevious,
    WaitingForLargeStepForward,
    WaitingForLargeStepBackward,
    WaitingForTimelineMiddle,
    WaitingForTimelineLast,
    WaitingForLocalizedError,
};

[[nodiscard]] bool hasReviewError(const dvs::ui::ReviewController& controller) {
    return !controller.sourceAErrorKey().isEmpty() || !controller.sourceBErrorKey().isEmpty() ||
           !controller.pairErrorKey().isEmpty();
}

[[nodiscard]] QUrl localFileUrl(const std::filesystem::path& path) {
    return QUrl::fromLocalFile(QString::fromStdWString(path.wstring()));
}

[[nodiscard]] int
runDesktop(int& argc,
           char** argv,
           const bool smokeMode,
           const std::optional<std::array<std::filesystem::path, 2U>>& smokeSources = std::nullopt,
           const bool shutdownDuringOpen = false) {
    dvs::ui::configureGraphicsBackend();
    dvs::ui::DesktopApplication desktop{
        argc,
        argv,
        dvs::ui::DesktopApplicationOptions{
            .smokeMode = smokeMode,
            .preferSoftwareDevice = smokeMode,
        },
    };
    std::unique_ptr<dvs::app::ReviewRuntime> runtime = dvs::app::ReviewRuntime::create();
    if (!runtime || runtime->controller() == nullptr || runtime->preferences() == nullptr ||
        !desktop.load(*runtime->controller(),
                      *runtime->preferences(),
                      [&runtime](dvs::ui::DualVideoSurface& surface) {
                          return runtime->attachSurface(surface);
                      })) {
        std::cerr << "DVS_UI_LOAD_FAILED\n";
        if (runtime) {
            runtime->prepareForSceneGraphRelease();
        }
        desktop.releaseSceneGraph();
        if (runtime && !runtime->shutdownAfterSceneGraphRelease()) {
            std::cerr << "DVS_RUNTIME_SHUTDOWN_TIMEOUT\n" << std::flush;
            static_cast<void>(dvs::app::reportFatalStartup(
                "DVS_UI_LOAD_FAILED; DVS_RUNTIME_SHUTDOWN_TIMEOUT", smokeMode));
            std::_Exit(EXIT_FAILURE);
        }
        return dvs::app::reportFatalStartup("DVS_UI_LOAD_FAILED", smokeMode);
    }

    QTimer smokePoll;
    QTimer smokeTimeout;
    SmokeStage smokeStage = SmokeStage::WaitingForGraphics;
    bool smokeCompleted = false;
    if (smokeMode) {
        smokePoll.setInterval(5);
        QObject::connect(&smokePoll, &QTimer::timeout, runtime->controller(), [&] {
            dvs::ui::ReviewController& controller = *runtime->controller();
            if (smokeStage != SmokeStage::WaitingForLocalizedError && hasReviewError(controller) &&
                !controller.busy()) {
                std::cerr << "DVS_UI_SMOKE_MEDIA_ERROR\n";
                desktop.exit(EXIT_FAILURE);
                return;
            }

            switch (smokeStage) {
            case SmokeStage::WaitingForGraphics:
                if (!controller.graphicsReady()) {
                    return;
                }
                if (!smokeSources.has_value()) {
                    smokeCompleted = true;
                    desktop.exit(EXIT_SUCCESS);
                    return;
                }
                if (!desktop.setSelectedSourcesForAutomation(localFileUrl((*smokeSources)[0U]),
                                                             localFileUrl((*smokeSources)[1U])) ||
                    !desktop.clickControlForAutomation("openPairButton")) {
                    std::cerr << "DVS_UI_SMOKE_OPEN_REJECTED\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                smokeStage = SmokeStage::WaitingForFirstFrame;
                if (shutdownDuringOpen) {
                    if (!controller.busy()) {
                        std::cerr << "DVS_UI_SHUTDOWN_SMOKE_OPEN_NOT_PENDING\n";
                        desktop.exit(EXIT_FAILURE);
                        return;
                    }
                    smokeCompleted = true;
                    desktop.exit(EXIT_SUCCESS);
                }
                return;
            case SmokeStage::WaitingForFirstFrame:
                if (controller.busy() || controller.currentFrame() != 0) {
                    return;
                }
                if (!desktop.clickControlForAutomation("nextButton")) {
                    std::cerr << "DVS_UI_SMOKE_NEXT_REJECTED\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                smokeStage = SmokeStage::WaitingForNextFrame;
                return;
            case SmokeStage::WaitingForNextFrame:
                if (controller.busy() || controller.currentFrame() != 1) {
                    return;
                }
                if (!desktop.clickControlForAutomation("previousButton")) {
                    std::cerr << "DVS_UI_SMOKE_PREVIOUS_REJECTED\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                smokeStage = SmokeStage::WaitingForPreviousFrame;
                return;
            case SmokeStage::WaitingForPreviousFrame:
                if (controller.busy() || controller.currentFrame() != 0) {
                    return;
                }
                if (!desktop.clickControlForAutomation("lastButton")) {
                    std::cerr << "DVS_UI_SMOKE_LAST_REJECTED\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                smokeStage = SmokeStage::WaitingForLastFrame;
                return;
            case SmokeStage::WaitingForLastFrame:
                if (controller.busy() || controller.totalFrames() == 0U ||
                    controller.currentFrame() !=
                        static_cast<qint64>(controller.totalFrames() - 1U)) {
                    return;
                }
                if (!desktop.clickControlForAutomation("firstButton")) {
                    std::cerr << "DVS_UI_SMOKE_FIRST_REJECTED\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                smokeStage = SmokeStage::WaitingForReturnToFirst;
                return;
            case SmokeStage::WaitingForReturnToFirst:
                if (controller.busy() || controller.currentFrame() != 0) {
                    return;
                }
                if (!desktop.clickControlForAutomation("playbackButton")) {
                    std::cerr << "DVS_UI_SMOKE_PLAY_REJECTED\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                smokeStage = SmokeStage::WaitingForPlaybackAdvance;
                return;
            case SmokeStage::WaitingForPlaybackAdvance:
                if (controller.currentFrame() <= 0 || !controller.canPause()) {
                    return;
                }
                if (!desktop.focusControlForAutomation("mediaViewportFocusTarget") ||
                    !desktop.sendKeyForAutomation(Qt::Key_Space)) {
                    std::cerr << "DVS_UI_SMOKE_SHORTCUT_PAUSE_REJECTED\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                smokeStage = SmokeStage::WaitingForPlaybackPause;
                return;
            case SmokeStage::WaitingForPlaybackPause:
                if (controller.playing() || !controller.canFirst()) {
                    return;
                }
                if (!desktop.clickControlForAutomation("firstButton")) {
                    std::cerr << "DVS_UI_SMOKE_PLAYBACK_FIRST_REJECTED\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                smokeStage = SmokeStage::WaitingForPlaybackReturnToFirst;
                return;
            case SmokeStage::WaitingForPlaybackReturnToFirst:
                if (controller.busy() || controller.currentFrame() != 0) {
                    return;
                }
                if (!desktop.focusControlForAutomation("viewModeCombo") ||
                    !desktop.sendKeyForAutomation(Qt::Key_Space) ||
                    !desktop.sendKeyForAutomation(Qt::Key_Up)) {
                    std::cerr << "DVS_UI_SMOKE_FOCUSED_COMBO_KEYS_REJECTED\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                smokeStage = SmokeStage::WaitingForFocusedComboShortcuts;
                return;
            case SmokeStage::WaitingForFocusedComboShortcuts:
                if (controller.playing() || controller.busy() || controller.currentFrame() != 0 ||
                    !controller.canPlay()) {
                    std::cerr << "DVS_UI_SMOKE_FOCUSED_COMBO_TRIGGERED_MEDIA_SHORTCUT\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                if (!desktop.sendKeyForAutomation(Qt::Key_Escape) ||
                    !desktop.focusControlForAutomation("mediaViewportFocusTarget") ||
                    !desktop.sendKeyForAutomation(Qt::Key_End)) {
                    std::cerr << "DVS_UI_SMOKE_SHORTCUT_END_REJECTED\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                smokeStage = SmokeStage::WaitingForShortcutLast;
                return;
            case SmokeStage::WaitingForShortcutLast:
                if (controller.busy() || controller.totalFrames() == 0U ||
                    controller.currentFrame() !=
                        static_cast<qint64>(controller.totalFrames() - 1U)) {
                    return;
                }
                if (!desktop.sendKeyForAutomation(Qt::Key_Home)) {
                    std::cerr << "DVS_UI_SMOKE_SHORTCUT_HOME_REJECTED\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                smokeStage = SmokeStage::WaitingForShortcutFirst;
                return;
            case SmokeStage::WaitingForShortcutFirst:
                if (controller.busy() || controller.currentFrame() != 0) {
                    return;
                }
                if (!desktop.sendKeyForAutomation(Qt::Key_Right)) {
                    std::cerr << "DVS_UI_SMOKE_SHORTCUT_RIGHT_REJECTED\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                smokeStage = SmokeStage::WaitingForShortcutNext;
                return;
            case SmokeStage::WaitingForShortcutNext:
                if (controller.busy() || controller.currentFrame() != 1) {
                    return;
                }
                if (!desktop.sendKeyForAutomation(Qt::Key_Left)) {
                    std::cerr << "DVS_UI_SMOKE_SHORTCUT_LEFT_REJECTED\n";
                    desktop.exit(EXIT_FAILURE);
                    return;
                }
                smokeStage = SmokeStage::WaitingForShortcutPrevious;
                return;
            case SmokeStage::WaitingForShortcutPrevious:
                if (!controller.busy() && controller.currentFrame() == 0) {
                    if (!desktop.sendKeyForAutomation(Qt::Key_Up)) {
                        std::cerr << "DVS_UI_SMOKE_SHORTCUT_UP_REJECTED\n";
                        desktop.exit(EXIT_FAILURE);
                        return;
                    }
                    smokeStage = SmokeStage::WaitingForLargeStepForward;
                }
                return;
            case SmokeStage::WaitingForLargeStepForward:
                if (!controller.busy() && controller.totalFrames() > 0U) {
                    const qint64 expected =
                        std::min<qint64>(static_cast<qint64>(controller.totalFrames() - 1U),
                                         runtime->preferences()->largeStepFrames());
                    if (controller.currentFrame() != expected) {
                        return;
                    }
                    if (!desktop.sendKeyForAutomation(Qt::Key_Down)) {
                        std::cerr << "DVS_UI_SMOKE_SHORTCUT_DOWN_REJECTED\n";
                        desktop.exit(EXIT_FAILURE);
                        return;
                    }
                    smokeStage = SmokeStage::WaitingForLargeStepBackward;
                }
                return;
            case SmokeStage::WaitingForLargeStepBackward:
                if (!controller.busy() && controller.currentFrame() == 0) {
                    if (!desktop.clickTimelineForAutomation(0.5)) {
                        std::cerr << "DVS_UI_SMOKE_TIMELINE_MIDDLE_REJECTED\n";
                        desktop.exit(EXIT_FAILURE);
                        return;
                    }
                    smokeStage = SmokeStage::WaitingForTimelineMiddle;
                }
                return;
            case SmokeStage::WaitingForTimelineMiddle:
                if (!controller.busy() && controller.totalFrames() > 0U &&
                    controller.currentFrame() ==
                        static_cast<qint64>((controller.totalFrames() - 1U + 1U) / 2U)) {
                    if (!desktop.clickTimelineForAutomation(1.0)) {
                        std::cerr << "DVS_UI_SMOKE_TIMELINE_LAST_REJECTED\n";
                        desktop.exit(EXIT_FAILURE);
                        return;
                    }
                    smokeStage = SmokeStage::WaitingForTimelineLast;
                }
                return;
            case SmokeStage::WaitingForTimelineLast:
                if (!controller.busy() && controller.totalFrames() > 0U &&
                    controller.currentFrame() ==
                        static_cast<qint64>(controller.totalFrames() - 1U)) {
                    std::filesystem::path missingSource = (*smokeSources)[0U];
                    missingSource += ".missing";
                    if (!desktop.setSelectedSourcesForAutomation(
                            localFileUrl(missingSource), localFileUrl((*smokeSources)[1U])) ||
                        !desktop.clickControlForAutomation("openPairButton")) {
                        std::cerr << "DVS_UI_SMOKE_ERROR_PATH_REJECTED\n";
                        desktop.exit(EXIT_FAILURE);
                        return;
                    }
                    smokeStage = SmokeStage::WaitingForLocalizedError;
                }
                return;
            case SmokeStage::WaitingForLocalizedError:
                if (controller.busy() || !hasReviewError(controller)) {
                    return;
                }
                if (const std::optional<std::string> detail =
                        desktop.objectStringPropertyForAutomation("frameErrorBannerDetail", "text");
                    !detail.has_value() || detail->find("source-missing") != std::string::npos ||
                    detail->find("missing or cannot be read") == std::string::npos ||
                    desktop.objectStringPropertyForAutomation("frameErrorBanner", "visible") !=
                        std::optional<std::string>{"true"} ||
                    desktop.objectStringPropertyForAutomation("statusOverlay", "visible") !=
                        std::optional<std::string>{"false"}) {
                    std::cerr << "DVS_UI_SMOKE_ERROR_NOT_LOCALIZED\n";
                    desktop.exit(EXIT_FAILURE);
                } else {
                    smokeCompleted = true;
                    desktop.exit(EXIT_SUCCESS);
                }
                return;
            }
        });
        smokeTimeout.setSingleShot(true);
        smokeTimeout.setInterval(30'000);
        QObject::connect(
            &smokeTimeout, &QTimer::timeout, runtime->controller(), [&desktop, &smokeStage] {
                std::cerr << "DVS_UI_SMOKE_TIMEOUT stage=" << static_cast<int>(smokeStage) << '\n';
                desktop.exit(EXIT_FAILURE);
            });
        smokePoll.start();
        smokeTimeout.start();
    }

    int result = desktop.exec();
    smokePoll.stop();
    smokeTimeout.stop();
    if (smokeMode && !smokeCompleted) {
        std::cerr << "DVS_UI_SMOKE_INCOMPLETE\n";
        result = EXIT_FAILURE;
    }
    runtime->prepareForSceneGraphRelease();
    desktop.releaseSceneGraph();
    if (!runtime->shutdownAfterSceneGraphRelease()) {
        std::cerr << "DVS_RUNTIME_SHUTDOWN_TIMEOUT\n" << std::flush;
        static_cast<void>(dvs::app::reportFatalStartup("DVS_RUNTIME_SHUTDOWN_TIMEOUT", smokeMode));
        std::_Exit(EXIT_FAILURE);
    }
    return result;
}

} // namespace

int main(int argc, char* argv[]) {
    const bool smokeArgument = argc >= 2 && std::string_view{argv[1]}.starts_with("--ui-");
    try {
        if (argc == 1) {
            return runDesktop(argc, argv, false);
        }
        if (argc == 2 && std::string_view{argv[1]} == "--ui-smoke") {
            return runDesktop(argc, argv, true);
        }
        if (argc == 4 && std::string_view{argv[1]} == "--ui-smoke") {
            return runDesktop(argc,
                              argv,
                              true,
                              std::array<std::filesystem::path, 2U>{
                                  std::filesystem::path{argv[2]},
                                  std::filesystem::path{argv[3]},
                              });
        }
        if (argc == 4 && std::string_view{argv[1]} == "--ui-shutdown-smoke") {
            return runDesktop(argc,
                              argv,
                              true,
                              std::array<std::filesystem::path, 2U>{
                                  std::filesystem::path{argv[2]},
                                  std::filesystem::path{argv[3]},
                              },
                              true);
        }
        if (argc == 2 && std::string_view{argv[1]} == "--ui-fatal-startup-smoke") {
            return dvs::app::reportFatalStartup("DVS_UI_FATAL_STARTUP_SMOKE", true);
        }
        return dvs::app::reportFatalStartup(
            "Unsupported GUI argument. Use DualVideoStudioCli.exe for diagnostics.", smokeArgument);
    } catch (const std::exception& exception) {
        return dvs::app::reportFatalStartup(exception.what(), smokeArgument);
    } catch (...) {
        return dvs::app::reportFatalStartup("Unknown fatal startup exception.", smokeArgument);
    }
}
