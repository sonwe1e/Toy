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

    QQmlEngine engine;
    engine.addImportPath(
        QDir{QCoreApplication::applicationDirPath()}.filePath(QStringLiteral("qml")));
    engine.rootContext()->setContextProperty(QStringLiteral("reviewController"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("reviewPreferences"), &preferences);
    QQmlComponent component{&engine, QUrl{QStringLiteral("qrc:/qml/Main.qml")}};
    ASSERT_EQ(component.status(), QQmlComponent::Ready) << componentErrors(component);

    std::unique_ptr<QObject> root{component.create()};
    ASSERT_NE(root, nullptr) << componentErrors(component);
    ASSERT_NE(qobject_cast<QQuickWindow*>(root.get()), nullptr);
    EXPECT_TRUE(root->property("manualAnchorActive").toBool());
    EXPECT_FALSE(root->property("manualOffsetActive").toBool());
    EXPECT_TRUE(root->property("anyManualAlignmentActive").toBool());

    QObject* const alignmentModeStatus =
        root->findChild<QObject*>(QStringLiteral("alignmentModeStatus"));
    QObject* const offsetStatus =
        root->findChild<QObject*>(QStringLiteral("manualOffsetStatusLabel"));
    QObject* const anchorButton = root->findChild<QObject*>(QStringLiteral("manualAnchorsButton"));
    QObject* const sourceBOffset = root->findChild<QObject*>(QStringLiteral("sourceBOffset"));
    ASSERT_NE(alignmentModeStatus, nullptr);
    ASSERT_NE(offsetStatus, nullptr);
    ASSERT_NE(anchorButton, nullptr);
    ASSERT_NE(sourceBOffset, nullptr);
    EXPECT_EQ(alignmentModeStatus->property("text").toString(), QStringLiteral("Manual alignment"));
    EXPECT_EQ(offsetStatus->property("text").toString(), QStringLiteral("Frame offsets"));
    EXPECT_EQ(anchorButton->property("text").toString(), QStringLiteral("Manual anchors active"));

    ASSERT_TRUE(sourceBOffset->setProperty("value", 2));
    QCoreApplication::processEvents();
    EXPECT_TRUE(root->property("manualOffsetActive").toBool());
    EXPECT_TRUE(root->property("anyManualAlignmentActive").toBool());
    EXPECT_EQ(offsetStatus->property("text").toString(), QStringLiteral("Manual offset active"));

    ASSERT_TRUE(sourceBOffset->setProperty("value", 0));
    QCoreApplication::processEvents();
    EXPECT_FALSE(root->property("manualOffsetActive").toBool());
    EXPECT_TRUE(root->property("anyManualAlignmentActive").toBool());
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
