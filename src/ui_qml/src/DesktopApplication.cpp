#include "dvs/ui/DesktopApplication.h"

#include "dvs/ui/ComparisonSurface.h"
#include "dvs/ui/ReviewController.h"
#include "dvs/ui/ReviewPreferencesController.h"
#include "dvs/ui/ReviewSessionFacade.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QObject>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickGraphicsConfiguration>
#include <QQuickItem>
#include <QQuickWindow>
#include <QResource>
#include <QScreen>
#include <QString>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqml.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

void initializeQmlResources() {
    Q_INIT_RESOURCE(dvs_ui_qml_resources);
}

namespace dvs::ui {
namespace {

void registerQmlTypes() {
    static std::once_flag registered;
    std::call_once(registered, [] {
        static_cast<void>(qmlRegisterType<ComparisonSurface>("Dvs.Ui", 1, 0, "ComparisonSurface"));
    });
}

[[nodiscard]] bool disableNativeMenuWindows() {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuWindows);
    return true;
}

} // namespace

class DesktopApplication::Impl final {
public:
    Impl(int& argc, char** argv, DesktopApplicationOptions options)
        : options_(options), nativeMenuWindowsDisabled_(disableNativeMenuWindows()),
          application_(argc, argv) {
        initializeQmlResources();
        registerQmlTypes();
        application_.setApplicationDisplayName(QStringLiteral("VCStation - VideoCompareStation"));
        application_.setApplicationName(QStringLiteral("VCStation"));
        application_.setOrganizationName(QStringLiteral("VCStation"));
        application_.setWindowIcon(QIcon{QStringLiteral(":/branding/vcstation-icon.png")});
    }

    ~Impl() {
        releaseSceneGraph();
    }

    [[nodiscard]] bool load(ReviewController& controller,
                            ReviewPreferencesController& preferences,
                            SurfaceBinder bindSurface) {
        if (engine_ || !bindSurface) {
            return false;
        }

        qmlWarnings_.clear();
        auto engine = std::make_unique<QQmlApplicationEngine>();
        engine->addImportPath(
            QDir{QCoreApplication::applicationDirPath()}.filePath(QStringLiteral("qml")));
        engine->rootContext()->setContextProperty(QStringLiteral("reviewController"), &controller);
        engine->rootContext()->setContextProperty(QStringLiteral("reviewPreferences"),
                                                  &preferences);
        shellController_ = std::make_unique<ReviewSessionFacade>(controller, preferences);
        engine->rootContext()->setContextProperty(QStringLiteral("reviewSession"),
                                                  shellController_.get());
        const QMetaObject::Connection warningConnection = QObject::connect(
            engine.get(),
            &QQmlEngine::warnings,
            engine.get(),
            [this](const QList<QQmlError>& warnings) {
                qmlWarnings_.insert(qmlWarnings_.end(), warnings.cbegin(), warnings.cend());
            });
        engine->load(QUrl{QStringLiteral("qrc:/qml/Main.qml")});
        static_cast<void>(QObject::disconnect(warningConnection));
        if (engine->rootObjects().size() != 1) {
            reportWarnings(qmlWarnings_);
            return false;
        }

        auto* const window = qobject_cast<QQuickWindow*>(engine->rootObjects().front());
        auto* const surface =
            window != nullptr
                ? window->findChild<ComparisonSurface*>(QStringLiteral("dualVideoSurface"))
                : nullptr;
        if (window == nullptr || surface == nullptr || !bindSurface(*surface)) {
            reportWarnings(qmlWarnings_);
            return false;
        }

        QQuickGraphicsConfiguration configuration;
        configuration.setPreferSoftwareDevice(options_.preferSoftwareDevice);
        configuration.setDepthBufferFor2D(true);
        window->setGraphicsConfiguration(configuration);
        if (options_.preferHighRefreshScreen) {
            const QList<QScreen*> screens = QGuiApplication::screens();
            const auto selected = std::max_element(
                screens.cbegin(), screens.cend(), [](const auto* lhs, const auto* rhs) {
                    return lhs->refreshRate() < rhs->refreshRate();
                });
            if (selected != screens.cend() && *selected != nullptr) {
                const QRect available = (*selected)->availableGeometry();
                window->setScreen(*selected);
                window->setPosition(available.center() -
                                    QPoint{window->width() / 2, window->height() / 2});
                activeScreenRefreshRate_ = (*selected)->refreshRate();
            }
        }
        if (options_.smokeMode) {
            window->setFlags(Qt::Tool | Qt::FramelessWindowHint);
            window->setOpacity(0.0);
            window->resize(960, 640);
            if (QScreen* const screen = QGuiApplication::primaryScreen()) {
                const QRect desktop = screen->virtualGeometry();
                window->setPosition(desktop.right() + 32, desktop.bottom() + 32);
            }
        }

        engine_ = std::move(engine);
        window_ = window;
        surface_ = surface;
        windowDestroyedConnection_ =
            QObject::connect(window, &QObject::destroyed, [this] { window_ = nullptr; });
        surfaceDestroyedConnection_ =
            QObject::connect(surface, &QObject::destroyed, [this] { surface_ = nullptr; });
        window_->show();
        window_->raise();
        window_->requestActivate();
        window_->requestUpdate();
        surface_->update();
        if (window_->screen() != nullptr) {
            activeScreenRefreshRate_ = window_->screen()->refreshRate();
        }
        return true;
    }

    [[nodiscard]] int exec() {
        if (!engine_ || window_ == nullptr) {
            return EXIT_FAILURE;
        }
        return application_.exec();
    }

    void exit(const int exitCode) noexcept {
        application_.exit(exitCode);
    }

    [[nodiscard]] double activeScreenRefreshRate() const noexcept {
        return activeScreenRefreshRate_;
    }

    [[nodiscard]] bool reviewLocalFiles(const QList<QUrl>& files) {
        if (window_ == nullptr || files.isEmpty()) {
            return false;
        }
        QVariantList values;
        values.reserve(files.size());
        for (const QUrl& file : files) {
            values.push_back(file);
        }
        return QMetaObject::invokeMethod(
            window_, "reviewDroppedUrls", Q_ARG(QVariant, QVariant::fromValue(values)));
    }

    [[nodiscard]] bool enqueueStartupRequest(const int kind, const QList<QUrl>& files) {
        if (window_ == nullptr || shellController_ == nullptr || kind < 0) {
            return false;
        }
        QVariantList values;
        values.reserve(files.size());
        for (const QUrl& file : files) {
            values.push_back(file);
        }
        return shellController_->enqueueStartupRequest(kind, values);
    }

    void activateWindow() noexcept {
        if (window_ == nullptr) {
            return;
        }
        window_->show();
        window_->raise();
        window_->requestActivate();
    }

    [[nodiscard]] bool openSourcesForAutomation(const QList<QUrl>& sources) noexcept {
        if (shellController_ == nullptr || sources.isEmpty() || sources.size() > 3) {
            return false;
        }
        QVariantList values;
        values.reserve(sources.size());
        for (const QUrl& source : sources) {
            values.push_back(source);
        }
        if (!shellController_->stageSources(values, 0)) {
            return false;
        }
        // Automation reports whether the UI accepted the action. Media validation failures are
        // intentionally projected through ReviewController and asserted by the smoke state machine.
        static_cast<void>(shellController_->openStagedSources(false));
        return true;
    }

    [[nodiscard]] bool clickControlForAutomation(const std::string_view objectName) noexcept {
        if (window_ == nullptr || objectName.empty()) {
            return false;
        }
        QObject* const control = window_->findChild<QObject*>(
            QString::fromUtf8(objectName.data(), static_cast<qsizetype>(objectName.size())));
        if (control != nullptr && !control->property("enabled").toBool()) {
            QObject* const transport = window_->findChild<QObject*>(QStringLiteral("transport"));
            if (transport != nullptr && transport->property("controllerState").toInt() == 1) {
                // Automation mirrors the user's bottom-edge wake gesture before clicking an
                // auto-hidden OSC control. Normal pointer input remains blocked while hidden.
                static_cast<void>(transport->setProperty("revealActive", true));
            }
        }
        return control != nullptr && control->property("enabled").toBool() &&
               QMetaObject::invokeMethod(control, "click", Qt::DirectConnection);
    }

    [[nodiscard]] bool focusControlForAutomation(const std::string_view objectName) noexcept {
        if (window_ == nullptr || objectName.empty()) {
            return false;
        }
        auto* const item = window_->findChild<QQuickItem*>(
            QString::fromUtf8(objectName.data(), static_cast<qsizetype>(objectName.size())));
        if (item == nullptr || !item->isEnabled() || !item->isVisible()) {
            return false;
        }
        item->forceActiveFocus(Qt::OtherFocusReason);
        return item->hasActiveFocus();
    }

    [[nodiscard]] bool clickTimelineForAutomation(const double normalizedPosition) noexcept {
        if (window_ == nullptr || !std::isfinite(normalizedPosition) || normalizedPosition < 0.0 ||
            normalizedPosition > 1.0) {
            return false;
        }
        auto* const timeline = window_->findChild<QQuickItem*>(QStringLiteral("timelineSlider"));
        if (timeline == nullptr || !timeline->isEnabled() || timeline->width() <= 0.0 ||
            timeline->height() <= 0.0) {
            return false;
        }
        const qreal horizontalInset = (std::min)(0.5, timeline->width() * 0.25);
        const qreal localX = (std::clamp)(timeline->width() * normalizedPosition,
                                          horizontalInset,
                                          timeline->width() - horizontalInset);
        const QPointF scenePosition =
            timeline->mapToScene(QPointF{localX, timeline->height() * 0.5});
        const QPointF globalPosition = window_->mapToGlobal(scenePosition.toPoint());
        QMouseEvent press{QEvent::MouseButtonPress,
                          scenePosition,
                          globalPosition,
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier};
        QMouseEvent release{QEvent::MouseButtonRelease,
                            scenePosition,
                            globalPosition,
                            Qt::LeftButton,
                            Qt::NoButton,
                            Qt::NoModifier};
        const bool pressed = QCoreApplication::sendEvent(window_, &press);
        const bool released = QCoreApplication::sendEvent(window_, &release);
        return pressed && released;
    }

    [[nodiscard]] bool sendKeyForAutomation(const int key, const int modifiers) noexcept {
        if (window_ == nullptr || key == 0) {
            return false;
        }
        const Qt::KeyboardModifiers keyboardModifiers{modifiers};
        QKeyEvent press{QEvent::KeyPress, key, keyboardModifiers};
        QKeyEvent release{QEvent::KeyRelease, key, keyboardModifiers};
        static_cast<void>(QCoreApplication::sendEvent(window_, &press));
        static_cast<void>(QCoreApplication::sendEvent(window_, &release));
        return true;
    }

    [[nodiscard]] std::optional<std::string>
    objectStringPropertyForAutomation(const std::string_view objectName,
                                      const std::string_view propertyName) const {
        if (window_ == nullptr || objectName.empty() || propertyName.empty()) {
            return std::nullopt;
        }
        const QObject* const object = window_->findChild<QObject*>(
            QString::fromUtf8(objectName.data(), static_cast<qsizetype>(objectName.size())));
        if (object == nullptr) {
            return std::nullopt;
        }
        const std::string property{propertyName};
        const QVariant value = object->property(property.c_str());
        return value.isValid() ? std::optional<std::string>{value.toString().toStdString()}
                               : std::nullopt;
    }

    [[nodiscard]] std::optional<int>
    objectIntPropertyForAutomation(const std::string_view objectName,
                                   const std::string_view propertyName) const {
        if (window_ == nullptr || objectName.empty() || propertyName.empty()) {
            return std::nullopt;
        }
        const QObject* const object = window_->findChild<QObject*>(
            QString::fromUtf8(objectName.data(), static_cast<qsizetype>(objectName.size())));
        if (object == nullptr) {
            return std::nullopt;
        }
        const std::string property{propertyName};
        bool converted = false;
        const int value = object->property(property.c_str()).toInt(&converted);
        return converted ? std::optional<int>{value} : std::nullopt;
    }

    [[nodiscard]] std::optional<QImage>
    captureControlForAutomation(const std::string_view objectName) const {
        if (window_ == nullptr || objectName.empty() || window_->width() <= 0 ||
            window_->height() <= 0) {
            return std::nullopt;
        }
        auto* const item = window_->findChild<QQuickItem*>(
            QString::fromUtf8(objectName.data(), static_cast<qsizetype>(objectName.size())));
        if (item == nullptr || !item->isVisible() || item->width() <= 0.0 ||
            item->height() <= 0.0) {
            return std::nullopt;
        }
        const QImage windowImage = window_->grabWindow();
        if (windowImage.isNull()) {
            return std::nullopt;
        }
        const QPointF topLeft = item->mapToScene(QPointF{});
        const qreal scaleX = static_cast<qreal>(windowImage.width()) / window_->width();
        const qreal scaleY = static_cast<qreal>(windowImage.height()) / window_->height();
        const QRect pixelBounds{
            static_cast<int>(std::floor(topLeft.x() * scaleX)),
            static_cast<int>(std::floor(topLeft.y() * scaleY)),
            static_cast<int>(std::ceil(item->width() * scaleX)),
            static_cast<int>(std::ceil(item->height() * scaleY)),
        };
        const QRect bounded = pixelBounds.intersected(windowImage.rect());
        if (bounded.isEmpty()) {
            return std::nullopt;
        }
        return windowImage.copy(bounded);
    }

    [[nodiscard]] bool openMenuForAutomation(const std::string_view objectName) noexcept {
        if (window_ == nullptr || objectName.empty()) {
            return false;
        }
        QObject* const menu = window_->findChild<QObject*>(
            QString::fromUtf8(objectName.data(), static_cast<qsizetype>(objectName.size())));
        if (menu == nullptr) {
            return false;
        }
        return QMetaObject::invokeMethod(menu, "open");
    }

    [[nodiscard]] bool closeMenuForAutomation(const std::string_view objectName) noexcept {
        if (window_ == nullptr || objectName.empty()) {
            return false;
        }
        QObject* const menu = window_->findChild<QObject*>(
            QString::fromUtf8(objectName.data(), static_cast<qsizetype>(objectName.size())));
        if (menu == nullptr) {
            return false;
        }
        return QMetaObject::invokeMethod(menu, "close");
    }

    [[nodiscard]] bool menuIsOpenForAutomation(const std::string_view objectName) const noexcept {
        if (window_ == nullptr || objectName.empty()) {
            return false;
        }
        const QObject* const menu = window_->findChild<QObject*>(
            QString::fromUtf8(objectName.data(), static_cast<qsizetype>(objectName.size())));
        if (menu == nullptr) {
            return false;
        }
        return menu->property("opened").toBool();
    }

    [[nodiscard]] std::optional<QImage>
    capturePopupWindowForAutomation(const std::string_view objectName) const {
        if (window_ == nullptr || objectName.empty()) {
            return std::nullopt;
        }
        const QString target =
            QString::fromUtf8(objectName.data(), static_cast<qsizetype>(objectName.size()));

        // Primary strategy: resolve the popup window through the menu's own contentItem.
        // QML Popup.Window menus host their visual tree in a contentItem that lives inside the
        // separate top-level QQuickWindow created for the popup.
        QObject* const menuObject = window_->findChild<QObject*>(target);
        if (menuObject != nullptr) {
            const QVariant contentItemVariant = menuObject->property("contentItem");
            if (contentItemVariant.isValid()) {
                auto* const contentItem = contentItemVariant.value<QQuickItem*>();
                if (contentItem != nullptr) {
                    QQuickWindow* const popupWindow = contentItem->window();
                    if (popupWindow != nullptr && popupWindow != window_ &&
                        popupWindow->width() > 0 && popupWindow->height() > 0) {
                        const QImage grabbed = popupWindow->grabWindow();
                        if (!grabbed.isNull()) {
                            return grabbed;
                        }
                    }
                }
            }
        }

        // Fallback: scan top-level windows for a child matching the target name.
        const QList<QWindow*> topLevelWindows = QGuiApplication::topLevelWindows();
        for (QWindow* topLevelWindow : topLevelWindows) {
            if (topLevelWindow == window_) {
                continue;
            }
            auto* quickWindow = qobject_cast<QQuickWindow*>(topLevelWindow);
            if (quickWindow == nullptr) {
                continue;
            }
            QObject* directChild = quickWindow->findChild<QObject*>(target);
            if (directChild == nullptr && quickWindow->contentItem() != nullptr) {
                directChild = quickWindow->contentItem()->findChild<QObject*>(target);
            }
            if (directChild == nullptr) {
                continue;
            }
            if (quickWindow->width() <= 0 || quickWindow->height() <= 0) {
                return std::nullopt;
            }
            const QImage grabbed = quickWindow->grabWindow();
            if (grabbed.isNull()) {
                return std::nullopt;
            }
            return grabbed;
        }
        return std::nullopt;
    }

    void releaseSceneGraph() noexcept {
        if (!engine_) {
            return;
        }
        if (window_ != nullptr) {
            window_->hide();
            window_->releaseResources();
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        static_cast<void>(QObject::disconnect(surfaceDestroyedConnection_));
        static_cast<void>(QObject::disconnect(windowDestroyedConnection_));
        surfaceDestroyedConnection_ = {};
        windowDestroyedConnection_ = {};
        surface_ = nullptr;
        window_ = nullptr;
        engine_.reset();
        shellController_.reset();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

private:
    static void reportWarnings(const std::vector<QQmlError>& warnings) {
        for (const QQmlError& error : warnings) {
            std::cerr << error.toString().toStdString() << '\n';
        }
    }

    DesktopApplicationOptions options_;
    bool nativeMenuWindowsDisabled_ = false;
    QGuiApplication application_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    std::unique_ptr<ReviewSessionFacade> shellController_;
    QQuickWindow* window_ = nullptr;
    ComparisonSurface* surface_ = nullptr;
    double activeScreenRefreshRate_ = 0.0;
    QMetaObject::Connection windowDestroyedConnection_;
    QMetaObject::Connection surfaceDestroyedConnection_;
    std::vector<QQmlError> qmlWarnings_;
};

DesktopApplication::DesktopApplication(int& argc, char** argv, DesktopApplicationOptions options)
    : impl_(std::make_unique<Impl>(argc, argv, options)) {}

DesktopApplication::~DesktopApplication() = default;

bool DesktopApplication::load(ReviewController& controller,
                              ReviewPreferencesController& preferences,
                              SurfaceBinder bindSurface) {
    return impl_->load(controller, preferences, std::move(bindSurface));
}

int DesktopApplication::exec() {
    return impl_->exec();
}

void DesktopApplication::exit(const int exitCode) noexcept {
    impl_->exit(exitCode);
}

double DesktopApplication::activeScreenRefreshRate() const noexcept {
    return impl_->activeScreenRefreshRate();
}

bool DesktopApplication::reviewLocalFiles(const QList<QUrl>& files) {
    return impl_->reviewLocalFiles(files);
}

bool DesktopApplication::enqueueStartupRequest(const int kind, const QList<QUrl>& files) {
    return impl_->enqueueStartupRequest(kind, files);
}

void DesktopApplication::activateWindow() noexcept {
    impl_->activateWindow();
}

bool DesktopApplication::openSourcesForAutomation(const QList<QUrl>& sources) noexcept {
    return impl_->openSourcesForAutomation(sources);
}

bool DesktopApplication::clickControlForAutomation(const std::string_view objectName) noexcept {
    return impl_->clickControlForAutomation(objectName);
}

bool DesktopApplication::focusControlForAutomation(const std::string_view objectName) noexcept {
    return impl_->focusControlForAutomation(objectName);
}

bool DesktopApplication::clickTimelineForAutomation(const double normalizedPosition) noexcept {
    return impl_->clickTimelineForAutomation(normalizedPosition);
}

bool DesktopApplication::sendKeyForAutomation(const int key, const int modifiers) noexcept {
    return impl_->sendKeyForAutomation(key, modifiers);
}

std::optional<std::string>
DesktopApplication::objectStringPropertyForAutomation(const std::string_view objectName,
                                                      const std::string_view propertyName) const {
    return impl_->objectStringPropertyForAutomation(objectName, propertyName);
}

std::optional<int>
DesktopApplication::objectIntPropertyForAutomation(const std::string_view objectName,
                                                   const std::string_view propertyName) const {
    return impl_->objectIntPropertyForAutomation(objectName, propertyName);
}

std::optional<QImage>
DesktopApplication::captureControlForAutomation(const std::string_view objectName) const {
    return impl_->captureControlForAutomation(objectName);
}

bool DesktopApplication::openMenuForAutomation(const std::string_view objectName) noexcept {
    return impl_->openMenuForAutomation(objectName);
}

bool DesktopApplication::closeMenuForAutomation(const std::string_view objectName) noexcept {
    return impl_->closeMenuForAutomation(objectName);
}

bool DesktopApplication::menuIsOpenForAutomation(const std::string_view objectName) const noexcept {
    return impl_->menuIsOpenForAutomation(objectName);
}

std::optional<QImage>
DesktopApplication::capturePopupWindowForAutomation(const std::string_view objectName) const {
    return impl_->capturePopupWindowForAutomation(objectName);
}

void DesktopApplication::releaseSceneGraph() noexcept {
    impl_->releaseSceneGraph();
}

} // namespace dvs::ui
