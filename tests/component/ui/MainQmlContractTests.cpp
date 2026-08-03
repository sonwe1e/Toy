#include "dvs/application/Alignment.h"
#include "dvs/application/SessionSnapshot.h"
#include "dvs/ui/ComparisonSurface.h"
#include "dvs/ui/GraphicsBackend.h"
#include "dvs/ui/ReviewController.h"
#include "dvs/ui/ReviewPreferencesController.h"
#include "dvs/ui/ReviewShellController.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QResource>
#include <QStringList>
#include <QThread>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqml.h>

#include <array>
#include <gtest/gtest.h>
#include <memory>
#include <utility>
#include <vector>

void initializeMainQmlContractResources() {
    Q_INIT_RESOURCE(dvs_ui_qml_resources);
}

namespace {

[[nodiscard]] std::string componentErrors(const QQmlComponent& component) {
    QStringList messages;
    for (const QQmlError& error : component.errors()) {
        messages.push_back(error.toString());
    }
    return messages.join(QStringLiteral("\n")).toStdString();
}

void sendKey(QWindow& window,
             const int key,
             const Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent press{QEvent::KeyPress, key, modifiers};
    QKeyEvent release{QEvent::KeyRelease, key, modifiers};
    QCoreApplication::sendEvent(&window, &press);
    QCoreApplication::sendEvent(&window, &release);
    QCoreApplication::processEvents();
}

} // namespace

namespace dvs::ui {
namespace {

class ClosedSettingsRepository final : public application::ISettingsRepository {
public:
    [[nodiscard]] application::PortSubmitResult
    submit(const application::SettingsLoadRequest&,
           std::shared_ptr<application::IApplicationEventSink>) override {
        return application::PortSubmitResult::Closed;
    }

    [[nodiscard]] application::PortSubmitResult
    submit(const application::SettingsSaveRequest&,
           std::shared_ptr<application::IApplicationEventSink>) override {
        return application::PortSubmitResult::Closed;
    }

    void cancel(const application::RequestContext&) noexcept override {}
};

TEST(MainQmlContractTests, MapsEveryCurrentMediaErrorAndExcludesDeletedUiDomains) {
    QFile mainQml{QStringLiteral(":/qml/Main.qml")};
    ASSERT_TRUE(mainQml.open(QIODevice::ReadOnly));
    const QString source = QString::fromUtf8(mainQml.readAll());
    constexpr std::array<std::string_view, 28U> kMediaErrorKeys{
        "invalid-argument",
        "invalid-rate",
        "invalid-frame-id",
        "invalid-frame-count",
        "invalid-dimensions",
        "invalid-duration",
        "invalid-media-descriptor",
        "arithmetic-overflow",
        "source-frame-rate-mismatch",
        "source-frame-count-mismatch",
        "source-duration-mismatch",
        "source-resolution-mismatch",
        "source-color-metadata-mismatch",
        "frame-out-of-range",
        "source-missing",
        "source-fingerprint-mismatch",
        "file-io",
        "media-open-failed",
        "media-probe-failed",
        "invalid-cfr-timing",
        "unsupported-codec",
        "unsupported-pixel-format",
        "media-decode-failed",
        "frame-timeline-invalid",
        "frame-budget-exceeded",
        "graphics-unavailable",
        "graphics-device-lost",
        "frame-presentation-timed-out",
    };
    for (const std::string_view key : kMediaErrorKeys) {
        EXPECT_TRUE(
            source.contains(QStringLiteral("case \"") +
                            QString::fromLatin1(key.data(), static_cast<qsizetype>(key.size())) +
                            QStringLiteral("\":")))
            << key;
    }
    for (const QString& removed : {QStringLiteral("clip-out-of-range"),
                                   QStringLiteral("clip-not-found"),
                                   QStringLiteral("export-record-not-found"),
                                   QStringLiteral("duplicate-clip-selection"),
                                   QStringLiteral("invalid-export-mode"),
                                   QStringLiteral("invalid-export-geometry")}) {
        EXPECT_FALSE(source.contains(removed)) << removed.toStdString();
    }
}

TEST(MainQmlContractTests, InstantiatesRootAndSeparatesManualAlignmentStates) {
    auto snapshot = std::make_shared<application::SessionSnapshot>();
    snapshot->graphicsReady = true;
    snapshot->sessionState = domain::SessionState::kReady;
    snapshot->playbackState = domain::PlaybackState::kPaused;
    snapshot->displayedFrame = domain::FrameId{0};
    snapshot->canonicalFrameCount = 10U;
    snapshot->sources = {
        application::SessionSourceView{
            .sourceId = 0U,
            .role = domain::ComparisonRole::kReference,
            .displayName = "A",
        },
        application::SessionSourceView{
            .sourceId = 1U,
            .role = domain::ComparisonRole::kPrediction,
            .displayName = "B",
        },
    };
    snapshot->presentedSources = {
        application::PresentedSourceState{
            .sourceId = 0U,
            .sourceFrameId = domain::FrameId{0},
            .matchKind = application::FrameMatchKind::ExactIndex,
        },
        application::PresentedSourceState{
            .sourceId = 1U,
            .sourceFrameId = domain::FrameId{0},
            .matchKind = application::FrameMatchKind::ExactIndex,
        },
    };
    snapshot->manualAlignmentAnchors = {
        application::SourceAlignmentAnchors{
            .sourceId = 1U,
            .anchors =
                {
                    application::ManualAlignmentAnchor{
                        .canonicalFrameId = domain::FrameId{2U},
                        .sourceFrameId = domain::FrameId{3U},
                    },
                },
        },
    };
    std::vector<application::PlaybackCommand> submitted;
    std::vector<application::CommandTerminal> terminals;
    ReviewController controller{
        ReviewController::Dependencies{
            .submit =
                [&submitted](application::PlaybackCommand command) {
                    submitted.push_back(std::move(command));
                    return application::PortSubmitResult::Accepted;
                },
            .snapshot = [snapshot] { return snapshot; },
            .takeCompletedCommands =
                [&terminals] {
                    std::vector<application::CommandTerminal> result = std::move(terminals);
                    terminals.clear();
                    return result;
                },
        },
    };
    ReviewPreferencesController preferences{std::make_shared<ClosedSettingsRepository>()};
    // A persisted three-source-only pair must not make a two-source session black. The
    // preference remains intact for a later third source, while the render-facing state
    // resolves to the valid A/B edge.
    preferences.setViewMode(ReviewPreferencesController::ViewMode::Wipe);
    preferences.setDifferenceEdge(ReviewPreferencesController::DifferenceEdge::Edge0And2);
    ReviewShellController shell{controller, preferences};

    QQmlEngine engine;
    engine.addImportPath(
        QDir{QCoreApplication::applicationDirPath()}.filePath(QStringLiteral("qml")));
    engine.rootContext()->setContextProperty(QStringLiteral("reviewController"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("reviewPreferences"), &preferences);
    engine.rootContext()->setContextProperty(QStringLiteral("reviewSession"), &shell);
    QQmlComponent component{&engine, QUrl{QStringLiteral("qrc:/qml/Main.qml")}};
    ASSERT_EQ(component.status(), QQmlComponent::Ready) << componentErrors(component);

    std::unique_ptr<QObject> root{component.create()};
    ASSERT_NE(root, nullptr) << componentErrors(component);
    ASSERT_NE(qobject_cast<QQuickWindow*>(root.get()), nullptr);
    QCoreApplication::processEvents();
    ASSERT_EQ(controller.sources()->rowCount(), 2);
    EXPECT_EQ(root->property("effectiveViewMode").toInt(), ComparisonSurface::Wipe);
    EXPECT_EQ(root->property("differenceEdge").toInt(), ComparisonSurface::Edge0And1);
    EXPECT_EQ(static_cast<int>(preferences.differenceEdge()),
              static_cast<int>(ReviewPreferencesController::DifferenceEdge::Edge0And2));
    ASSERT_TRUE(QMetaObject::invokeMethod(root.get(), "setInPoint"));
    EXPECT_EQ(shell.inFrame(), 0);
    EXPECT_EQ(shell.inMediaTime(), controller.mediaTimeForFrame(0));
    shell.clearRange();

    EXPECT_EQ(shell.queuedIntentCount(), 0);
    EXPECT_TRUE(shell.queuedIntents().isEmpty());
    EXPECT_TRUE(shell.activeIntent().isEmpty());

    EXPECT_TRUE(root->property("manualAnchorActive").toBool());
    EXPECT_FALSE(root->property("manualOffsetActive").toBool());
    EXPECT_TRUE(root->property("anyManualAlignmentActive").toBool());

    QObject* const alignmentModeStatus =
        root->findChild<QObject*>(QStringLiteral("alignmentModeStatus"));
    QObject* const offsetStatus =
        root->findChild<QObject*>(QStringLiteral("manualOffsetStatusLabel"));
    QObject* const anchorButton = root->findChild<QObject*>(QStringLiteral("manualAnchorsButton"));
    QObject* const offsetRepeater =
        root->findChild<QObject*>(QStringLiteral("sourceOffsetRepeater"));
    ASSERT_NE(alignmentModeStatus, nullptr);
    ASSERT_NE(offsetStatus, nullptr);
    ASSERT_NE(anchorButton, nullptr);
    ASSERT_NE(offsetRepeater, nullptr);
    EXPECT_EQ(offsetRepeater->property("count").toInt(), 2);
    EXPECT_EQ(alignmentModeStatus->property("text").toString(), QStringLiteral("Manual alignment"));
    EXPECT_EQ(offsetStatus->property("text").toString(), QStringLiteral("Frame alignment offset"));
    EXPECT_EQ(anchorButton->property("text").toString(), QStringLiteral("Manual anchors active…"));

    auto* const window = qobject_cast<QQuickWindow*>(root.get());
    ASSERT_NE(window, nullptr);
    QObject* const inspector =
        root->findChild<QObject*>(QStringLiteral("advancedAlignmentInspector"));
    QObject* const tabbedInspector = root->findChild<QObject*>(QStringLiteral("tabbedInspector"));
    auto* const compareBar = root->findChild<QQuickItem*>(QStringLiteral("compareModeBar"));
    auto* const transport = root->findChild<QQuickItem*>(QStringLiteral("transport"));
    auto* const transportBar = root->findChild<QQuickItem*>(QStringLiteral("transportBar"));
    auto* const viewport = root->findChild<QQuickItem*>(QStringLiteral("mediaViewportFocusTarget"));
    auto* const surface = root->findChild<QQuickItem*>(QStringLiteral("dualVideoSurface"));
    auto* const sideModeButton = root->findChild<QQuickItem*>(QStringLiteral("sideModeButton"));
    QObject* const analysisChrome =
        root->findChild<QObject*>(QStringLiteral("analysisControlsChrome"));
    QObject* const surfaceLabelRepeater =
        root->findChild<QObject*>(QStringLiteral("surfaceLabelRepeater"));
    auto* const frameErrorBanner = root->findChild<QQuickItem*>(QStringLiteral("frameErrorBanner"));
    QObject* const activeSourceRepeater =
        root->findChild<QObject*>(QStringLiteral("activeSourceRepeater"));
    QObject* const timeline = root->findChild<QObject*>(QStringLiteral("timelineSlider"));
    QObject* const setInButton = root->findChild<QObject*>(QStringLiteral("setInButton"));
    QObject* const setOutButton = root->findChild<QObject*>(QStringLiteral("setOutButton"));
    QObject* const clearRangeButton = root->findChild<QObject*>(QStringLiteral("clearRangeButton"));
    QObject* const loopRangeButton = root->findChild<QObject*>(QStringLiteral("loopRangeButton"));
    QObject* const mediaInfoRepeater =
        root->findChild<QObject*>(QStringLiteral("mediaInfoRepeater"));
    QObject* const shortcutHelp = root->findChild<QObject*>(QStringLiteral("shortcutHelpOverlay"));
    QObject* const contextViewMenu = root->findChild<QObject*>(QStringLiteral("contextViewMenu"));
    QObject* const reviewContextMenu =
        root->findChild<QObject*>(QStringLiteral("reviewContextMenu"));
    QObject* const contextOpenAction =
        root->findChild<QObject*>(QStringLiteral("contextOpenAction"));
    QObject* const contextPairMenu = root->findChild<QObject*>(QStringLiteral("contextPairMenu"));
    QObject* const contextReferenceMenu =
        root->findChild<QObject*>(QStringLiteral("contextReferenceMenu"));
    QObject* const contextInfoAction =
        root->findChild<QObject*>(QStringLiteral("contextInfoAction"));
    QObject* const immersiveHud = root->findChild<QObject*>(QStringLiteral("immersiveReviewHud"));
    auto* const firstButton = root->findChild<QQuickItem*>(QStringLiteral("firstButton"));
    auto* const lastButton = root->findChild<QQuickItem*>(QStringLiteral("lastButton"));
    QObject* const analysisGridMenuItem =
        root->findChild<QObject*>(QStringLiteral("analysisGridMenuItem"));
    QObject* const compareMenu = root->findChild<QObject*>(QStringLiteral("compareMenu"));
    QObject* const analyzeMenu = root->findChild<QObject*>(QStringLiteral("analyzeMenu"));
    ASSERT_NE(inspector, nullptr);
    ASSERT_NE(tabbedInspector, nullptr);
    ASSERT_NE(compareBar, nullptr);
    ASSERT_NE(transport, nullptr);
    ASSERT_NE(transportBar, nullptr);
    ASSERT_NE(viewport, nullptr);
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->property("viewMode").toInt(), ComparisonSurface::Wipe);
    EXPECT_EQ(surface->property("differenceEdge").toInt(), ComparisonSurface::Edge0And1);
    ASSERT_NE(sideModeButton, nullptr);
    ASSERT_NE(analysisChrome, nullptr);
    ASSERT_NE(surfaceLabelRepeater, nullptr);
    ASSERT_NE(frameErrorBanner, nullptr);
    ASSERT_NE(activeSourceRepeater, nullptr);
    ASSERT_NE(timeline, nullptr);
    ASSERT_NE(setInButton, nullptr);
    ASSERT_NE(setOutButton, nullptr);
    ASSERT_NE(clearRangeButton, nullptr);
    ASSERT_NE(loopRangeButton, nullptr);
    ASSERT_NE(mediaInfoRepeater, nullptr);
    ASSERT_NE(shortcutHelp, nullptr);
    ASSERT_NE(contextViewMenu, nullptr);
    ASSERT_NE(reviewContextMenu, nullptr);
    ASSERT_NE(contextOpenAction, nullptr);
    ASSERT_NE(contextPairMenu, nullptr);
    ASSERT_NE(contextReferenceMenu, nullptr);
    ASSERT_NE(contextInfoAction, nullptr);
    ASSERT_NE(immersiveHud, nullptr);
    ASSERT_NE(firstButton, nullptr);
    ASSERT_NE(lastButton, nullptr);
    ASSERT_NE(analysisGridMenuItem, nullptr);
    ASSERT_NE(compareMenu, nullptr);
    ASSERT_NE(analyzeMenu, nullptr);
    EXPECT_FALSE(analysisGridMenuItem->property("enabled").toBool());
    EXPECT_FALSE(inspector->property("visible").toBool());
    EXPECT_EQ(root->property("minimumWidth").toDouble(), 960.0);
    EXPECT_TRUE(compareBar->isVisible());
    EXPECT_GT(transport->width(), 0.0);
    EXPECT_GT(firstButton->width(), 0.0);
    EXPECT_GT(lastButton->width(), 0.0);
    EXPECT_FALSE(analysisChrome->property("visible").toBool());
    EXPECT_EQ(surfaceLabelRepeater->property("count").toInt(), 2);
    EXPECT_EQ(activeSourceRepeater->property("count").toInt(), 2);
    EXPECT_EQ(root->property("availableViewModes").toList().size(), 3);
    EXPECT_GE(viewport->height() / window->contentItem()->height(), 0.78);

    snapshot->lastError = domain::makeMediaError(domain::MediaErrorCode::kMediaDecodeFailed,
                                                 domain::MediaOperation::kMediaDecode,
                                                 domain::SourceId{0U},
                                                 true,
                                                 "Synthetic frame read failure.");
    controller.refreshProjection();
    QCoreApplication::processEvents();
    EXPECT_TRUE(frameErrorBanner->isVisible());
    const QPointF bannerTopLeft = frameErrorBanner->mapToItem(viewport, QPointF{0.0, 0.0});
    EXPECT_GE(bannerTopLeft.y(), 0.0);
    EXPECT_LE(bannerTopLeft.y() + frameErrorBanner->height(), viewport->height());
    snapshot->lastError.reset();
    controller.refreshProjection();
    QCoreApplication::processEvents();

    preferences.setViewMode(ReviewPreferencesController::ViewMode::Wipe);
    for (const double wipePosition : {0.05, 0.95}) {
        root->setProperty("wipePosition", wipePosition);
        QCoreApplication::processEvents();
        QVariant firstBadgeResult;
        QVariant secondBadgeResult;
        ASSERT_TRUE(QMetaObject::invokeMethod(viewport,
                                              "surfaceLabelGeometry",
                                              Q_RETURN_ARG(QVariant, firstBadgeResult),
                                              Q_ARG(QVariant, QVariant{0})));
        ASSERT_TRUE(QMetaObject::invokeMethod(viewport,
                                              "surfaceLabelGeometry",
                                              Q_RETURN_ARG(QVariant, secondBadgeResult),
                                              Q_ARG(QVariant, QVariant{1})));
        const QVariantMap firstBadge = firstBadgeResult.toMap();
        const QVariantMap secondBadge = secondBadgeResult.toMap();
        ASSERT_FALSE(firstBadge.isEmpty());
        ASSERT_FALSE(secondBadge.isEmpty());
        const QVariantMap leftWipeBadge =
            firstBadge.value(QStringLiteral("sourceSlot")).toInt() == 0 ? firstBadge : secondBadge;
        const QVariantMap rightWipeBadge =
            firstBadge.value(QStringLiteral("sourceSlot")).toInt() == 1 ? firstBadge : secondBadge;
        EXPECT_TRUE(leftWipeBadge.value(QStringLiteral("visible")).toBool());
        EXPECT_TRUE(rightWipeBadge.value(QStringLiteral("visible")).toBool());
        EXPECT_LE(leftWipeBadge.value(QStringLiteral("x")).toDouble() +
                      leftWipeBadge.value(QStringLiteral("width")).toDouble(),
                  rightWipeBadge.value(QStringLiteral("x")).toDouble());
        EXPECT_GE(leftWipeBadge.value(QStringLiteral("x")).toDouble(), 0.0);
        EXPECT_LE(rightWipeBadge.value(QStringLiteral("x")).toDouble() +
                      rightWipeBadge.value(QStringLiteral("width")).toDouble(),
                  viewport->width());
    }
    preferences.setViewMode(ReviewPreferencesController::ViewMode::SideBySide);
    QVariant zoomResult;
    ASSERT_TRUE(QMetaObject::invokeMethod(timeline,
                                          "setZoom",
                                          Q_RETURN_ARG(QVariant, zoomResult),
                                          Q_ARG(QVariant, QVariant{2.0}),
                                          Q_ARG(QVariant, QVariant{0.5})));
    EXPECT_GT(timeline->property("zoomFactor").toDouble(), 1.0);

    window->resize(960, 640);
    window->show();
    for (int iteration = 0; iteration < 5; ++iteration) {
        QCoreApplication::processEvents();
    }
    const QPointF transportTopLeft =
        transportBar->mapToItem(window->contentItem(), QPointF{0.0, 0.0});
    EXPECT_EQ(window->contentItem()->width(), window->width());
    EXPECT_GE(transportTopLeft.x(), 0.0);
    EXPECT_LE(transportTopLeft.x() + transportBar->width(), window->width())
        << "left=" << transportTopLeft.x() << " barWidth=" << transportBar->width()
        << " contentWidth=" << window->contentItem()->width();

    ASSERT_TRUE(QMetaObject::invokeMethod(compareMenu, "open"));
    for (int iteration = 0; iteration < 5; ++iteration) {
        QCoreApplication::processEvents();
    }
    EXPECT_TRUE(compareMenu->property("opened").toBool());
    EXPECT_TRUE(root->property("anyMenuOpen").toBool());
    EXPECT_FALSE(root->property("globalMediaShortcutsEnabled").toBool());
    const std::size_t submittedBeforeMenuShortcut = submitted.size();
    sendKey(*window, Qt::Key_Right);
    EXPECT_EQ(submitted.size(), submittedBeforeMenuShortcut);
    ASSERT_TRUE(QMetaObject::invokeMethod(compareMenu, "close"));
    for (int iteration = 0; iteration < 5; ++iteration) {
        QCoreApplication::processEvents();
    }
    EXPECT_FALSE(root->property("anyMenuOpen").toBool());
    EXPECT_TRUE(root->property("globalMediaShortcutsEnabled").toBool());

    sideModeButton->forceActiveFocus();
    ASSERT_TRUE(sideModeButton->hasActiveFocus());
    EXPECT_FALSE(root->property("globalMediaShortcutsEnabled").toBool());
    sendKey(*window, Qt::Key_Tab);
    QCoreApplication::processEvents();
    EXPECT_FALSE(root->property("chromeVisible").toBool());
    EXPECT_TRUE(root->property("globalMediaShortcutsEnabled").toBool());
    EXPECT_TRUE(viewport->hasActiveFocus());
    EXPECT_FALSE(transport->isVisible());
    EXPECT_FALSE(analysisChrome->property("visible").toBool());
    EXPECT_DOUBLE_EQ(viewport->property("radius").toDouble(), 0.0);
    QObject* const viewportBorder = viewport->property("border").value<QObject*>();
    ASSERT_NE(viewportBorder, nullptr);
    EXPECT_EQ(viewportBorder->property("width").toInt(), 0);
    const QPointF immersiveTopLeft = viewport->mapToItem(window->contentItem(), QPointF{0.0, 0.0});
    EXPECT_DOUBLE_EQ(immersiveTopLeft.x(), 0.0);
    EXPECT_DOUBLE_EQ(immersiveTopLeft.y(), 0.0);
    EXPECT_DOUBLE_EQ(viewport->width(), window->contentItem()->width());
    EXPECT_DOUBLE_EQ(viewport->height(), window->contentItem()->height());
    EXPECT_DOUBLE_EQ(surface->x(), 0.0);
    EXPECT_DOUBLE_EQ(surface->y(), 0.0);
    EXPECT_DOUBLE_EQ(surface->width(), viewport->width());
    EXPECT_DOUBLE_EQ(surface->height(), viewport->height());

    sendKey(*window, Qt::Key_Right);
    ASSERT_FALSE(submitted.empty());
    const auto* const step = std::get_if<application::StepFramesCommand>(&submitted.back());
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->delta, 1);
    snapshot->displayedFrame = domain::FrameId{1};
    terminals.push_back(application::CommandTerminal{
        .context = application::commandContext(submitted.back()),
        .outcome = application::CommandOutcome::Succeeded,
    });
    snapshot->displayedFrame = domain::FrameId{2};
    controller.refreshProjection();
    QCoreApplication::processEvents();
    EXPECT_TRUE(immersiveHud->property("visible").toBool());

    preferences.setShortcutPreset(1);
    QCoreApplication::processEvents();
    sendKey(*window, Qt::Key_Right);
    ASSERT_FALSE(submitted.empty());
    const auto* const playerStep = std::get_if<application::StepFramesCommand>(&submitted.back());
    ASSERT_NE(playerStep, nullptr);
    EXPECT_EQ(playerStep->delta, 5 * 30);
    terminals.push_back(application::CommandTerminal{
        .context = application::commandContext(submitted.back()),
        .outcome = application::CommandOutcome::Succeeded,
    });
    controller.refreshProjection();
    preferences.setShortcutPreset(0);

    sendKey(*window, Qt::Key_Question);
    EXPECT_TRUE(shortcutHelp->property("visible").toBool());
    shortcutHelp->setProperty("visible", false);
    QElapsedTimer hudWait;
    hudWait.start();
    while (hudWait.elapsed() < 900) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5U);
    }
    EXPECT_FALSE(immersiveHud->property("visible").toBool());
    sendKey(*window, Qt::Key_Space);
    ASSERT_FALSE(submitted.empty());
    EXPECT_NE(std::get_if<application::PlayCommand>(&submitted.back()), nullptr);

    sendKey(*window, Qt::Key_Tab);
    QCoreApplication::processEvents();
    EXPECT_TRUE(root->property("chromeVisible").toBool());
    EXPECT_TRUE(transport->isVisible());

    snapshot->sources.push_back(application::SessionSourceView{
        .sourceId = 2U,
        .role = domain::ComparisonRole::kPrediction,
        .displayName = "C",
    });
    snapshot->presentedSources.push_back(application::PresentedSourceState{
        .sourceId = 2U,
        .sourceFrameId = domain::FrameId{2},
        .matchKind = application::FrameMatchKind::ExactIndex,
    });
    preferences.setViewMode(ReviewPreferencesController::ViewMode::ThreeUp);
    controller.refreshProjection();
    QCoreApplication::processEvents();
    EXPECT_EQ(controller.sourceCount(), 3);
    EXPECT_EQ(shell.effectiveViewMode(), ComparisonSurface::ThreeUp);
    EXPECT_EQ(root->property("effectiveViewMode").toInt(), ComparisonSurface::ThreeUp);
    EXPECT_EQ(surfaceLabelRepeater->property("count").toInt(), 3);
    EXPECT_EQ(activeSourceRepeater->property("count").toInt(), 3);
    EXPECT_EQ(root->property("availableViewModes").toList().size(), 6);
    EXPECT_TRUE(analysisGridMenuItem->property("enabled").toBool());

    // Three-up (3 sources): each source panel gets a divider outline.
    QObject* const panelDividerRepeater =
        root->findChild<QObject*>(QStringLiteral("panelDividerRepeater"));
    ASSERT_NE(panelDividerRepeater, nullptr);
    EXPECT_EQ(panelDividerRepeater->property("count").toInt(), 3);
    // Analysis Grid (3 sources): same divider treatment; labels must stay inside the
    // viewport and must not overlap the analysis chrome.
    preferences.setViewMode(ReviewPreferencesController::ViewMode::AnalysisGrid);
    controller.refreshProjection();
    QCoreApplication::processEvents();
    EXPECT_EQ(root->property("effectiveViewMode").toInt(), 4);
    EXPECT_EQ(panelDividerRepeater->property("count").toInt(), 3);
    EXPECT_EQ(surfaceLabelRepeater->property("count").toInt(), 3);
    EXPECT_TRUE(analysisChrome->property("visible").toBool());
    auto* const chromeItem = qobject_cast<QQuickItem*>(analysisChrome);
    ASSERT_NE(chromeItem, nullptr);
    const QRectF chromeRectInViewport(chromeItem->mapToItem(viewport, QPointF{0, 0}),
                                      chromeItem->size());
    for (int slot = 0; slot < 3; ++slot) {
        QVariant labelResult;
        ASSERT_TRUE(QMetaObject::invokeMethod(viewport,
                                              "surfaceLabelGeometry",
                                              Q_RETURN_ARG(QVariant, labelResult),
                                              Q_ARG(QVariant, QVariant{slot})));
        const QVariantMap label = labelResult.toMap();
        ASSERT_FALSE(label.isEmpty());
        EXPECT_TRUE(label.value(QStringLiteral("visible")).toBool());
        const qreal labelX = label.value(QStringLiteral("x")).toDouble();
        const qreal labelW = label.value(QStringLiteral("width")).toDouble();
        // surfaceLabelGeometry reports x/width/visible; pull the real y/height from the
        // live label delegate so containment and chrome-overlap use actual geometry.
        QQuickItem* labelDelegate = nullptr;
        ASSERT_TRUE(QMetaObject::invokeMethod(surfaceLabelRepeater,
                                              "itemAt",
                                              Q_RETURN_ARG(QQuickItem*, labelDelegate),
                                              Q_ARG(int, slot)));
        ASSERT_NE(labelDelegate, nullptr);
        const qreal labelY = labelDelegate->y();
        const qreal labelH = labelDelegate->height();
        EXPECT_GE(labelX, 0.0);
        EXPECT_GE(labelY, 0.0);
        EXPECT_LE(labelX + labelW, viewport->width());
        EXPECT_LE(labelY + labelH, viewport->height());
        EXPECT_TRUE(QRectF(labelX, labelY, labelW, labelH).intersects(chromeRectInViewport) ==
                    false)
            << "source label slot " << slot << " overlaps analysis chrome";
    }
    preferences.setViewMode(ReviewPreferencesController::ViewMode::ThreeUp);
    controller.refreshProjection();
    QCoreApplication::processEvents();

    // In multi-source the TabbedInspector exposes all four tabs.
    shell.setInspectorVisible(true);
    QCoreApplication::processEvents();
    QObject* const inspectorTabBar =
        tabbedInspector->findChild<QObject*>(QStringLiteral("inspectorTabBar"));
    ASSERT_NE(inspectorTabBar, nullptr);
    EXPECT_EQ(inspectorTabBar->property("count").toInt(), 4);
    for (const char* const tabName :
         {"compareTabButton", "alignmentTabButton", "reviewTabButton", "infoTabButton"}) {
        QObject* const tab = tabbedInspector->findChild<QObject*>(QString::fromLatin1(tabName));
        ASSERT_NE(tab, nullptr);
        EXPECT_TRUE(tab->property("visible").toBool());
    }
    // Restore the pre-existing hidden-inspector state so the following two-source and
    // one-source sections observe the same chrome layout as before this check.
    shell.setInspectorVisible(false);
    QCoreApplication::processEvents();

    snapshot->sources.resize(2U);
    snapshot->presentedSources.resize(2U);
    controller.refreshProjection();
    preferences.setViewMode(ReviewPreferencesController::ViewMode::AnalysisGrid);
    QCoreApplication::processEvents();
    // Two-source effective state falls back safely without overwriting the persisted three-up
    // preference, which becomes valid again if a third source is later restored.
    EXPECT_EQ(preferences.viewMode(), ReviewPreferencesController::ViewMode::AnalysisGrid);
    EXPECT_EQ(root->property("effectiveViewMode").toInt(), ComparisonSurface::SideBySide);
    EXPECT_EQ(root->property("availableViewModes").toList().size(), 3);
    EXPECT_EQ(activeSourceRepeater->property("count").toInt(), 2);
    EXPECT_FALSE(analysisGridMenuItem->property("enabled").toBool());

    snapshot->sources.resize(1U);
    snapshot->presentedSources.resize(1U);
    controller.refreshProjection();
    QCoreApplication::processEvents();
    EXPECT_EQ(surfaceLabelRepeater->property("count").toInt(), 1);
    EXPECT_EQ(activeSourceRepeater->property("count").toInt(), 1);
    EXPECT_EQ(root->property("availableViewModes").toList().size(), 1);
    EXPECT_FALSE(compareBar->isVisible());

    shell.setInspectorVisible(true);
    QCoreApplication::processEvents();
    EXPECT_TRUE(tabbedInspector->property("visible").toBool());
    EXPECT_EQ(tabbedInspector->property("effectiveTab").toInt(), 2);
    EXPECT_TRUE(setInButton->property("visible").toBool());
    EXPECT_TRUE(setOutButton->property("visible").toBool());

    // In single mode the inspector keeps only the Review/Info tabs; Compare/Alignment
    // tabs hide and the Review-tab range actions remain reachable.
    QObject* const compareTab =
        tabbedInspector->findChild<QObject*>(QStringLiteral("compareTabButton"));
    QObject* const alignmentTab =
        tabbedInspector->findChild<QObject*>(QStringLiteral("alignmentTabButton"));
    QObject* const reviewTab =
        tabbedInspector->findChild<QObject*>(QStringLiteral("reviewTabButton"));
    QObject* const infoTab = tabbedInspector->findChild<QObject*>(QStringLiteral("infoTabButton"));
    ASSERT_NE(compareTab, nullptr);
    ASSERT_NE(alignmentTab, nullptr);
    ASSERT_NE(reviewTab, nullptr);
    ASSERT_NE(infoTab, nullptr);
    EXPECT_FALSE(compareTab->property("visible").toBool());
    EXPECT_FALSE(alignmentTab->property("visible").toBool());
    EXPECT_TRUE(reviewTab->property("visible").toBool());
    EXPECT_TRUE(infoTab->property("visible").toBool());
    EXPECT_TRUE(loopRangeButton->property("visible").toBool());
    EXPECT_TRUE(clearRangeButton->property("visible").toBool());

    snapshot->sessionState = domain::SessionState::kEmpty;
    snapshot->displayedFrame.reset();
    snapshot->canonicalFrameCount = 0U;
    snapshot->sources.clear();
    snapshot->presentedSources.clear();
    snapshot->validatedComparison.reset();
    controller.refreshProjection();
    QCoreApplication::processEvents();
    EXPECT_EQ(root->property("oscState").toInt(), 2);
    EXPECT_FALSE(transport->isVisible());
    EXPECT_FALSE(compareMenu->property("enabled").toBool());
    EXPECT_FALSE(analyzeMenu->property("enabled").toBool());
    EXPECT_TRUE(reviewContextMenu->property("emptyStateOnly").toBool());
    EXPECT_EQ(reviewContextMenu->property("availableActionCount").toInt(), 2);
}

} // namespace
} // namespace dvs::ui

int main(int argumentCount, char* arguments[]) {
    dvs::ui::configureGraphicsBackend();
    QGuiApplication application{argumentCount, arguments};
    initializeMainQmlContractResources();
    static_cast<void>(
        qmlRegisterType<dvs::ui::ComparisonSurface>("Dvs.Ui", 1, 0, "ComparisonSurface"));
    testing::InitGoogleTest(&argumentCount, arguments);
    return RUN_ALL_TESTS();
}
