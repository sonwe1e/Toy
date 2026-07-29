#include "dvs/application/Alignment.h"
#include "dvs/application/SessionSnapshot.h"
#include "dvs/ui/ComparisonSurface.h"
#include "dvs/ui/GraphicsBackend.h"
#include "dvs/ui/ReviewController.h"
#include "dvs/ui/ReviewPreferencesController.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickWindow>
#include <QResource>
#include <QStringList>
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
    ReviewController controller{
        ReviewController::Dependencies{
            .submit =
                [](application::PlaybackCommand) { return application::PortSubmitResult::Closed; },
            .snapshot = [snapshot] { return snapshot; },
            .takeCompletedCommands = [] { return std::vector<application::CommandTerminal>{}; },
        },
    };
    ReviewPreferencesController preferences{std::make_shared<ClosedSettingsRepository>()};
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
    QQmlComponent component{&engine, QUrl{QStringLiteral("qrc:/qml/Main.qml")}};
    ASSERT_EQ(component.status(), QQmlComponent::Ready) << componentErrors(component);

    std::unique_ptr<QObject> root{component.create()};
    ASSERT_NE(root, nullptr) << componentErrors(component);
    ASSERT_NE(qobject_cast<QQuickWindow*>(root.get()), nullptr);
    QCoreApplication::processEvents();
    ASSERT_EQ(controller.sources()->rowCount(), 2);
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
    EXPECT_EQ(offsetStatus->property("text").toString(), QStringLiteral("Frame offsets"));
    EXPECT_EQ(anchorButton->property("text").toString(), QStringLiteral("Manual anchors active"));
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
