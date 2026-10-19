#include "controllers/app_controllers.h"
#include "i18n.h"
#include "app_paths.h"
#include "ipc_client.h"
#include "platform/system_media_bridge.h"
#include "platform/system_tray_controller.h"
#include "platform/playback_window_controller.h"
#include "wallpaper/wallpaper_manager.h"
#ifdef NEKOTUNE_MPRIS
#include "platform/linux/mpris_service.h"
#endif

#ifdef NEKOTUNE_EMBED_BACKEND
#include "runtime/backend_runtime.h"
#endif

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QApplication>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QWindow>

// The QML engine owns its image network manager separately from backend HTTP.
// Prefer disk cache only for the trusted provider image host.
// This reduces repeat artwork requests when list delegates are recreated.
// Offline selection still comes from backend-resolved empty/local cover URLs.
// The manager does not decide song identity or cover precedence.
class CoverNetworkManager final : public QNetworkAccessManager {
  public:
    using QNetworkAccessManager::QNetworkAccessManager;

  protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData = nullptr) override {
        QNetworkRequest cachedRequest(request);
        if (operation == GetOperation && request.url().scheme() == "https")
            cachedRequest.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                                       QNetworkRequest::PreferCache);
        return QNetworkAccessManager::createRequest(operation, cachedRequest, outgoingData);
    }
};

class CoverNetworkManagerFactory final : public QQmlNetworkAccessManagerFactory {
  public:
    QNetworkAccessManager *create(QObject *parent) override {
        auto *manager = new CoverNetworkManager(parent);
        auto *cache = new QNetworkDiskCache(manager);
        const auto directory = nekotune::AppPaths::configFile("covers");
        cache->setCacheDirectory(directory);
        cache->setMaximumCacheSize(64 * 1024 * 1024);
        manager->setCache(cache);
        return manager;
    }
};

// One entry point builds standalone client and embedded desktop variants.
// The embedded define starts BackendRuntime before frontend connection.
// Both variants still communicate through the same local socket protocol.
// Construct controllers before loading QML so context properties are ready.
// Declaration order keeps the engine's referenced controllers alive through
// QML teardown and lets the runtime stop on application exit.
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QGuiApplication::setApplicationName(QStringLiteral("NekoTune"));
    QGuiApplication::setOrganizationName(QStringLiteral("NekoTune"));
    QString directoryError;
    if (!nekotune::AppPaths::prepare(&directoryError)) { qCritical() << directoryError; return 1; }
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    QQuickWindow::setDefaultAlphaBuffer(true);
    if (app.arguments().contains("--safe-mode"))
        qputenv("NEKOTUNE_SAFE_MODE", "1");

    QCommandLineParser commandLine;
    commandLine.setApplicationDescription(QStringLiteral("NekoTune Qt/QML client"));
    commandLine.addHelpOption();
    commandLine.addOption(
        QCommandLineOption(QStringLiteral("safe-mode"), QStringLiteral("Start with extensions disabled.")));
    commandLine.addOption(
        QCommandLineOption({QStringLiteral("lyrics-debug"), QStringLiteral("debug-lyrics")},
                           QStringLiteral("Enable the lyrics diagnostics page in the sidebar.")));
    commandLine.process(app);

#ifdef NEKOTUNE_EMBED_BACKEND
    nekotune::BackendRuntime runtime;
    if (!runtime.start()) {
        qCritical() << runtime.errorString();
        return 1;
    }
    qInfo() << "NekoTune backend listening on" << runtime.serverName();
#endif

    IpcClient ipcClient;
    I18n i18n;
    AppControllers controllers(ipcClient);
    SystemMediaBridge mediaBridge(ipcClient, *controllers.playback);
    SystemTrayController desktopIntegration(mediaBridge, i18n);
    QObject::connect(&desktopIntegration, &SystemTrayController::quitRequested, &app, &QCoreApplication::quit);
    QObject::connect(&mediaBridge, &SystemMediaBridge::raiseRequested, &desktopIntegration, &SystemTrayController::showWindow);
    QObject::connect(&mediaBridge, &SystemMediaBridge::quitRequested, &desktopIntegration, &SystemTrayController::quit);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &desktopIntegration, &SystemTrayController::shutdown);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &mediaBridge, &SystemMediaBridge::shutdown);
#ifdef NEKOTUNE_MPRIS
    MprisService mpris(mediaBridge);
#endif

    QQmlApplicationEngine engine;
    // Destroy auxiliary QML windows before the engine they were created in.
    PlaybackWindowController playbackWindows;
    WallpaperManager wallpaper;
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &wallpaper, &WallpaperManager::shutdown);
#ifdef NEKOTUNE_EMBED_BACKEND
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &runtime, &nekotune::BackendRuntime::stop);
#endif
    desktopIntegration.setPlaybackWindows(&playbackWindows);
    engine.setNetworkAccessManagerFactory(new CoverNetworkManagerFactory);
    engine.rootContext()->setContextProperty(QStringLiteral("ipcClient"), &ipcClient);
    engine.rootContext()->setContextProperty(QStringLiteral("i18n"), &i18n);
    engine.rootContext()->setContextProperty(QStringLiteral("controllers"), &controllers);
    engine.rootContext()->setContextProperty(QStringLiteral("desktopIntegration"), &desktopIntegration);
    engine.rootContext()->setContextProperty(QStringLiteral("playbackWindows"), &playbackWindows);
    engine.rootContext()->setContextProperty(QStringLiteral("wallpaperManager"), &wallpaper);
    engine.rootContext()->setContextProperty(QStringLiteral("lyricsDebugEnabled"),
                                             commandLine.isSet(QStringLiteral("lyrics-debug")) ||
                                                 commandLine.isSet(QStringLiteral("debug-lyrics")));
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    QQmlApplicationEngine recoveryEngine;
    recoveryEngine.rootContext()->setContextProperty(QStringLiteral("controllers"), &controllers);
    recoveryEngine.rootContext()->setContextProperty(QStringLiteral("i18n"), &i18n);
    recoveryEngine.rootContext()->setContextProperty(QStringLiteral("ignoreExtensionTheme"), true);
    recoveryEngine.load(QUrl(QStringLiteral("qrc:/qml/ExtensionsWindow.qml")));
    QObject::connect(controllers.extensions, &ExtensionsController::managerRequested, &app,
                     [&recoveryEngine] {
                         if (recoveryEngine.rootObjects().isEmpty())
                             return;
                         if (auto *window = qobject_cast<QWindow *>(recoveryEngine.rootObjects().first())) {
                             window->show();
                             window->raise();
                             window->requestActivate();
                         }
                     });

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    const auto window = qobject_cast<QWindow *>(engine.rootObjects().first());
    if (!window) return 1;
    playbackWindows.initialize(&engine, window);
    desktopIntegration.attachWindow(window);
    wallpaper.initialize(&engine, &ipcClient);

    return app.exec();
}
