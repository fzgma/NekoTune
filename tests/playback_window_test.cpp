#include "app_paths.h"
#include "controllers/app_controllers.h"
#include "i18n.h"
#include "platform/playback_window_controller.h"
#include "platform/system_media_bridge.h"
#include "platform/system_tray_controller.h"
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QDir>
#include <QJsonArray>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QtTest>

class PlaybackWindowTest final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_directory;
    std::unique_ptr<IpcClient> m_client;
    std::unique_ptr<AppControllers> m_controllers;
    std::unique_ptr<I18n> m_translator;
    std::unique_ptr<PlaybackWindowController> m_windows;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QWindow> m_main;
    QWindow *window(const char *name) { return m_windows->findChild<QWindow *>(name); }
    void setupWindows(const QString &platform) {
        m_windows = std::make_unique<PlaybackWindowController>(nullptr, platform);
        m_engine = std::make_unique<QQmlEngine>();
        m_engine->rootContext()->setContextProperty("controllers", m_controllers.get());
        m_engine->rootContext()->setContextProperty("i18n", m_translator.get());
        m_engine->rootContext()->setContextProperty("ipcClient", m_client.get());
        m_engine->rootContext()->setContextProperty("playbackWindows", m_windows.get());
        m_main = std::make_unique<QWindow>();
        m_main->resize(640, 480);
        m_main->show();
        m_windows->initialize(m_engine.get(), m_main.get());
    }
  private slots:
    void init() {
        m_directory = std::make_unique<QTemporaryDir>();
        qputenv("NEKOTUNE_HOME", m_directory->filePath("home").toUtf8());
        qputenv("NEKOTUNE_SOCKET", m_directory->filePath("unused.sock").toUtf8());
        qputenv("NEKOTUNE_SAFE_MODE", "1");
        qputenv("NEKOTUNE_LANGUAGE", "zh");
        QVERIFY(nekotune::AppPaths::prepare());
        m_client = std::make_unique<IpcClient>();
        m_controllers = std::make_unique<AppControllers>(*m_client);
        m_translator = std::make_unique<I18n>();
    }
    void cleanup() {
        m_windows.reset(); m_engine.reset(); m_main.reset();
        m_controllers.reset(); m_client.reset(); m_translator.reset(); m_directory.reset();
    }
    void miniSwitchCloseAndRestoreState() {
        setupWindows("xcb");
        m_main->setProperty("viewMode", "library");
        m_main->setWindowState(Qt::WindowMaximized);
        m_windows->setMiniMode(true);
        QVERIFY(m_windows->miniMode());
        QVERIFY(!m_main->isVisible());
        auto *mini = window("miniPlayerWindow");
        QVERIFY(mini && mini->isVisible());
        QVERIFY(!mini->transientParent());
        QVERIFY(!mini->flags().testFlag(Qt::WindowStaysOnTopHint));
        m_windows->setMiniAlwaysOnTop(true);
        QVERIFY(mini->isVisible());
        QVERIFY(mini->flags().testFlag(Qt::WindowStaysOnTopHint));
        mini->close();
        QVERIFY(!m_windows->miniMode());
        QVERIFY(!mini->isVisible());
        QVERIFY(m_main->isVisible());
        QCOMPARE(m_main->windowState(), Qt::WindowMaximized);
        QCOMPARE(m_main->property("viewMode").toString(), QString("library"));
        m_windows->setMiniMode(true);
        m_windows->showMainWindow();
        QVERIFY(!m_windows->miniMode());
        QCOMPARE(window("miniPlayerWindow"), mini);
    }
    void lyricsLockRecoveryAndShutdown() {
        setupWindows("xcb");
        m_windows->setDesktopLyricsVisible(true);
        QVERIFY(m_windows->desktopLyricsVisible());
        auto *lyrics = window("desktopLyricsWindow");
        QVERIFY(lyrics && !lyrics->transientParent());
        QVERIFY(lyrics->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        m_windows->setDesktopLyricsLocked(true);
        QVERIFY(!m_windows->desktopLyricsLocked());
        m_windows->setTrayAvailable(true);
        m_windows->setDesktopLyricsLocked(true);
        QVERIFY(m_windows->desktopLyricsLocked());
        QVERIFY(lyrics->flags().testFlag(Qt::WindowTransparentForInput));
        m_windows->setMiniMode(true);
        QVERIFY(m_windows->desktopLyricsVisible());
        m_windows->setTrayAvailable(false);
        QVERIFY(!m_windows->desktopLyricsLocked());
        QVERIFY(!lyrics->flags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(m_windows->miniMode()); // A visible mini player needs no tray.
        m_windows->setTrayAvailable(true);
        m_windows->setDesktopLyricsLocked(true);
        lyrics->close();
        QVERIFY(!m_windows->desktopLyricsVisible());
        m_windows->setDesktopLyricsVisible(true);
        QVERIFY(!m_windows->desktopLyricsLocked());
        m_windows->shutdown();
        QVERIFY(!lyrics->isVisible());
        QVERIFY(!window("miniPlayerWindow")->isVisible());
        m_windows->setMiniMode(true);
        m_windows->setDesktopLyricsVisible(true);
        QVERIFY(!lyrics->isVisible());
        m_windows->shutdown();
    }
    void platformSupport_data() {
        QTest::addColumn<QString>("platform"); QTest::addColumn<bool>("supported");
        QTest::newRow("x11") << QString("xcb") << true;
        QTest::newRow("windows") << QString("windows") << true;
        QTest::newRow("wayland") << QString("wayland") << false;
        QTest::newRow("wayland-egl") << QString("wayland-egl") << false;
        QTest::newRow("offscreen") << QString("offscreen") << false;
    }
    void platformSupport() {
        QFETCH(QString, platform); QFETCH(bool, supported);
        setupWindows(platform);
        QCOMPARE(m_windows->desktopLyricsSupported(), supported);
        if (!supported) {
            m_windows->setDesktopLyricsVisible(true);
            QVERIFY(!m_windows->desktopLyricsVisible());
            QVERIFY(!m_windows->desktopLyricsUnavailableReason().isEmpty());
        }
        m_windows->setMiniMode(true);
        QVERIFY(m_windows->miniMode());
    }
    void preferencesClampAndPreserveOtherSettings() {
        QVERIFY(nekotune::AppPaths::saveSetting("language", "en"));
        QVERIFY(nekotune::AppPaths::saveSetting("playback_windows", QJsonObject{
            {"mini_always_on_top", true}, {"mini", QJsonObject{{"x", 999999}, {"y", -999999}, {"width", -5}, {"height", 999999}}}}));
        setupWindows("xcb");
        QVERIFY(m_windows->miniAlwaysOnTop());
        QVERIFY(!m_windows->miniMode());
        QVERIFY(!m_windows->desktopLyricsVisible());
        m_windows->setMiniMode(true);
        auto *mini = window("miniPlayerWindow");
        QVERIFY(mini->screen()->availableGeometry().contains(mini->geometry()));
        QVERIFY(mini->width() >= mini->minimumWidth());
        mini->resize(480, 180);
        QTRY_COMPARE(nekotune::AppPaths::setting("playback_windows").toObject().value("mini").toObject().value("width").toInt(), 480);
        QCOMPARE(nekotune::AppPaths::setting("language").toString(), QString("en"));
        m_windows.reset(); m_engine.reset(); m_main.reset();
        setupWindows("xcb");
        QVERIFY(!m_windows->miniMode());
        m_windows->setMiniMode(true);
        QCOMPARE(window("miniPlayerWindow")->size(), QSize(480, 180));
    }
    void missingEngineKeepsMainWindow() {
        m_windows = std::make_unique<PlaybackWindowController>(nullptr, "xcb");
        m_main = std::make_unique<QWindow>();
        m_main->show();
        m_windows->initialize(nullptr, m_main.get());
        QSignalSpy errors(m_windows.get(), &PlaybackWindowController::errorOccurred);
        m_windows->setMiniMode(true);
        QVERIFY(!m_windows->miniMode());
        QVERIFY(m_main->isVisible());
        QCOMPARE(errors.count(), 1);
    }
    void nativePlatformBehavior() {
        setupWindows(QGuiApplication::platformName());
        const bool supported = QGuiApplication::platformName() == "xcb" || QGuiApplication::platformName() == "windows";
        QCOMPARE(m_windows->desktopLyricsSupported(), supported);
        m_windows->setDesktopLyricsVisible(true);
        QCOMPARE(m_windows->desktopLyricsVisible(), supported);
        m_windows->setMiniMode(true);
        QVERIFY(m_windows->miniMode());
        auto *mini = qobject_cast<QQuickWindow *>(window("miniPlayerWindow"));
        QVERIFY(mini && mini->isVisible());
        QTest::qWait(100);
        const auto directory = qEnvironmentVariable("NEKOTUNE_TEST_SCREENSHOT_DIR");
        if (!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(mini->grabWindow().save(directory + "/native-mini.png"));
        }
        mini->close();
        QVERIFY(m_main->isVisible());
        QVERIFY(!m_windows->miniMode());
    }
    void trayActionsRestoreMainAndUnlock() {
        setupWindows("xcb");
        SystemMediaBridge bridge(*m_client, *m_controllers->playback);
        bool available = true;
        SystemTrayController tray(bridge, *m_translator, nullptr, [&available] { return available; });
        tray.setPlaybackWindows(m_windows.get());
        tray.attachWindow(m_main.get());
        auto *icon = tray.findChild<QSystemTrayIcon *>();
        auto action = [icon](const char *name) { return icon->contextMenu()->findChild<QAction *>(name); };
        action("trayMiniMode")->trigger();
        QVERIFY(m_windows->miniMode());
        emit icon->activated(QSystemTrayIcon::Trigger);
        QVERIFY(!m_windows->miniMode());
        QVERIFY(m_main->isVisible());
        action("trayDesktopLyrics")->trigger();
        QVERIFY(m_windows->desktopLyricsVisible());
        action("trayLockDesktopLyrics")->trigger();
        QVERIFY(m_windows->desktopLyricsLocked());
        action("trayLockDesktopLyrics")->trigger();
        QVERIFY(!m_windows->desktopLyricsLocked());
        m_windows->setMiniMode(true);
        tray.toggleWindow();
        QVERIFY(!m_windows->presentationVisible());
        available = false;
        QVERIFY(QMetaObject::invokeMethod(&tray, "refreshAvailability"));
        QVERIFY(m_main->isVisible());
        QVERIFY(!m_windows->miniMode());
    }
    void visualWindowsAndPlaybackCapabilities() {
        qputenv("NEKOTUNE_SAFE_MODE", "0");
        setupWindows("xcb");
        const auto song = QJsonObject{{"song_id", 1}, {"resource_key", "visual-track"},
                                     {"title", "夜樱电台 · Moonlight Radio"}, {"artist", "NekoTune"}};
        m_controllers->playback->apply({{"song", song}, {"state", "paused"}, {"position", 1500},
            {"capabilities", QJsonObject{{"can_play", true}, {"can_pause", true},
                                         {"can_go_previous", false}, {"can_go_next", true}}}});
        emit m_client->eventReceived({{"event", "lyrics.changed"}, {"lyrics", QJsonObject{
            {"track_id", "visual-track"}, {"state", "ready"}, {"document", QJsonObject{
                {"lines", QJsonArray{QJsonObject{{"time_ms", 1000}, {"text", "月光洒落在你的耳边"}, {"words", QJsonArray{
                    QJsonObject{{"time_ms", 1000}, {"duration_ms", 1000}, {"text", "月光"}},
                    QJsonObject{{"time_ms", 2000}, {"duration_ms", 1500}, {"text", "洒落在你的耳边"}}}}},
                    QJsonObject{{"time_ms", 4000}, {"text", "Let the music stay with you tonight"}}}}}}}}});
        m_windows->setMiniMode(true);
        m_windows->setDesktopLyricsVisible(true);
        auto *mini = qobject_cast<QQuickWindow *>(window("miniPlayerWindow"));
        auto *lyrics = qobject_cast<QQuickWindow *>(window("desktopLyricsWindow"));
        QVERIFY(mini && lyrics);
        auto *play = mini->findChild<QObject *>("miniPlayButton");
        QVERIFY(play && !play->property("enabled").toBool());
        QQmlComponent fixture(m_engine.get());
        fixture.setData("import QtQuick; QtObject { property bool connected: true }", QUrl());
        std::unique_ptr<QObject> transport(fixture.create());
        QVERIFY(transport);
        mini->setProperty("transport", QVariant::fromValue(transport.get()));
        lyrics->setProperty("transport", QVariant::fromValue(transport.get()));
        QVERIFY(play->property("enabled").toBool());
        QVERIFY(!mini->findChild<QObject *>("miniPreviousButton")->property("enabled").toBool());
        QVERIFY(mini->findChild<QObject *>("miniNextButton")->property("enabled").toBool());
        const auto directory = qEnvironmentVariable("NEKOTUNE_TEST_SCREENSHOT_DIR");
        for (const auto &language : {QString("zh"), QString("en")}) {
            m_translator->setLanguage(language);
            for (const auto &mode : {QString("dark"), QString("light")}) {
                m_controllers->theme->setMode(mode);
                QCOMPARE(m_controllers->theme->isDark(), mode == "dark");
                QTest::qWait(100);
                QCOMPARE(mini->color(), QColor(Qt::transparent));
                QCOMPARE(lyrics->color(), QColor(Qt::transparent));
                const auto miniImage = mini->grabWindow();
                const auto lyricsImage = lyrics->grabWindow();
                QVERIFY(!miniImage.isNull() && !lyricsImage.isNull());
                QCOMPARE(miniImage.pixelColor(0, 0).alpha(), 0);
                QCOMPARE(lyricsImage.pixelColor(0, 0).alpha(), 0);
                if (!directory.isEmpty()) {
                    QVERIFY(QDir().mkpath(directory));
                    QVERIFY(miniImage.save(directory + "/mini-" + language + "-" + mode + ".png"));
                    QVERIFY(lyricsImage.save(directory + "/desktop-lyrics-" + language + "-" + mode + ".png"));
                }
            }
        }
        // Restore real transport bindings before the fixture is destroyed.
        mini->setProperty("transport", QVariant::fromValue(m_client.get()));
        lyrics->setProperty("transport", QVariant::fromValue(m_client.get()));
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QQuickWindow::setDefaultAlphaBuffer(true);
    PlaybackWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "playback_window_test.moc"
