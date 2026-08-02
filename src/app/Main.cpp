#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "dvs/platform/ProcessTelemetry.h"
#include "dvs/ui/ComparisonSurface.h"
#include "dvs/ui/DesktopApplication.h"
#include "dvs/ui/GraphicsBackend.h"
#include "dvs/ui/ReviewController.h"
#include "dvs/ui/ReviewPreferencesController.h"
#include "dvs/ui/SourceListModel.h"

#include "ReviewRuntime.h"
#include "StartupFailureReporter.h"
#include "StartupRequest.h"
#include "StartupRequestBroker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

struct SmokeSources final {
    std::filesystem::path first;
    std::optional<std::filesystem::path> second;
    std::optional<std::filesystem::path> third;
};

enum class PerformanceComparisonMode {
    Side,
    Wipe,
    Difference,
};

struct PerformanceInvocation final {
    SmokeSources sources;
    std::chrono::seconds duration;
    PerformanceComparisonMode comparisonMode = PerformanceComparisonMode::Side;
};

struct PerformanceMetrics final {
    std::uint64_t presentedFrames = 0U;
    std::uint64_t droppedFrames = 0U;
    std::uint64_t sourceSplitObservations = 0U;
    std::size_t peakFrameBytes = 0U;
    std::size_t peakWorkingSetBytes = 0U;
    std::size_t baselineThreads = 0U;
    std::size_t peakThreads = 0U;
    std::size_t finalThreads = 0U;
    qint64 playbackResponseMilliseconds = -1;
    qint64 openFirstFrameMilliseconds = -1;
    qint64 seekP50Milliseconds = -1;
    qint64 seekP95Milliseconds = -1;
    qint64 warmStepP50Milliseconds = -1;
    qint64 warmStepP95Milliseconds = -1;
    qint64 analysisMilliseconds = -1;
    std::uint64_t analysisDecodedFrames = 0U;
    qint64 shutdownMilliseconds = -1;
    std::uint64_t comparisonSampledPixels = 0U;
    double comparisonBrightPixelRatio = 0.0;
    bool comparisonModeVerified = false;
    bool comparisonFrameRetained = false;
};

void writeStandardError(std::string_view message) noexcept {
    const HANDLE standardError = GetStdHandle(STD_ERROR_HANDLE);
    if (standardError == nullptr || standardError == INVALID_HANDLE_VALUE) {
        return;
    }
    while (!message.empty()) {
        const std::size_t chunkSize =
            std::min<std::size_t>(message.size(), (std::numeric_limits<DWORD>::max)());
        DWORD written = 0U;
        if (WriteFile(
                standardError, message.data(), static_cast<DWORD>(chunkSize), &written, nullptr) ==
                FALSE ||
            written == 0U) {
            return;
        }
        message.remove_prefix(written);
    }
}

[[nodiscard]] std::optional<std::chrono::seconds> parseDuration(const std::string_view text) {
    std::int64_t seconds = 0;
    const auto [position, error] = std::from_chars(text.data(), text.data() + text.size(), seconds);
    if (error != std::errc{} || position != text.data() + text.size() || seconds < 5 ||
        seconds > 3600) {
        return std::nullopt;
    }
    return std::chrono::seconds{seconds};
}

[[nodiscard]] std::optional<PerformanceComparisonMode>
parsePerformanceComparisonMode(const std::string_view text) {
    if (text == "side") {
        return PerformanceComparisonMode::Side;
    }
    if (text == "wipe") {
        return PerformanceComparisonMode::Wipe;
    }
    if (text == "diff") {
        return PerformanceComparisonMode::Difference;
    }
    return std::nullopt;
}

[[nodiscard]] std::string_view
performanceComparisonModeName(const PerformanceComparisonMode comparisonMode) noexcept {
    switch (comparisonMode) {
    case PerformanceComparisonMode::Side:
        return "side";
    case PerformanceComparisonMode::Wipe:
        return "wipe";
    case PerformanceComparisonMode::Difference:
        return "diff";
    }
    return "unknown";
}

[[nodiscard]] std::string_view
performanceModeControlName(const PerformanceComparisonMode comparisonMode) noexcept {
    switch (comparisonMode) {
    case PerformanceComparisonMode::Side:
        return "sideModeButton";
    case PerformanceComparisonMode::Wipe:
        return "wipeModeButton";
    case PerformanceComparisonMode::Difference:
        return "diffModeButton";
    }
    return {};
}

[[nodiscard]] dvs::ui::ComparisonSurface::ViewMode
performanceSurfaceMode(const PerformanceComparisonMode comparisonMode) noexcept {
    switch (comparisonMode) {
    case PerformanceComparisonMode::Side:
        return dvs::ui::ComparisonSurface::SideBySide;
    case PerformanceComparisonMode::Wipe:
        return dvs::ui::ComparisonSurface::Wipe;
    case PerformanceComparisonMode::Difference:
        return dvs::ui::ComparisonSurface::Difference;
    }
    return dvs::ui::ComparisonSurface::SideBySide;
}

[[nodiscard]] std::optional<PerformanceInvocation> parsePerformanceInvocation(const int argc,
                                                                              char** const argv) {
    if (argc < 5 || argv == nullptr || std::string_view{argv[1]} != "--ui-performance") {
        return std::nullopt;
    }
    std::vector<std::filesystem::path> sourcePaths;
    std::optional<std::chrono::seconds> duration;
    PerformanceComparisonMode comparisonMode = PerformanceComparisonMode::Side;
    for (int index = 2; index < argc;) {
        const std::string_view argument{argv[index]};
        if (argument == "--seconds") {
            if (duration.has_value() || index + 1 >= argc) {
                return std::nullopt;
            }
            duration = parseDuration(argv[index + 1]);
            if (!duration.has_value()) {
                return std::nullopt;
            }
            index += 2;
            continue;
        }
        if (argument == "--mode") {
            if (index + 1 >= argc) {
                return std::nullopt;
            }
            const std::optional<PerformanceComparisonMode> parsed =
                parsePerformanceComparisonMode(argv[index + 1]);
            if (!parsed.has_value()) {
                return std::nullopt;
            }
            comparisonMode = *parsed;
            index += 2;
            continue;
        }
        if (argument.starts_with("--") || sourcePaths.size() == 3U) {
            return std::nullopt;
        }
        sourcePaths.emplace_back(argv[index]);
        ++index;
    }
    if (sourcePaths.empty() || !duration.has_value() ||
        (comparisonMode != PerformanceComparisonMode::Side && sourcePaths.size() < 2U)) {
        return std::nullopt;
    }
    SmokeSources sources{.first = std::move(sourcePaths.front())};
    if (sourcePaths.size() >= 2U) {
        sources.second = std::move(sourcePaths[1U]);
    }
    if (sourcePaths.size() == 3U) {
        sources.third = std::move(sourcePaths[2U]);
    }
    return PerformanceInvocation{
        .sources = std::move(sources),
        .duration = *duration,
        .comparisonMode = comparisonMode,
    };
}

[[nodiscard]] double brightPixelRatio(const QImage& source, std::uint64_t& sampledPixels) {
    const QImage image = source.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) {
        sampledPixels = 0U;
        return 0.0;
    }
    const int horizontalStep = (std::max)(1, image.width() / 192);
    const int verticalStep = (std::max)(1, image.height() / 108);
    std::uint64_t brightPixels = 0U;
    sampledPixels = 0U;
    for (int y = 0; y < image.height(); y += verticalStep) {
        const auto* const row = image.constScanLine(y);
        for (int x = 0; x < image.width(); x += horizontalStep) {
            const int offset = x * 4;
            const unsigned int intensity = static_cast<unsigned int>(row[offset]) +
                                           static_cast<unsigned int>(row[offset + 1]) +
                                           static_cast<unsigned int>(row[offset + 2]);
            ++sampledPixels;
            if (intensity >= 96U) {
                ++brightPixels;
            }
        }
    }
    return sampledPixels == 0U
               ? 0.0
               : static_cast<double>(brightPixels) / static_cast<double>(sampledPixels);
}

[[nodiscard]] bool hasReviewError(const dvs::ui::ReviewController& controller) {
    return !controller.sourceAErrorKey().isEmpty() || !controller.sourceBErrorKey().isEmpty() ||
           !controller.sourceCErrorKey().isEmpty() || !controller.pairErrorKey().isEmpty();
}

[[nodiscard]] QUrl localFileUrl(const std::filesystem::path& path) {
    return QUrl::fromLocalFile(QString::fromStdWString(path.wstring()));
}

[[nodiscard]] bool applyStartupRequest(const dvs::app::StartupRequest& request,
                                       dvs::ui::DesktopApplication& desktop) {
    desktop.activateWindow();
    switch (request.kind) {
    case dvs::app::StartupRequest::Kind::Empty:
        return true;
    case dvs::app::StartupRequest::Kind::PlaySingle:
    case dvs::app::StartupRequest::Kind::Compare: {
        QList<QUrl> sources;
        sources.reserve(static_cast<qsizetype>(request.sources.size()));
        for (const auto& source : request.sources) {
            sources.push_back(localFileUrl(source));
        }
        return desktop.enqueueStartupRequest(static_cast<int>(request.kind), sources);
    }
    }
    return false;
}

[[nodiscard]] int runDesktop(int& argc,
                             char** argv,
                             const bool smokeMode,
                             const std::optional<SmokeSources>& smokeSources = std::nullopt,
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
    std::unique_ptr<dvs::app::StartupRequestBroker> startupBroker;
    dvs::app::StartupRequest startupRequest;
    if (!smokeMode) {
        const dvs::app::StartupRequestParseResult parsed =
            dvs::app::parseStartupRequest(QCoreApplication::arguments());
        if (!parsed) {
            return dvs::app::reportFatalStartup(parsed.error.toStdString(), false);
        }
        startupRequest = *parsed.request;
        startupBroker = std::make_unique<dvs::app::StartupRequestBroker>();
        const auto brokerResult = startupBroker->startOrForward(startupRequest);
        if (brokerResult == dvs::app::StartupRequestBroker::StartResult::Forwarded) {
            return EXIT_SUCCESS;
        }
        if (brokerResult == dvs::app::StartupRequestBroker::StartResult::Failed) {
            return dvs::app::reportFatalStartup(
                "The VCStation startup request broker could not be initialized.", false);
        }
    }
    std::unique_ptr<dvs::app::ReviewRuntime> runtime = dvs::app::ReviewRuntime::create();
    if (!runtime || runtime->controller() == nullptr || runtime->preferences() == nullptr ||
        !desktop.load(*runtime->controller(),
                      *runtime->preferences(),
                      [&runtime](dvs::ui::ComparisonSurface& surface) {
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
    if (!smokeMode && !applyStartupRequest(startupRequest, desktop)) {
        runtime->prepareForSceneGraphRelease();
        desktop.releaseSceneGraph();
        static_cast<void>(runtime->shutdownAfterSceneGraphRelease());
        return dvs::app::reportFatalStartup("The requested startup action could not be opened.",
                                            false);
    }
    if (startupBroker) {
        startupBroker->setRequestHandler([&desktop](dvs::app::StartupRequest request) {
            return applyStartupRequest(request, desktop);
        });
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
                std::cerr << "DVS_UI_SMOKE_MEDIA_ERROR"
                          << " stage=" << static_cast<int>(smokeStage)
                          << " sourceA=" << controller.sourceAErrorKey().toStdString()
                          << " sourceB=" << controller.sourceBErrorKey().toStdString()
                          << " sourceC=" << controller.sourceCErrorKey().toStdString()
                          << " comparison=" << controller.pairErrorKey().toStdString()
                          << " detail=" << controller.lastErrorTechnicalDetail().toStdString()
                          << '\n';
                desktop.exit(EXIT_FAILURE);
                return;
            }

            switch (smokeStage) {
            case SmokeStage::WaitingForGraphics: {
                if (!controller.graphicsReady()) {
                    return;
                }
                if (!smokeSources.has_value()) {
                    smokeCompleted = true;
                    desktop.exit(EXIT_SUCCESS);
                    return;
                }
                QList<QUrl> sources{localFileUrl(smokeSources->first)};
                if (smokeSources->second.has_value()) {
                    sources.push_back(localFileUrl(*smokeSources->second));
                }
                if (smokeSources->third.has_value()) {
                    sources.push_back(localFileUrl(*smokeSources->third));
                }
                const bool openAccepted = desktop.openSourcesForAutomation(sources);
                if (!openAccepted) {
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
            }
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
                if (!smokeSources->second.has_value()) {
                    if (!desktop.focusControlForAutomation("mediaViewportFocusTarget") ||
                        !desktop.sendKeyForAutomation(Qt::Key_End)) {
                        std::cerr << "DVS_UI_SMOKE_SINGLE_SHORTCUT_END_REJECTED\n";
                        desktop.exit(EXIT_FAILURE);
                        return;
                    }
                    smokeStage = SmokeStage::WaitingForShortcutLast;
                    return;
                }
                if (!desktop.focusControlForAutomation("sideModeButton") ||
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
                    if (!desktop.sendKeyForAutomation(Qt::Key_Right,
                                                      static_cast<int>(Qt::ShiftModifier))) {
                        std::cerr << "DVS_UI_SMOKE_SHORTCUT_SHIFT_RIGHT_REJECTED\n";
                        desktop.exit(EXIT_FAILURE);
                        return;
                    }
                    smokeStage = SmokeStage::WaitingForLargeStepForward;
                }
                return;
            case SmokeStage::WaitingForLargeStepForward:
                if (!controller.busy() && controller.totalFrames() > 0U) {
                    const qint64 expected =
                        std::min<qint64>(static_cast<qint64>(controller.totalFrames() - 1U), 5);
                    if (controller.currentFrame() != expected) {
                        return;
                    }
                    if (!desktop.sendKeyForAutomation(Qt::Key_Left,
                                                      static_cast<int>(Qt::ShiftModifier))) {
                        std::cerr << "DVS_UI_SMOKE_SHORTCUT_SHIFT_LEFT_REJECTED\n";
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
                    if (!smokeSources->second.has_value()) {
                        smokeCompleted = true;
                        desktop.exit(EXIT_SUCCESS);
                        return;
                    }
                    std::filesystem::path missingSource = smokeSources->first;
                    missingSource += ".missing";
                    QList<QUrl> errorSources{localFileUrl(missingSource)};
                    if (smokeSources->second.has_value()) {
                        errorSources.push_back(localFileUrl(*smokeSources->second));
                    }
                    const bool errorOpenAccepted = desktop.openSourcesForAutomation(errorSources);
                    if (!errorOpenAccepted) {
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

[[nodiscard]] int runUpgradeSettingsSmoke(int& argc, char** argv, const SmokeSources& sources) {
    if (!sources.second.has_value()) {
        writeStandardError("DVS_UPGRADE_SETTINGS_SMOKE_REQUIRES_TWO_SOURCES\n");
        return EXIT_FAILURE;
    }
    dvs::ui::configureGraphicsBackend();
    dvs::ui::DesktopApplication desktop{
        argc,
        argv,
        dvs::ui::DesktopApplicationOptions{
            .smokeMode = true,
            .preferSoftwareDevice = true,
        },
    };
    std::unique_ptr<dvs::app::ReviewRuntime> runtime = dvs::app::ReviewRuntime::create();
    if (!runtime || runtime->controller() == nullptr || runtime->preferences() == nullptr ||
        !desktop.load(*runtime->controller(),
                      *runtime->preferences(),
                      [&runtime](dvs::ui::ComparisonSurface& surface) {
                          return runtime->attachSurface(surface);
                      })) {
        writeStandardError("DVS_UPGRADE_SETTINGS_UI_LOAD_FAILED\n");
        return EXIT_FAILURE;
    }

    enum class Stage {
        WaitingForPreferences,
        WaitingForGraphics,
        WaitingForFirstFrame,
        WaitingForEffectiveState,
    };
    Stage stage = Stage::WaitingForPreferences;
    bool passed = false;
    bool failed = false;
    std::string failureReason;
    const auto fail = [&](std::string reason) {
        if (!failed) {
            failed = true;
            failureReason = std::move(reason);
            desktop.exit(EXIT_FAILURE);
        }
    };

    QTimer poll;
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, runtime->controller(), [&] {
        dvs::ui::ReviewController& controller = *runtime->controller();
        const dvs::ui::ReviewPreferencesController& preferences = *runtime->preferences();
        switch (stage) {
        case Stage::WaitingForPreferences:
            if (preferences.viewMode() != dvs::ui::ReviewPreferencesController::ViewMode::Wipe ||
                preferences.differenceEdge() !=
                    dvs::ui::ReviewPreferencesController::DifferenceEdge::Edge0And2) {
                return;
            }
            stage = Stage::WaitingForGraphics;
            return;
        case Stage::WaitingForGraphics: {
            if (!controller.graphicsReady()) {
                return;
            }
            const QList<QUrl> automationSources{localFileUrl(sources.first),
                                                localFileUrl(*sources.second)};
            if (!desktop.openSourcesForAutomation(automationSources)) {
                fail("upgrade-settings-open-rejected");
                return;
            }
            stage = Stage::WaitingForFirstFrame;
            return;
        }
        case Stage::WaitingForFirstFrame:
            if (controller.busy()) {
                return;
            }
            if (hasReviewError(controller)) {
                fail("upgrade-settings-media-error");
                return;
            }
            if (controller.currentFrame() != 0) {
                return;
            }
            stage = Stage::WaitingForEffectiveState;
            return;
        case Stage::WaitingForEffectiveState: {
            const std::optional<int> effectiveMode =
                desktop.objectIntPropertyForAutomation("dualVideoSurface", "viewMode");
            const std::optional<int> effectiveEdge =
                desktop.objectIntPropertyForAutomation("dualVideoSurface", "differenceEdge");
            if (!effectiveMode.has_value() || !effectiveEdge.has_value()) {
                return;
            }
            if (*effectiveMode != static_cast<int>(dvs::ui::ComparisonSurface::Wipe) ||
                *effectiveEdge != static_cast<int>(dvs::ui::ComparisonSurface::Edge0And1)) {
                return;
            }
            passed = true;
            desktop.exit(EXIT_SUCCESS);
            return;
        }
        }
    });

    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(15'000);
    QObject::connect(&timeout, &QTimer::timeout, runtime->controller(), [&] {
        fail("upgrade-settings-timeout");
    });
    poll.start();
    timeout.start();
    int result = desktop.exec();
    poll.stop();
    timeout.stop();
    if (!passed) {
        result = EXIT_FAILURE;
        if (!failed) {
            failureReason = "upgrade-settings-incomplete";
        }
        writeStandardError("DVS_UPGRADE_SETTINGS_SMOKE_FAILED " + failureReason + '\n');
    }

    runtime->prepareForSceneGraphRelease();
    desktop.releaseSceneGraph();
    if (!runtime->shutdownAfterSceneGraphRelease()) {
        writeStandardError("DVS_RUNTIME_SHUTDOWN_TIMEOUT\n");
        return EXIT_FAILURE;
    }
    return result;
}

[[nodiscard]] int runPerformance(int& argc,
                                 char** argv,
                                 const SmokeSources& sources,
                                 const std::chrono::seconds duration,
                                 const PerformanceComparisonMode comparisonMode) {
    constexpr auto kWarmup = std::chrono::seconds{2};
    constexpr std::size_t kMaximumFrameBytes = 256U * 1024U * 1024U;
    dvs::ui::configureGraphicsBackend();
    dvs::ui::DesktopApplication desktop{
        argc,
        argv,
        dvs::ui::DesktopApplicationOptions{
            .smokeMode = false,
            .preferSoftwareDevice = false,
            .preferHighRefreshScreen = true,
        },
    };
    std::unique_ptr<dvs::app::ReviewRuntime> runtime = dvs::app::ReviewRuntime::create();
    if (!runtime || runtime->controller() == nullptr || runtime->preferences() == nullptr ||
        !desktop.load(*runtime->controller(),
                      *runtime->preferences(),
                      [&runtime](dvs::ui::ComparisonSurface& surface) {
                          return runtime->attachSurface(surface);
                      })) {
        writeStandardError("DVS_PERFORMANCE_UI_LOAD_FAILED\n");
        return EXIT_FAILURE;
    }
    enum class Stage {
        WaitingForGraphics,
        WaitingForFirstFrame,
        WaitingForComparisonMode,
        WaitingForComparisonPixels,
        WaitingForPlayback,
        Running,
        WaitingForPause,
        Seeking,
        WarmStepping,
        Analyzing,
    };
    Stage stage = Stage::WaitingForGraphics;
    PerformanceMetrics metrics;
    QElapsedTimer responseTimer;
    QElapsedTimer openTimer;
    QElapsedTimer playbackTimer;
    QElapsedTimer seekTimer;
    QElapsedTimer warmStepTimer;
    QElapsedTimer analysisTimer;
    QElapsedTimer comparisonModeTimer;
    qint64 lastCountedFrame = -1;
    qint64 comparisonFrameBeforeSwitch = -1;
    std::vector<qint64> seekTargets;
    std::vector<qint64> seekMilliseconds;
    std::vector<qint64> warmStepMilliseconds;
    std::vector<dvs::media::DecoderBackendStatus> playbackBackends;
    dvs::platform::GpuTransferStatistics playbackTransfer;
    std::optional<dvs::ui::RenderAckRelayStatistics> playbackRelayBaseline;
    std::optional<dvs::ui::RenderAckRelayStatistics> playbackRelayEnd;
    std::size_t seekIndex = 0U;
    std::size_t warmStepIndex = 0U;
    qint64 warmStepTarget = -1;
    std::uint64_t analysisSignatureBaseline = 0U;
    bool analysisObservedRunning = false;
    bool completed = false;
    bool failed = false;
    std::string failureReason;
    const std::size_t expectedSourceCount = 1U +
                                            static_cast<std::size_t>(sources.second.has_value()) +
                                            static_cast<std::size_t>(sources.third.has_value());
    const bool requiresComparisonPixels =
        expectedSourceCount > 1U && comparisonMode != PerformanceComparisonMode::Side;

    const auto fail = [&](std::string reason) {
        if (!failed) {
            failed = true;
            failureReason = std::move(reason);
            desktop.exit(EXIT_FAILURE);
        }
    };
    const auto samplePresentedSources = [&] {
        auto* const model = runtime->controller()->sources();
        const qint64 canonical = runtime->controller()->currentFrame();
        if (model == nullptr || canonical < 0 ||
            model->rowCount() != static_cast<int>(expectedSourceCount)) {
            ++metrics.sourceSplitObservations;
            return;
        }
        for (int row = 0; row < model->rowCount(); ++row) {
            const QVariant sourceFrame =
                model->data(model->index(row, 0), dvs::ui::SourceListModel::CurrentSourceFrameRole);
            if (!sourceFrame.isValid() || sourceFrame.toLongLong() != canonical) {
                ++metrics.sourceSplitObservations;
                return;
            }
        }
    };
    const auto sampleFrame = [&] {
        if (stage != Stage::Running) {
            return;
        }
        const qint64 current = runtime->controller()->currentFrame();
        if (current < 0 || current == lastCountedFrame) {
            return;
        }
        if (playbackTimer.elapsed() < kWarmup.count() * 1000) {
            return;
        }
        if (lastCountedFrame >= 0 && current <= lastCountedFrame) {
            fail("canonical-frame-regressed");
            return;
        }
        lastCountedFrame = current;
        samplePresentedSources();
    };
    QObject::connect(runtime->controller(),
                     &dvs::ui::ReviewController::stateChanged,
                     runtime->controller(),
                     sampleFrame);
    const auto startSeek = [&] {
        if (seekIndex >= seekTargets.size()) {
            return false;
        }
        seekTimer.start();
        return runtime->controller()->seekFrame(seekTargets[seekIndex]);
    };
    const auto startWarmStep = [&] {
        constexpr std::size_t kWarmStepSamples = 20U;
        if (warmStepIndex >= kWarmStepSamples) {
            return false;
        }
        const qint64 delta = (warmStepIndex % 2U) == 0U ? 1 : -1;
        warmStepTarget = runtime->controller()->currentFrame() + delta;
        warmStepTimer.start();
        return runtime->controller()->stepFrames(delta);
    };

    QTimer poll;
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, runtime->controller(), [&] {
        dvs::ui::ReviewController& controller = *runtime->controller();
        if (hasReviewError(controller) && !controller.busy()) {
            fail("media-error:" + controller.lastErrorTechnicalDetail().toStdString());
            return;
        }

        metrics.peakFrameBytes = std::max(metrics.peakFrameBytes, runtime->reservedFrameBytes());

        switch (stage) {
        case Stage::WaitingForGraphics: {
            if (!controller.graphicsReady()) {
                return;
            }
            QList<QUrl> automationSources{localFileUrl(sources.first)};
            if (sources.second.has_value()) {
                automationSources.push_back(localFileUrl(*sources.second));
            }
            if (sources.third.has_value()) {
                automationSources.push_back(localFileUrl(*sources.third));
            }
            const bool openAccepted = desktop.openSourcesForAutomation(automationSources);
            if (!openAccepted) {
                fail("open-rejected");
                return;
            }
            openTimer.start();
            stage = Stage::WaitingForFirstFrame;
            return;
        }
        case Stage::WaitingForFirstFrame:
            if (controller.busy() || controller.currentFrame() != 0) {
                return;
            }
            metrics.openFirstFrameMilliseconds = openTimer.elapsed();
            metrics.baselineThreads = dvs::platform::sampleCurrentProcessTelemetry().threadCount;
            metrics.peakThreads = metrics.baselineThreads;
            if (expectedSourceCount > 1U) {
                comparisonFrameBeforeSwitch = controller.currentFrame();
                if (!desktop.clickControlForAutomation(
                        performanceModeControlName(comparisonMode))) {
                    fail("comparison-mode-click-rejected");
                    return;
                }
                stage = Stage::WaitingForComparisonMode;
                return;
            }
            metrics.comparisonModeVerified = true;
            responseTimer.start();
            if (!controller.play()) {
                fail("play-rejected");
                return;
            }
            stage = Stage::WaitingForPlayback;
            return;
        case Stage::WaitingForComparisonMode: {
            if (controller.busy() || comparisonFrameBeforeSwitch < 0 ||
                controller.currentFrame() != comparisonFrameBeforeSwitch) {
                fail("comparison-mode-lost-frame");
                return;
            }
            const std::optional<int> effectiveMode =
                desktop.objectIntPropertyForAutomation("dualVideoSurface", "viewMode");
            if (!effectiveMode.has_value()) {
                fail("comparison-mode-surface-missing");
                return;
            }
            if (*effectiveMode != static_cast<int>(performanceSurfaceMode(comparisonMode))) {
                return;
            }
            comparisonModeTimer.start();
            stage = Stage::WaitingForComparisonPixels;
            return;
        }
        case Stage::WaitingForComparisonPixels:
            // A scene-graph frame must be allowed to consume the selected mode before its pixels
            // are captured. The capture is intentionally performed through the visible surface.
            if (controller.busy() || comparisonFrameBeforeSwitch < 0 ||
                controller.currentFrame() != comparisonFrameBeforeSwitch) {
                fail("comparison-mode-lost-frame");
                return;
            }
            if (comparisonModeTimer.elapsed() < 100) {
                return;
            }
            if (requiresComparisonPixels) {
                const std::optional<QImage> image =
                    desktop.captureControlForAutomation("dualVideoSurface");
                if (!image.has_value()) {
                    fail("comparison-mode-capture-failed");
                    return;
                }
                metrics.comparisonBrightPixelRatio =
                    brightPixelRatio(*image, metrics.comparisonSampledPixels);
                metrics.comparisonModeVerified = metrics.comparisonSampledPixels > 0U &&
                                                 metrics.comparisonBrightPixelRatio >= 0.01;
                if (!metrics.comparisonModeVerified) {
                    fail("comparison-mode-black-output");
                    return;
                }
            } else {
                metrics.comparisonModeVerified = true;
            }
            metrics.comparisonFrameRetained = true;
            responseTimer.start();
            if (!controller.play()) {
                fail("play-rejected");
                return;
            }
            stage = Stage::WaitingForPlayback;
            return;
        case Stage::WaitingForPlayback:
            if (!controller.playing()) {
                return;
            }
            metrics.playbackResponseMilliseconds = responseTimer.elapsed();
            playbackTimer.start();
            stage = Stage::Running;
            return;
        case Stage::Running:
            sampleFrame();
            if (!controller.playing() && playbackTimer.elapsed() < duration.count() * 1000) {
                fail("playback-ended-before-duration");
                return;
            }
            if (!playbackRelayBaseline.has_value() &&
                playbackTimer.elapsed() >= kWarmup.count() * 1000) {
                playbackRelayBaseline = runtime->renderRelayStatistics();
            }
            if (playbackTimer.elapsed() < duration.count() * 1000) {
                return;
            }
            playbackRelayEnd = runtime->renderRelayStatistics();
            if (!controller.pause()) {
                fail("pause-rejected");
                return;
            }
            stage = Stage::WaitingForPause;
            return;
        case Stage::WaitingForPause:
            if (controller.playing()) {
                return;
            }
            playbackBackends = runtime->decoderBackendStatuses();
            playbackTransfer = runtime->transferStatistics();
            if (controller.totalFrames() < 100U) {
                fail("insufficient-frames-for-seek-sampling");
                return;
            }
            seekTargets.reserve(20U);
            for (std::size_t index = 0U; index < 20U; ++index) {
                const qulonglong percentile = ((index * 37U) % 97U) + 1U;
                const qulonglong frame = (controller.totalFrames() - 1U) * percentile / 100U;
                seekTargets.push_back(static_cast<qint64>(frame));
            }
            if (!startSeek()) {
                fail("seek-rejected");
                return;
            }
            stage = Stage::Seeking;
            return;
        case Stage::Seeking:
            if (controller.busy() || controller.currentFrame() != seekTargets[seekIndex]) {
                return;
            }
            seekMilliseconds.push_back(seekTimer.elapsed());
            ++seekIndex;
            if (seekIndex < seekTargets.size()) {
                if (!startSeek()) {
                    fail("seek-rejected");
                }
                return;
            }
            std::ranges::sort(seekMilliseconds);
            metrics.seekP50Milliseconds = seekMilliseconds[9U];
            metrics.seekP95Milliseconds = seekMilliseconds[18U];
            if (!startWarmStep()) {
                fail("warm-step-rejected");
                return;
            }
            stage = Stage::WarmStepping;
            return;
        case Stage::WarmStepping:
            if (controller.busy() || controller.currentFrame() != warmStepTarget) {
                return;
            }
            warmStepMilliseconds.push_back(warmStepTimer.elapsed());
            ++warmStepIndex;
            if (warmStepIndex < 20U) {
                if (!startWarmStep()) {
                    fail("warm-step-rejected");
                }
                return;
            }
            std::ranges::sort(warmStepMilliseconds);
            metrics.warmStepP50Milliseconds = warmStepMilliseconds[9U];
            metrics.warmStepP95Milliseconds = warmStepMilliseconds[18U];
            if (expectedSourceCount == 1U) {
                metrics.finalThreads = dvs::platform::sampleCurrentProcessTelemetry().threadCount;
                completed = true;
                desktop.exit(EXIT_SUCCESS);
                return;
            }
            analysisSignatureBaseline = runtime->decodedSignatureCount();
            analysisTimer.start();
            if (!controller.estimateAlignment()) {
                fail("analysis-rejected");
                return;
            }
            stage = Stage::Analyzing;
            return;
        case Stage::Analyzing:
            if (controller.alignmentAnalysisRunning()) {
                analysisObservedRunning = true;
                return;
            }
            if (!analysisObservedRunning || controller.busy()) {
                return;
            }
            metrics.analysisMilliseconds = analysisTimer.elapsed();
            metrics.analysisDecodedFrames =
                runtime->decodedSignatureCount() - analysisSignatureBaseline;
            metrics.finalThreads = dvs::platform::sampleCurrentProcessTelemetry().threadCount;
            completed = true;
            desktop.exit(EXIT_SUCCESS);
            return;
        }
    });

    QTimer telemetryPoll;
    telemetryPoll.setInterval(250);
    QObject::connect(&telemetryPoll, &QTimer::timeout, runtime->controller(), [&] {
        const dvs::platform::ProcessTelemetry telemetry =
            dvs::platform::sampleCurrentProcessTelemetry();
        metrics.peakWorkingSetBytes =
            std::max(metrics.peakWorkingSetBytes, telemetry.workingSetBytes);
        metrics.peakThreads = std::max(metrics.peakThreads, telemetry.threadCount);
    });

    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(static_cast<int>((duration + std::chrono::seconds{90}).count() * 1000));
    QObject::connect(
        &timeout, &QTimer::timeout, runtime->controller(), [&] { fail("performance-timeout"); });
    poll.start();
    telemetryPoll.start();
    timeout.start();
    int result = desktop.exec();
    poll.stop();
    telemetryPoll.stop();
    timeout.stop();

    const dvs::platform::GpuTransferStatistics transfer = runtime->transferStatistics();
    const dvs::ui::RenderAckRelayStatistics relay = runtime->renderRelayStatistics();
    const dvs::media::MediaProbeStatistics probe = runtime->mediaProbeStatistics();
    const dvs::media::FrameProviderStatistics provider = runtime->frameProviderStatistics();
    const std::vector<dvs::media::DecoderBackendStatus> backends =
        runtime->decoderBackendStatuses();
    if (playbackRelayBaseline.has_value() && playbackRelayEnd.has_value()) {
        metrics.presentedFrames = playbackRelayEnd->acknowledgementsPopped -
                                  playbackRelayBaseline->acknowledgementsPopped;
        metrics.droppedFrames =
            playbackRelayEnd->canonicalFrameGaps - playbackRelayBaseline->canonicalFrameGaps;
        if (playbackRelayEnd->canonicalFrameRegressions !=
            playbackRelayBaseline->canonicalFrameRegressions) {
            failed = true;
            failureReason = "canonical-frame-regressed";
            result = EXIT_FAILURE;
        }
    } else {
        if (!failed) {
            failed = true;
            failureReason = "playback-ack-window-missing";
        }
        result = EXIT_FAILURE;
    }
    const std::uint64_t totalCounted = metrics.presentedFrames + metrics.droppedFrames;
    const double dropRatio = totalCounted == 0U ? 1.0
                                                : static_cast<double>(metrics.droppedFrames) /
                                                      static_cast<double>(totalCounted);
    const bool allHardware = backends.size() == expectedSourceCount &&
                             std::all_of(backends.begin(), backends.end(), [](const auto& status) {
                                 return status.backend == dvs::media::DecoderBackend::D3d11Va &&
                                        status.deviceGeneration.value() != 0U;
                             });
    const std::uint64_t completedDecodes =
        std::accumulate(playbackBackends.begin(),
                        playbackBackends.end(),
                        std::uint64_t{0U},
                        [](const std::uint64_t total, const auto& status) {
                            return total + status.completedDecodeCount;
                        });
    const std::uint64_t cacheHits = std::accumulate(
        playbackBackends.begin(),
        playbackBackends.end(),
        std::uint64_t{0U},
        [](const std::uint64_t total, const auto& status) { return total + status.cacheHitCount; });
    const std::uint64_t exactSeeks =
        std::accumulate(playbackBackends.begin(),
                        playbackBackends.end(),
                        std::uint64_t{0U},
                        [](const std::uint64_t total, const auto& status) {
                            return total + status.exactSeekCount;
                        });
    const std::uint64_t totalDecodeMicroseconds =
        std::accumulate(playbackBackends.begin(),
                        playbackBackends.end(),
                        std::uint64_t{0U},
                        [](const std::uint64_t total, const auto& status) {
                            return total + status.totalDecodeMicroseconds;
                        });
    const std::uint64_t maximumDecodeMicroseconds =
        std::accumulate(playbackBackends.begin(),
                        playbackBackends.end(),
                        std::uint64_t{0U},
                        [](const std::uint64_t maximum, const auto& status) {
                            return std::max(maximum, status.maximumDecodeMicroseconds);
                        });
    const double cacheHitRatio = completedDecodes == 0U ? 0.0
                                                        : static_cast<double>(cacheHits) /
                                                              static_cast<double>(completedDecodes);
    const double analysisFramesPerSecond =
        metrics.analysisMilliseconds <= 0
            ? 0.0
            : static_cast<double>(metrics.analysisDecodedFrames) * 1000.0 /
                  static_cast<double>(metrics.analysisMilliseconds);
    if (!completed || metrics.sourceSplitObservations != 0U || dropRatio > 0.005 ||
        metrics.playbackResponseMilliseconds < 0 || metrics.playbackResponseMilliseconds > 100 ||
        metrics.seekP95Milliseconds < 0 || metrics.seekP95Milliseconds > 500 ||
        metrics.warmStepP95Milliseconds < 0 ||
        (expectedSourceCount > 1U && metrics.analysisDecodedFrames == 0U) ||
        metrics.peakFrameBytes > kMaximumFrameBytes || !allHardware ||
        metrics.finalThreads > metrics.baselineThreads + 2U || !metrics.comparisonModeVerified ||
        (expectedSourceCount > 1U && !metrics.comparisonFrameRetained) ||
        transfer.deviceLossReports != 0U) {
        result = EXIT_FAILURE;
    }

    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    runtime->prepareForSceneGraphRelease();
    desktop.releaseSceneGraph();
    const bool shutdownCompleted = runtime->shutdownAfterSceneGraphRelease();
    metrics.shutdownMilliseconds = shutdownTimer.elapsed();
    if (!shutdownCompleted || metrics.shutdownMilliseconds > 7000) {
        result = EXIT_FAILURE;
    }

    QJsonObject report;
    const auto addNumber = [&report](const QString& key, const auto value) {
        report.insert(key, static_cast<double>(value));
    };
    addNumber(QStringLiteral("duration_seconds"), duration.count());
    addNumber(QStringLiteral("screen_refresh_hz"), desktop.activeScreenRefreshRate());
    addNumber(QStringLiteral("presented_frames"), metrics.presentedFrames);
    addNumber(QStringLiteral("dropped_frames"), metrics.droppedFrames);
    addNumber(QStringLiteral("drop_ratio"), dropRatio);
    addNumber(QStringLiteral("source_split_observations"), metrics.sourceSplitObservations);
    addNumber(QStringLiteral("open_first_frame_ms"), metrics.openFirstFrameMilliseconds);
    addNumber(QStringLiteral("playback_response_ms"), metrics.playbackResponseMilliseconds);
    addNumber(QStringLiteral("cold_seek_p50_ms"), metrics.seekP50Milliseconds);
    addNumber(QStringLiteral("seek_p95_ms"), metrics.seekP95Milliseconds);
    QJsonArray seekSamples;
    for (const qint64 sample : seekMilliseconds) {
        seekSamples.append(static_cast<double>(sample));
    }
    report.insert(QStringLiteral("seek_samples_ms"), seekSamples);
    addNumber(QStringLiteral("warm_step_p50_ms"), metrics.warmStepP50Milliseconds);
    addNumber(QStringLiteral("warm_step_p95_ms"), metrics.warmStepP95Milliseconds);
    addNumber(QStringLiteral("analysis_ms"), metrics.analysisMilliseconds);
    addNumber(QStringLiteral("analysis_decoded_frames"), metrics.analysisDecodedFrames);
    addNumber(QStringLiteral("analysis_frames_per_second"), analysisFramesPerSecond);
    const std::string_view comparisonModeName = performanceComparisonModeName(comparisonMode);
    report.insert(QStringLiteral("comparison_mode"),
                  QString::fromUtf8(comparisonModeName.data(),
                                    static_cast<qsizetype>(comparisonModeName.size())));
    addNumber(QStringLiteral("comparison_sampled_pixels"), metrics.comparisonSampledPixels);
    addNumber(QStringLiteral("comparison_bright_pixel_ratio"), metrics.comparisonBrightPixelRatio);
    report.insert(QStringLiteral("comparison_mode_verified"), metrics.comparisonModeVerified);
    report.insert(QStringLiteral("comparison_frame_retained"), metrics.comparisonFrameRetained);
    addNumber(QStringLiteral("shutdown_ms"), metrics.shutdownMilliseconds);
    addNumber(QStringLiteral("peak_frame_bytes"), metrics.peakFrameBytes);
    addNumber(QStringLiteral("peak_working_set_bytes"), metrics.peakWorkingSetBytes);
    addNumber(QStringLiteral("baseline_threads"), metrics.baselineThreads);
    addNumber(QStringLiteral("peak_threads"), metrics.peakThreads);
    addNumber(QStringLiteral("final_threads"), metrics.finalThreads);
    addNumber(QStringLiteral("submitted_sets"), playbackTransfer.submittedSets);
    addNumber(QStringLiteral("replaced_sets"), playbackTransfer.replacedSets);
    addNumber(QStringLiteral("published_sets"), playbackTransfer.publishedSets);
    addNumber(QStringLiteral("failed_sets"), transfer.failedSets);
    addNumber(QStringLiteral("cancelled_sets"), transfer.cancelledSets);
    addNumber(QStringLiteral("transfer_average_us"),
              playbackTransfer.completedTransfers == 0U
                  ? 0U
                  : playbackTransfer.totalTransferMicroseconds /
                        playbackTransfer.completedTransfers);
    addNumber(QStringLiteral("transfer_maximum_us"), playbackTransfer.maximumTransferMicroseconds);
    addNumber(QStringLiteral("zero_copy_sets"), playbackTransfer.zeroCopySets);
    addNumber(QStringLiteral("render_frame_notifications"), relay.frameNotifications);
    addNumber(QStringLiteral("render_ack_notifications"), relay.ackNotifications);
    addNumber(QStringLiteral("render_ack_backpressure"), relay.ackBackpressureNotifications);
    addNumber(QStringLiteral("render_acknowledgements"), relay.acknowledgementsPopped);
    addNumber(QStringLiteral("render_canonical_gaps"), relay.canonicalFrameGaps);
    addNumber(QStringLiteral("render_canonical_regressions"), relay.canonicalFrameRegressions);
    addNumber(QStringLiteral("render_update_requests"), relay.updateRequests);
    addNumber(QStringLiteral("render_item_updates"), relay.itemUpdates);
    addNumber(QStringLiteral("render_frame_to_start_average_us"),
              relay.frameToRenderSamples == 0U
                  ? 0U
                  : relay.totalFrameToRenderMicroseconds / relay.frameToRenderSamples);
    addNumber(QStringLiteral("render_start_to_ack_average_us"),
              relay.renderToAckSamples == 0U
                  ? 0U
                  : relay.totalRenderToAckMicroseconds / relay.renderToAckSamples);
    addNumber(QStringLiteral("render_frame_to_ack_average_us"),
              relay.frameToAckSamples == 0U
                  ? 0U
                  : relay.totalFrameToAckMicroseconds / relay.frameToAckSamples);
    addNumber(QStringLiteral("render_frame_to_ack_maximum_us"),
              relay.maximumFrameToAckMicroseconds);
    addNumber(QStringLiteral("device_loss_reports"), transfer.deviceLossReports);
    addNumber(QStringLiteral("decoder_calls"), completedDecodes);
    addNumber(QStringLiteral("decoder_cache_hits"), cacheHits);
    addNumber(QStringLiteral("decoder_cache_hit_ratio"), cacheHitRatio);
    addNumber(QStringLiteral("decoder_exact_seeks"), exactSeeks);
    addNumber(QStringLiteral("decoder_average_us"),
              completedDecodes == 0U ? 0U : totalDecodeMicroseconds / completedDecodes);
    addNumber(QStringLiteral("decoder_maximum_us"), maximumDecodeMicroseconds);
    addNumber(QStringLiteral("probe_index_count"), probe.completedProbes);
    addNumber(QStringLiteral("probe_index_average_us"),
              probe.completedProbes == 0U
                  ? 0U
                  : probe.totalProbeIndexMicroseconds / probe.completedProbes);
    addNumber(QStringLiteral("probe_index_maximum_us"), probe.maximumProbeIndexMicroseconds);
    addNumber(QStringLiteral("frameset_assembly_count"), provider.assembledFrameSets);
    addNumber(QStringLiteral("frameset_assembly_average_us"),
              provider.assembledFrameSets == 0U
                  ? 0U
                  : provider.totalAssemblyMicroseconds / provider.assembledFrameSets);
    addNumber(QStringLiteral("frameset_assembly_maximum_us"), provider.maximumAssemblyMicroseconds);
    addNumber(QStringLiteral("frameset_cache_hits"), provider.frameSetCacheHits);

    QJsonArray sourceDecode;
    for (std::size_t index = 0U; index < playbackBackends.size(); ++index) {
        const auto& backend = playbackBackends[index];
        sourceDecode.append(QJsonObject{
            {QStringLiteral("source_id"), static_cast<double>(backend.sourceId)},
            {QStringLiteral("calls"), static_cast<double>(backend.completedDecodeCount)},
            {QStringLiteral("average_us"),
             static_cast<double>(backend.completedDecodeCount == 0U
                                     ? 0U
                                     : backend.totalDecodeMicroseconds /
                                           backend.completedDecodeCount)},
            {QStringLiteral("maximum_us"), static_cast<double>(backend.maximumDecodeMicroseconds)},
        });
    }
    report.insert(QStringLiteral("per_source_decode"), sourceDecode);
    addNumber(QStringLiteral("expected_source_count"), expectedSourceCount);
    report.insert(QStringLiteral("all_d3d11va"), allHardware);
    report.insert(QStringLiteral("shutdown_completed"), shutdownCompleted);
    report.insert(QStringLiteral("passed"), result == EXIT_SUCCESS);
    if (failed) {
        report.insert(QStringLiteral("failure"), QString::fromStdString(failureReason));
    }
    const auto encodedReport = QJsonDocument{report}.toJson(QJsonDocument::Compact);
    writeStandardError("DVS_PERFORMANCE_RESULT " + encodedReport.toStdString() + '\n');
    if (!shutdownCompleted) {
        writeStandardError("DVS_RUNTIME_SHUTDOWN_TIMEOUT\n");
    }
    return result;
}

} // namespace

int main(int argc, char* argv[]) {
    const bool smokeArgument = argc >= 2 && std::string_view{argv[1]}.starts_with("--ui-");
    try {
        if (argc == 2 && std::string_view{argv[1]} == "--ui-stderr-smoke") {
            writeStandardError("DVS_GUI_STDERR_OK\n");
            return EXIT_SUCCESS;
        }
        if (argc == 2 && std::string_view{argv[1]} == "--ui-smoke") {
            return runDesktop(argc, argv, true);
        }
        if (argc == 3 && std::string_view{argv[1]} == "--ui-smoke") {
            return runDesktop(argc,
                              argv,
                              true,
                              SmokeSources{
                                  .first = std::filesystem::path{argv[2]},
                              });
        }
        if (argc == 4 && std::string_view{argv[1]} == "--ui-smoke") {
            return runDesktop(argc,
                              argv,
                              true,
                              SmokeSources{
                                  .first = std::filesystem::path{argv[2]},
                                  .second = std::filesystem::path{argv[3]},
                              });
        }
        if (argc == 5 && std::string_view{argv[1]} == "--ui-smoke") {
            return runDesktop(argc,
                              argv,
                              true,
                              SmokeSources{
                                  .first = std::filesystem::path{argv[2]},
                                  .second = std::filesystem::path{argv[3]},
                                  .third = std::filesystem::path{argv[4]},
                              });
        }
        if (argc == 4 && std::string_view{argv[1]} == "--ui-upgrade-settings-smoke") {
            return runUpgradeSettingsSmoke(argc,
                                           argv,
                                           SmokeSources{
                                               .first = std::filesystem::path{argv[2]},
                                               .second = std::filesystem::path{argv[3]},
                                           });
        }
        if (argc >= 2 && std::string_view{argv[1]} == "--ui-performance") {
            const std::optional<PerformanceInvocation> invocation =
                parsePerformanceInvocation(argc, argv);
            if (!invocation.has_value()) {
                return dvs::app::reportFatalStartup(
                    "Usage: --ui-performance <one to three sources> --seconds <5-3600> "
                    "[--mode side|wipe|diff]. Wipe and diff require at least two sources.",
                    true);
            }
            return runPerformance(
                argc, argv, invocation->sources, invocation->duration, invocation->comparisonMode);
        }
        if (argc == 4 && std::string_view{argv[1]} == "--ui-shutdown-smoke") {
            return runDesktop(argc,
                              argv,
                              true,
                              SmokeSources{
                                  .first = std::filesystem::path{argv[2]},
                                  .second = std::filesystem::path{argv[3]},
                              },
                              true);
        }
        if (argc == 2 && std::string_view{argv[1]} == "--ui-fatal-startup-smoke") {
            return dvs::app::reportFatalStartup("DVS_UI_FATAL_STARTUP_SMOKE", true);
        }
        return runDesktop(argc, argv, false);
    } catch (const std::exception& exception) {
        return dvs::app::reportFatalStartup(exception.what(), smokeArgument);
    } catch (...) {
        return dvs::app::reportFatalStartup("Unknown fatal startup exception.", smokeArgument);
    }
}
