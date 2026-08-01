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
#include <QGuiApplication>
#include <QKeyEvent>
#include <QObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWindow>
#include <QResource>
#include <QStringList>
#include <QThread>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqml.h>

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
    ReviewShellController shell{controller};
    QObject workspace;
    workspace.setProperty("busy", false);
    workspace.setProperty("dirty", false);
    workspace.setProperty("hasProject", false);
    workspace.setProperty("canSave", false);
    workspace.setProperty("relinkRequired", false);
    workspace.setProperty("nextRelinkSourceId", -1);
    workspace.setProperty("errorTechnicalDetail", QString{});

    QQmlEngine engine;
    engine.addImportPath(
        QDir{QCoreApplication::applicationDirPath()}.filePath(QStringLiteral("qml")));
    engine.rootContext()->setContextProperty(QStringLiteral("reviewController"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("reviewPreferences"), &preferences);
    engine.rootContext()->setContextProperty(QStringLiteral("workspaceController"), &workspace);
    engine.rootContext()->setContextProperty(QStringLiteral("reviewShell"), &shell);
    QQmlComponent component{&engine, QUrl{QStringLiteral("qrc:/qml/Main.qml")}};
    ASSERT_EQ(component.status(), QQmlComponent::Ready) << componentErrors(component);

    std::unique_ptr<QObject> root{component.create()};
    ASSERT_NE(root, nullptr) << componentErrors(component);
    ASSERT_NE(qobject_cast<QQuickWindow*>(root.get()), nullptr);
    QCoreApplication::processEvents();
    ASSERT_EQ(controller.sources()->rowCount(), 2);

    const QVariant requestUrls = QVariantList{QUrl::fromLocalFile(QStringLiteral("C:/a.mp4"))};
    ASSERT_TRUE(shell.enqueueStartupRequest(2, requestUrls.toList()));
    EXPECT_FALSE(shell.takeNextStartupRequest().isEmpty());
    for (int requestIndex = 0; requestIndex < 8; ++requestIndex) {
        QVariant accepted;
        ASSERT_TRUE(QMetaObject::invokeMethod(root.get(),
                                              "enqueueStartupRequest",
                                              Q_RETURN_ARG(QVariant, accepted),
                                              Q_ARG(QVariant, QVariant{2}),
                                              Q_ARG(QVariant, requestUrls)));
        EXPECT_TRUE(accepted.toBool());
    }
    QVariant overflowAccepted;
    ASSERT_TRUE(QMetaObject::invokeMethod(root.get(),
                                          "enqueueStartupRequest",
                                          Q_RETURN_ARG(QVariant, overflowAccepted),
                                          Q_ARG(QVariant, QVariant{2}),
                                          Q_ARG(QVariant, requestUrls)));
    EXPECT_FALSE(overflowAccepted.toBool());
    EXPECT_EQ(shell.queuedStartupRequestCount(), 8);
    shell.completeStartupRequest();

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
    QObject* const activeSourceRepeater =
        root->findChild<QObject*>(QStringLiteral("activeSourceRepeater"));
    QObject* const timeline = root->findChild<QObject*>(QStringLiteral("timelineSlider"));
    QObject* const shortcutHelp = root->findChild<QObject*>(QStringLiteral("shortcutHelpOverlay"));
    QObject* const contextViewMenu = root->findChild<QObject*>(QStringLiteral("contextViewMenu"));
    QObject* const contextPairMenu = root->findChild<QObject*>(QStringLiteral("contextPairMenu"));
    QObject* const contextReferenceMenu =
        root->findChild<QObject*>(QStringLiteral("contextReferenceMenu"));
    QObject* const contextBadCaseAction =
        root->findChild<QObject*>(QStringLiteral("contextBadCaseAction"));
    QObject* const contextInfoAction =
        root->findChild<QObject*>(QStringLiteral("contextInfoAction"));
    QObject* const immersiveHud = root->findChild<QObject*>(QStringLiteral("immersiveReviewHud"));
    auto* const firstButton = root->findChild<QQuickItem*>(QStringLiteral("firstButton"));
    auto* const lastButton = root->findChild<QQuickItem*>(QStringLiteral("lastButton"));
    QObject* const badCaseDialog = root->findChild<QObject*>(QStringLiteral("badCaseFolderDialog"));
    QObject* const analysisGridMenuItem =
        root->findChild<QObject*>(QStringLiteral("analysisGridMenuItem"));
    ASSERT_NE(inspector, nullptr);
    ASSERT_NE(compareBar, nullptr);
    ASSERT_NE(transport, nullptr);
    ASSERT_NE(transportBar, nullptr);
    ASSERT_NE(viewport, nullptr);
    ASSERT_NE(surface, nullptr);
    ASSERT_NE(sideModeButton, nullptr);
    ASSERT_NE(analysisChrome, nullptr);
    ASSERT_NE(surfaceLabelRepeater, nullptr);
    ASSERT_NE(activeSourceRepeater, nullptr);
    ASSERT_NE(timeline, nullptr);
    ASSERT_NE(shortcutHelp, nullptr);
    ASSERT_NE(contextViewMenu, nullptr);
    ASSERT_NE(contextPairMenu, nullptr);
    ASSERT_NE(contextReferenceMenu, nullptr);
    ASSERT_NE(contextBadCaseAction, nullptr);
    ASSERT_NE(contextInfoAction, nullptr);
    ASSERT_NE(immersiveHud, nullptr);
    ASSERT_NE(firstButton, nullptr);
    ASSERT_NE(lastButton, nullptr);
    ASSERT_NE(badCaseDialog, nullptr);
    ASSERT_NE(analysisGridMenuItem, nullptr);
    EXPECT_FALSE(analysisGridMenuItem->property("enabled").toBool());
    EXPECT_FALSE(inspector->property("visible").toBool());
    EXPECT_EQ(root->property("minimumWidth").toDouble(), 960.0);
    EXPECT_TRUE(compareBar->isVisible());
    EXPECT_GT(transport->width(), 0.0);
    EXPECT_GT(firstButton->width(), 0.0);
    EXPECT_GT(lastButton->width(), 0.0);
    EXPECT_TRUE(analysisChrome->property("visible").toBool());
    EXPECT_EQ(surfaceLabelRepeater->property("count").toInt(), 2);
    EXPECT_EQ(activeSourceRepeater->property("count").toInt(), 2);
    EXPECT_EQ(root->property("availableViewModes").toList().size(), 3);
    EXPECT_GE(viewport->height() / window->contentItem()->height(), 0.78);
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

    root->setProperty("shortcutPreset", 1);
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
    root->setProperty("shortcutPreset", 0);

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
    EXPECT_EQ(surfaceLabelRepeater->property("count").toInt(), 3);
    EXPECT_EQ(activeSourceRepeater->property("count").toInt(), 3);
    EXPECT_EQ(root->property("availableViewModes").toList().size(), 6);
    EXPECT_TRUE(analysisGridMenuItem->property("enabled").toBool());

    snapshot->sources.resize(2U);
    snapshot->presentedSources.resize(2U);
    controller.refreshProjection();
    preferences.setViewMode(ReviewPreferencesController::ViewMode::AnalysisGrid);
    QCoreApplication::processEvents();
    EXPECT_EQ(preferences.viewMode(), ReviewPreferencesController::ViewMode::SideBySide);
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
