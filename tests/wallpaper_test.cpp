#include "app_paths.h"
#include "controllers/app_controllers.h"
#include "extensions/extension_view.h"
#include "i18n.h"
#include "wallpaper/wallpaper_manager.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlContext>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestPlatform final : public WallpaperPlatform {
  public:
    int prepares = 0;
    bool recover = false;
    bool supportsTransparency() const override { return true; }
    QString unavailable, failure;
    QString unavailableReason() const override { return unavailable; }
    QString prepare(QWindow *window, QScreen *screen) override {
        ++prepares;
        window->setScreen(screen);
        window->resize(1024, 720);
        return failure;
    }
    QString check(QWindow *, QScreen *) override { return failure; }
    bool needsHealthCheck() const override { return recover; }
    bool retryable() const override { return recover; }
};

class WallpaperTest final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_directory;
    std::unique_ptr<QLocalServer> m_server;
    QPointer<QLocalSocket> m_peer;
    std::unique_ptr<IpcClient> m_client;
    std::unique_ptr<AppControllers> m_controllers;
    std::unique_ptr<I18n> m_i18n;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<WallpaperManager> m_manager;
    TestPlatform *m_platform = nullptr;
    QStringList m_requests;
    void setup(bool real = false) {
        auto platform = std::make_unique<TestPlatform>();
        m_platform = platform.get();
        m_manager = real ? std::make_unique<WallpaperManager>()
                         : std::make_unique<WallpaperManager>(nullptr, std::move(platform));
        m_engine->rootContext()->setContextProperty("wallpaperManager", m_manager.get());
        m_manager->initialize(m_engine.get(), m_client.get());
    }
    QQuickWindow *window() { return m_manager->findChild<QQuickWindow *>("musicWallpaperWindow"); }
    void send(const QJsonObject &message) {
        QVERIFY(m_peer);
        m_peer->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
        m_peer->flush();
    }
    QJsonObject snapshot(const QString &identity = "track-a") {
        return {
            {"event", "server.connected"},
            {"data",
             QJsonObject{
                 {"state", "playing"},
                 {"position", 1500},
                 {"duration", 20000},
                 {"playback_rate", 1.5},
                 {"song", QJsonObject{{"resource_key", identity},
                                      {"title", "月光电台 · Moonlight Radio"},
                                      {"artist", "NekoTune"},
                                      {"album", "夜樱"}}},
                 {"lyrics",
                  QJsonObject{
                      {"track_id", identity},
                      {"state", "ready"},
                      {"document",
                       QJsonObject{
                           {"lines", QJsonArray{QJsonObject{{"time_ms", 1000},
                                                            {"text", "月光洒落在你的耳边"},
                                                            {"words", QJsonArray{QJsonObject{
                                                                          {"time_ms", 1000},
                                                                          {"duration_ms", 3000},
                                                                          {"text", "月光洒落在你的耳边"}}}}},
                                                QJsonObject{{"time_ms", 6000},
                                                            {"text", "Let the music stay with you"}}}}}}}}}}};
    }
    void publishTheme(const QString &directory, const QString &source = "Wallpaper.qml",
                      int generation = 1, const QString &state = "running",
                      const QString &id = "test.wallpaper/default") {
        send({{"event", "extensions.changed"},
              {"selections", QJsonObject{{"theme", id}}},
              {"extensions", QJsonArray{QJsonObject{
                  {"id", "test.wallpaper"}, {"type", "theme"}, {"state", state},
                  {"directory", directory}, {"generation", generation}, {"trusted", true},
                  {"themes", QJsonArray{QJsonObject{
                      {"id", id}, {"schemaVersion", 1},
                      {"variants", QJsonObject{{"light", QJsonObject{}}, {"dark", QJsonObject{}}}},
                      {"ui", QJsonObject{{"wallpaper", source}}}}}}}}}});
    }
    ExtensionView *themeView() {
        return window() ? window()->findChild<ExtensionView *>("wallpaperThemeSlotTheme") : nullptr;
    }
  private slots:
    void init() {
        m_directory = std::make_unique<QTemporaryDir>();
        qputenv("NEKOTUNE_HOME", m_directory->filePath("home").toUtf8());
        qputenv("NEKOTUNE_SOCKET", m_directory->filePath("backend.sock").toUtf8());
        qputenv("NEKOTUNE_SAFE_MODE", "1");
        qputenv("NEKOTUNE_LANGUAGE", "en");
        QVERIFY(nekotune::AppPaths::prepare());
        m_server = std::make_unique<QLocalServer>();
        QVERIFY(m_server->listen(qEnvironmentVariable("NEKOTUNE_SOCKET")));
        connect(m_server.get(), &QLocalServer::newConnection, this, [this] {
            m_peer = m_server->nextPendingConnection();
            connect(m_peer, &QLocalSocket::readyRead, this, [this] {
                while (m_peer && m_peer->canReadLine()) {
                    auto message = QJsonDocument::fromJson(m_peer->readLine()).object();
                    m_requests.append(message.value("method").toString());
                    QJsonObject data = message.value("method") == "extensions.list"
                        ? QJsonObject{{"catalogueReady", true}, {"startupComplete", true}}
                        : QJsonObject{};
                    if (message.value("method") == "extensions.call" && message.value("params").toObject().value("id") == "nekotune.spectrum")
                        data = {{"token", "visual-test"}, {"available", true}};
                    m_peer->write(QJsonDocument(QJsonObject{{"id", message.value("id")},
                                                            {"status", "ok"},
                                                            {"data", data}})
                                      .toJson(QJsonDocument::Compact) +
                                  '\n');
                }
            });
        });
        m_client = std::make_unique<IpcClient>();
        m_controllers = std::make_unique<AppControllers>(*m_client);
        m_i18n = std::make_unique<I18n>();
        m_engine = std::make_unique<QQmlEngine>();
        m_engine->rootContext()->setContextProperty("controllers", m_controllers.get());
        m_engine->rootContext()->setContextProperty("i18n", m_i18n.get());
        m_engine->rootContext()->setContextProperty("ipcClient", m_client.get());
        m_requests.clear();
        QTRY_VERIFY(m_peer && m_client->connected());
    }
    void cleanup() {
        m_manager.reset();
        m_engine.reset();
        m_controllers.reset();
        m_i18n.reset();
        m_client.reset();
        m_server.reset();
        m_peer.clear();
        m_directory.reset();
    }
    void runtimeToggleSharesControllersAndDoesNotSendCommands() {
        setup();
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QTest::qWait(100);
        const auto requests = m_requests;
        for (int i = 0; i < 3; ++i) {
            QVERIFY(m_manager->setOption("enabled", true));
            QTRY_COMPARE(m_manager->runningCount(), 1);
            QPointer<QQuickWindow> first = window();
            QVERIFY(first);
            QCOMPARE(first->property("app").value<QObject *>(), m_controllers.get());
            QCOMPARE(first->property("transport").value<QObject *>(), m_client.get());
            QVERIFY(first->flags().testFlag(Qt::WindowTransparentForInput));
            QVERIFY(!first->transientParent());
            QVERIFY(m_manager->setOption("enabled", true));
            QCOMPARE(window(), first.data());
            QCOMPARE(m_manager->findChildren<QQuickWindow *>().size(), 1);
            QVERIFY(m_manager->setOption("enabled", false));
            QVERIFY(first.isNull());
            QCOMPARE(m_manager->runningCount(), 0);
            QCOMPARE(m_controllers->playback->state(), QString("playing"));
        }
        QCOMPARE(m_requests, requests);
        QCOMPARE(m_platform->prepares, 3);
        m_manager->shutdown();
        m_manager->shutdown();
        QVERIFY(!m_manager->setOption("enabled", true));
    }
    void preferencesValidatePersistAndFailHonestly() {
        QVERIFY(nekotune::AppPaths::saveSetting("language", "en"));
        QVERIFY(nekotune::AppPaths::saveSetting("wallpaper", QJsonObject{{"target", "invalid"},
                                                                         {"style", "unknown"},
                                                                         {"updateHz", -1},
                                                                         {"intensity", 900},
                                                                         {"autoRestore", "yes"},
                                                                         {"spectrum", true}}));
        setup();
        QCOMPARE(m_manager->settings().value("target").toString(), QString("primary"));
        QCOMPARE(m_manager->settings().value("style").toString(), QString("classic"));
        QVERIFY(!m_manager->setOption("style", "unknown"));
        QVERIFY(!m_manager->setOption("style", true));
        QVERIFY(m_manager->setOption("style", "lyrics"));
        m_manager.reset();
        setup();
        QCOMPARE(m_manager->settings().value("style").toString(), QString("lyrics"));
        QCOMPARE(m_manager->settings().value("updateHz").toInt(), 30);
        QCOMPARE(m_manager->settings().value("intensity").toDouble(), .35);
        QVERIFY(!m_manager->setOption("updateHz", 45));
        QVERIFY(!m_manager->setOption("spectrum", true));
        QVERIFY(!m_manager->audioAnalysis().value("spectrumAvailable").toBool());
        QVERIFY(m_manager->setOption("updateHz", 60));
        QCOMPARE(nekotune::AppPaths::setting("language").toString(), QString("en"));
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        QFile settings(nekotune::AppPaths::configFile("settings.json"));
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("broken json");
        settings.close();
        QVERIFY(!m_manager->setOption("updateHz", 30));
        QVERIFY(!m_manager->setOption("style", "vinyl"));
        QCOMPARE(m_manager->settings().value("style").toString(), QString("lyrics"));
        QCOMPARE(m_manager->settings().value("updateHz").toInt(), 60);
        QCOMPARE(m_manager->error(), QString("wallpaper_save_failed"));
        QVERIFY(!m_manager->setOption("enabled", false));
        QVERIFY(!window());
        QVERIFY(!m_manager->requestedEnabled());
    }
    void restoreIsSeparateFromSavedIntentAndSafeMode() {
        QVERIFY(nekotune::AppPaths::saveSetting("wallpaper",
                                                QJsonObject{{"enabled", true}, {"autoRestore", false}}));
        setup();
        QVERIFY(!m_manager->requestedEnabled());
        m_manager.reset();
        QVERIFY(nekotune::AppPaths::saveSetting("wallpaper",
                                                QJsonObject{{"enabled", true}, {"autoRestore", true}}));
        setup();
        QVERIFY(!m_manager->requestedEnabled()); // --safe-mode escape hatch
        m_manager.reset();
        qputenv("NEKOTUNE_SAFE_MODE", "0");
        setup();
        QTRY_COMPARE(m_manager->runningCount(), 1);
        QVERIFY(m_manager->requestedEnabled());
    }
    void missingOutputFailureAndRemovalReleaseWindows() {
        setup();
        QVERIFY(m_manager->setOption("target", "selected"));
        QVERIFY(m_manager->setOption("screenId", "disconnected-display"));
        QVERIFY(m_manager->setOption("enabled", true));
        QVERIFY(!window());
        QCOMPARE(m_manager->outputs().last().toMap().value("state").toString(), QString("unavailable"));
        m_platform->failure = "wallpaper_windows_embed";
        QVERIFY(m_manager->setOption("target", "primary"));
        QVERIFY(!window());
        QCOMPARE(m_manager->outputs().first().toMap().value("state").toString(), QString("failed"));
        m_platform->failure.clear();
        m_manager->retry();
        QTRY_COMPARE(m_manager->runningCount(), 1);
        QPointer<QQuickWindow> first = window();
        emit qGuiApp->screenRemoved(QGuiApplication::primaryScreen());
        QVERIFY(first.isNull());
        m_manager->shutdown();
        QCoreApplication::processEvents(); // queued removal callbacks must be
                                           // harmless
        QVERIFY(!window());
    }
    void disconnectReconnectAndLateLyrics_data() {
        QTest::addColumn<QString>("style");
        for (const auto &style : {"classic", "vinyl", "lyrics"})
            QTest::newRow(style) << QString(style);
    }
    void disconnectReconnectAndLateLyrics() {
        QFETCH(QString, style);
        setup();
        QVERIFY(m_manager->setOption("style", style));
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        auto *lyrics = window()->findChild<QObject *>("wallpaperLyrics");
        QVERIFY(lyrics);
        QCOMPARE(lyrics->property("activeIndex").toInt(), 0);
        send({{"event", "player.position_changed"}, {"position", 7000}});
        QTRY_COMPARE(lyrics->property("activeIndex").toInt(), 1);
        QCOMPARE(m_controllers->playback->song().value("resource_key").toString(), QString("track-a"));
        send({{"event", "player.track_changed"},
              {"song", QJsonObject{{"resource_key", "track-b"}, {"title", "B"}}}});
        QTRY_COMPARE(lyrics->property("songIdentity").toString(), QString("track-b"));
        QCOMPARE(lyrics->property("activeIndex").toInt(), -1);
        send({{"event", "lyrics.changed"}, {"lyrics", snapshot().value("data").toObject().value("lyrics")}});
        QTest::qWait(30);
        QCOMPARE(lyrics->property("activeIndex").toInt(), -1);
        m_peer->disconnectFromServer();
        QTRY_VERIFY(!m_manager->dataReady());
        QVERIFY(!window()->property("playing").toBool());
        QTRY_VERIFY(m_client->connected());
        QVERIFY(!m_manager->dataReady());
        send(snapshot("track-b"));
        QTRY_VERIFY(m_manager->dataReady());
        QCOMPARE(lyrics->property("activeIndex").toInt(), 0);
        send({{"event", "player.state_changed"}, {"state", "paused"}});
        QTRY_VERIFY(!window()->property("playing").toBool());
        QCOMPARE(m_controllers->playback->playbackRate(), 1.5);
    }
    void lyricTransitionsRespectWallpaperSettings_data() {
        disconnectReconnectAndLateLyrics_data();
    }
    void lyricTransitionsRespectWallpaperSettings() {
        QFETCH(QString, style);
        setup();
        QVERIFY(m_manager->setOption("style", style));
        QVERIFY(m_manager->setOption("nextLine", true));
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        window()->resize(1465, 800);
        auto *lyrics = window()->findChild<QObject *>("wallpaperLyrics");
        auto *animation = window()->findChild<QObject *>("compactLineTransition");
        auto *outgoing = window()->findChild<QObject *>("compactOutgoingFrame");
        QVERIFY(lyrics && animation && outgoing);
        QTest::qWait(50);
        const auto directory = qEnvironmentVariable("NEKOTUNE_TEST_SCREENSHOT_DIR");
        if (!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(window()->grabWindow().save(directory + "/lyrics-before-" + style + ".png"));
        }
        send({{"event", "player.position_changed"}, {"position", 7000}});
        QTRY_VERIFY(animation->property("running").toBool());
        QVERIFY(outgoing->property("active").toBool());
        if (!directory.isEmpty()) {
            QTest::qWait(40);
            QVERIFY(window()->grabWindow().save(directory + "/lyrics-transition-" + style + ".png"));
        }
        QTRY_VERIFY(!animation->property("running").toBool());
        QVERIFY(!outgoing->property("active").toBool());
        if (!directory.isEmpty())
            QVERIFY(window()->grabWindow().save(directory + "/lyrics-after-" + style + ".png"));

        QVERIFY(m_manager->setOption("intensity", 0.0));
        send({{"event", "player.position_changed"}, {"position", 1500}});
        QTRY_COMPARE(lyrics->property("activeIndex").toInt(), 0);
        QVERIFY(!animation->property("running").toBool());
        QVERIFY(!outgoing->property("active").toBool());
        QVERIFY(m_manager->setOption("intensity", 0.5));
        send({{"event", "player.position_changed"}, {"position", 7000}});
        QTRY_VERIFY(animation->property("running").toBool());
        QVERIFY(m_manager->setOption("lyrics", false));
        QVERIFY(!animation->property("running").toBool());
        QVERIFY(!outgoing->property("active").toBool());
    }
    void actualUnsupportedQpaNeverCreatesWindow() {
        if (QGuiApplication::platformName() == "wayland" || QGuiApplication::platformName() == "windows")
            QSKIP("Unsupported-QPA test only");
        setup(true);
        QVERIFY(!m_manager->supported());
        QVERIFY(m_manager->setOption("enabled", true));
        QVERIFY(!window());
        QVERIFY(!m_manager->unavailableReason().isEmpty());
    }
    void recoveryIsCancelledByDisableAndShutdown() {
        setup();
        m_platform->recover = true;
        m_platform->failure = "wallpaper_windows_structure";
        QVERIFY(m_manager->setOption("enabled", true));
        QCOMPARE(m_manager->outputs().first().toMap().value("state").toString(), QString("retrying"));
        QVERIFY(m_manager->setOption("enabled", false));
        QTest::qWait(1200);
        QCOMPARE(m_platform->prepares, 1);
        QVERIFY(!window());
        QVERIFY(m_manager->setOption("enabled", true));
        m_platform->failure.clear();
        QTRY_COMPARE_WITH_TIMEOUT(m_manager->runningCount(), 1, 4000);
        m_platform->failure = "wallpaper_surface_lost";
        QTRY_VERIFY(!window());
        const int attempts = m_platform->prepares;
        m_manager->shutdown();
        QTest::qWait(1200);
        QCOMPARE(m_platform->prepares, attempts);
        QVERIFY(!window());
    }
    void hiddenWindowStopsInterpolationAndClosesSafely_data() {
        disconnectReconnectAndLateLyrics_data();
    }
    void hiddenWindowStopsInterpolationAndClosesSafely() {
        QFETCH(QString, style);
        setup();
        QVERIFY(m_manager->setOption("style", style));
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        auto *lyrics = window()->findChild<QObject *>("wallpaperLyrics");
        auto *timer = window()->findChild<QObject *>("lyricsFrameTimer");
        QVERIFY(timer && lyrics);
        QCOMPARE(timer->property("interval").toInt(), 33);
        QVERIFY(m_manager->setOption("updateHz", 60));
        QCOMPARE(timer->property("interval").toInt(), 16);
        window()->hide();
        QVERIFY(!lyrics->property("running").toBool());
        QVERIFY(!timer->property("running").toBool());
        window()->show();
        QPointer<QQuickWindow> first = window();
        first->close();
        QTRY_VERIFY(first.isNull());
        QCOMPARE(m_manager->outputs().first().toMap().value("state").toString(), QString("failed"));
        m_manager->retry();
        QTRY_COMPARE(m_manager->runningCount(), 1);
        first = window();
        first->destroy();
        m_manager->shutdown();
        QVERIFY(first.isNull());
        QCoreApplication::processEvents();
        QVERIFY(!window());
    }
    void stylesReuseWindowAndReleaseInactiveLayouts() {
        setup();
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        QTest::qWait(100);
        const auto requests = m_requests;
        QPointer<QQuickWindow> first = window();
        const auto nativeId = first->winId();
        for (const auto &style : {"vinyl", "lyrics", "classic", "lyrics", "vinyl"}) {
            QPointer<QObject> previous = first->findChild<QObject *>("wallpaperLyrics");
            QVERIFY(previous);
            QVERIFY(m_manager->setOption("style", style));
            QTRY_VERIFY(previous.isNull());
            QCOMPARE(window(), first.data());
            QCOMPARE(window()->winId(), nativeId);
            QCOMPARE(m_manager->runningCount(), 1);
            QCOMPARE(m_platform->prepares, 1);
            QCOMPARE(window()->findChildren<QObject *>("wallpaperLyrics").size(), 1);
            auto *lyrics = window()->findChild<QObject *>("wallpaperLyrics");
            QCOMPARE(lyrics->property("activeIndex").toInt(), 0);
            QCOMPARE(lyrics->property("position").toDouble(), 1500.0);
            QCOMPARE(lyrics->property("playbackRate").toDouble(), 1.5);
            QCOMPARE(window()->property("app").value<QObject *>(), m_controllers.get());
            QVERIFY(m_manager->setOption("lyrics", false));
            QVERIFY(!lyrics->property("running").toBool());
            QVERIFY(!lyrics->findChild<QObject *>("lyricsFrameTimer")->property("running").toBool());
            QVERIFY(m_manager->setOption("lyrics", true));
        }
        QCOMPARE(m_requests, requests);
    }
    void themeWallpaperIsOptInAndReloadsWithoutRecreatingWindow() {
        qputenv("NEKOTUNE_SAFE_MODE", "0");
        setup();
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QTRY_VERIFY(m_controllers->extensions->appearanceReady());
        const auto example = QFINDTESTDATA("../examples/extensions/theme-qml/Wallpaper.qml");
        QVERIFY(!example.isEmpty());
        const auto folder = QFileInfo(example).absolutePath();
        publishTheme(folder);
        QTRY_VERIFY(m_controllers->theme->uiSlots().contains("wallpaper"));
        QVERIFY(!window());
        QVERIFY(!m_manager->requestedEnabled());
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        QVERIFY(themeView());
        QVERIFY(!themeView()->ready());
        QPointer<QQuickWindow> first = window();
        const auto nativeId = first->winId();
        QTest::qWait(100);
        const auto requests = m_requests;
        QVERIFY(m_manager->setOption("style", "theme"));
        QTRY_COMPARE(first->property("themeStatus").toString(), QString("active"));
        QVERIFY(themeView()->ready());
        QPointer<QQuickItem> previous = themeView()->item();
        const auto context = themeView()->wallpaperContext;
        QVERIFY(context);
        QCOMPARE(context->property("song").toMap().value("resource_key").toString(), QString("track-a"));
        QCOMPARE(context->property("position").toDouble(), 1500.0);
        QCOMPARE(context->property("duration").toDouble(), 20000.0);
        QCOMPARE(context->property("playbackRate").toDouble(), 1.5);
        QVERIFY(context->property("connected").toBool());
        QVERIFY(context->property("screen").value<QObject *>());
        QVERIFY(!context->setProperty("position", 12345));
        QCOMPARE(context->property("position").toDouble(), 1500.0);
        m_controllers->theme->setMode("light");
        QTest::qWait(50);
        QCOMPARE(themeView()->item(), previous.data());
        for (int generation : {2, 3}) {
            publishTheme(folder, "Wallpaper.qml", generation);
            QTRY_VERIFY(previous.isNull());
            QTRY_VERIFY(themeView()->ready());
            previous = themeView()->item();
            QCOMPARE(window(), first.data());
            QCOMPARE(first->winId(), nativeId);
        }
        publishTheme(folder, "Wallpaper.qml", 4, "running", "test.wallpaper/other");
        QTRY_VERIFY(previous.isNull());
        QTRY_VERIFY(themeView()->ready());
        previous = themeView()->item();
        QVERIFY(m_manager->setOption("style", "vinyl"));
        QTRY_VERIFY(previous.isNull());
        QVERIFY(!themeView()->ready());
        QCOMPARE(first->property("themeStatus").toString(), QString());
        QCOMPARE(first->winId(), nativeId);
        QCOMPARE(m_platform->prepares, 1);
        QCOMPARE(m_requests, requests);
        QVERIFY(m_manager->setOption("style", "theme"));
        QTRY_VERIFY(themeView()->ready());
        previous = themeView()->item();
        QVERIFY(m_manager->setOption("enabled", false));
        QVERIFY(first.isNull());
        QVERIFY(previous.isNull());
    }
    void themeWallpaperFallbackReportsErrorsAndRecovers() {
        qputenv("NEKOTUNE_SAFE_MODE", "0");
        setup();
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QVERIFY(m_manager->setOption("style", "theme"));
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        QPointer<QQuickWindow> first = window();
        const auto nativeId = first->winId();
        QCOMPARE(first->property("themeStatus").toString(), QString("missing"));
        QVERIFY(first->findChild<QObject *>("wallpaperLyrics"));
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QFile broken(folder.filePath("Broken.qml"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("import QtQuick\nQtObject {}\n");
        broken.close();
        QSignalSpy changes(m_manager.get(), &WallpaperManager::outputsChanged);
        publishTheme(folder.path(), "Broken.qml");
        QTRY_COMPARE(first->property("themeStatus").toString(), QString("error"));
        QVERIFY(first->findChild<QObject *>("wallpaperLyrics"));
        auto output = m_manager->outputs().first().toMap();
        QCOMPARE(output.value("state").toString(), QString("running"));
        QCOMPARE(output.value("themeStatus").toString(), QString("error"));
        QVERIFY(!output.value("themeError").toString().isEmpty());
        QVERIFY(changes.count() > 0);
        QCOMPARE(m_manager->settings().value("style").toString(), QString("theme"));
        const auto example = QFINDTESTDATA("../examples/extensions/theme-qml/Wallpaper.qml");
        QVERIFY(!example.isEmpty());
        const auto directory = QFileInfo(example).absolutePath();
        publishTheme(directory, "Wallpaper.qml", 2);
        QTRY_COMPARE(first->property("themeStatus").toString(), QString("active"));
        QPointer<QQuickItem> previous = themeView()->item();
        publishTheme(directory, "Wallpaper.qml", 3, "disabled");
        QTRY_VERIFY(previous.isNull());
        QTRY_COMPARE(first->property("themeStatus").toString(), QString("missing"));
        QTRY_VERIFY(first->findChild<QObject *>("wallpaperLyrics"));
        publishTheme(directory, "Wallpaper.qml", 4);
        QTRY_COMPARE(first->property("themeStatus").toString(), QString("active"));
        previous = themeView()->item();
        send({{"event", "extensions.changed"}});
        QTRY_VERIFY(previous.isNull());
        QTRY_COMPARE(first->property("themeStatus").toString(), QString("missing"));
        QCOMPARE(first->winId(), nativeId);
        QCOMPARE(m_platform->prepares, 1);
        QCOMPARE(m_manager->settings().value("style").toString(), QString("theme"));
    }
    void themeWallpaperPreferenceRestoresAndSafeModeNeverLoadsCode() {
        QVERIFY(nekotune::AppPaths::saveSetting("wallpaper", QJsonObject{
            {"style", "theme"}, {"enabled", true}, {"autoRestore", true}}));
        setup();
        QCOMPARE(m_manager->settings().value("style").toString(), QString("theme"));
        QVERIFY(!m_manager->requestedEnabled());
        const auto example = QFINDTESTDATA("../examples/extensions/theme-qml/Wallpaper.qml");
        QVERIFY(!example.isEmpty());
        publishTheme(QFileInfo(example).absolutePath());
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        QCOMPARE(window()->property("themeStatus").toString(), QString("safe-mode"));
        QVERIFY(!themeView()->ready());
        QVERIFY(window()->findChild<QObject *>("wallpaperLyrics"));
        m_manager.reset();
        qputenv("NEKOTUNE_SAFE_MODE", "0");
        setup();
        QTRY_COMPARE(m_manager->runningCount(), 1);
        publishTheme(QFileInfo(example).absolutePath(), "Wallpaper.qml", 2);
        QTRY_COMPARE(window()->property("themeStatus").toString(), QString("active"));
        QFile settings(nekotune::AppPaths::configFile("settings.json"));
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("broken json");
        settings.close();
        QVERIFY(!m_manager->setOption("style", "classic"));
        QCOMPARE(m_manager->settings().value("style").toString(), QString("theme"));
        QCOMPARE(window()->property("themeStatus").toString(), QString("active"));
    }
    void themeWallpaperSourceCanDifferFromTheme() {
        qputenv("NEKOTUNE_SAFE_MODE", "0");
        setup();
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QTRY_VERIFY(m_controllers->extensions->appearanceReady());
        const auto example = QFINDTESTDATA("../examples/extensions/theme-qml/Wallpaper.qml");
        QVERIFY(!example.isEmpty());
        // The wallpaper package is available, but a built-in palette is the current theme.
        auto publish = [&](const QString &state, bool trusted) {
            send({{"event", "extensions.changed"},
                  {"selections", QJsonObject{{"theme", "builtin/ocean"}}},
                  {"extensions", QJsonArray{QJsonObject{
                      {"id", "test.wallpaper"}, {"type", "theme"}, {"state", state},
                      {"directory", QFileInfo(example).absolutePath()}, {"generation", 1}, {"trusted", trusted},
                      {"themes", QJsonArray{QJsonObject{
                          {"id", "test.wallpaper/default"}, {"schemaVersion", 1},
                          {"variants", QJsonObject{{"light", QJsonObject{}}, {"dark", QJsonObject{}}}},
                          {"ui", QJsonObject{{"wallpaper", "Wallpaper.qml"}}}}}}}}}});
        };
        publish("running", true);
        QTRY_VERIFY(m_controllers->theme->wallpaperSlots().contains("test.wallpaper/default"));
        QVERIFY(!m_controllers->theme->uiSlots().contains("wallpaper"));

        QVERIFY(m_manager->setOption("style", "theme"));
        QVERIFY(m_manager->setOption("themeWallpaper", "test.wallpaper/default"));
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        QTRY_COMPARE(window()->property("themeStatus").toString(), QString("active"));

        QCOMPARE(m_controllers->theme->activeId(), QString("builtin/ocean"));
        QPointer<QQuickWindow> first = window();
        QPointer<QQuickItem> item = themeView()->item();
        publish("disabled", true);
        QTRY_COMPARE(window()->property("themeStatus").toString(), QString("source-missing"));
        QTRY_VERIFY(item.isNull());
        QVERIFY(window()->findChild<QObject *>("wallpaperLyrics"));
        QCOMPARE(m_manager->settings().value("themeWallpaper").toString(), QString("test.wallpaper/default"));
        publish("running", false);
        QTRY_VERIFY(!m_controllers->theme->items().last().toMap().value("trusted").toBool());
        QVERIFY(m_controllers->theme->wallpaperSlots().isEmpty());
        QVERIFY(!themeView()->ready());
        publish("running", true);
        QTRY_COMPARE(window()->property("themeStatus").toString(), QString("active"));
        QCOMPARE(window(), first.data());

        // Following and explicitly choosing the same source must keep its QML instance.
        publishTheme(QFileInfo(example).absolutePath());
        QTRY_VERIFY(m_controllers->theme->uiSlots().contains("wallpaper"));
        QCOMPARE(m_controllers->theme->uiSlots().value("wallpaper"),
                 m_controllers->theme->wallpaperSlots().value("test.wallpaper/default"));
        item = themeView()->item();
        QVERIFY(m_manager->setOption("themeWallpaper", ""));
        QTest::qWait(50);
        QVERIFY(item);
        QCOMPARE(themeView()->item(), item.data());
        QVERIFY(m_manager->setOption("themeWallpaper", "test.wallpaper/default"));
        QTest::qWait(50);
        QVERIFY(item);
        QCOMPARE(themeView()->item(), item.data());
        publish("running", true);
        QTRY_COMPARE(m_controllers->theme->activeId(), QString("builtin/ocean"));

        QVERIFY(m_manager->setOption("themeWallpaper", "test.wallpaper/missing"));
        QTRY_COMPARE(window()->property("themeStatus").toString(), QString("source-missing"));
        QVERIFY(m_manager->setOption("themeWallpaper", ""));
        QTRY_COMPARE(window()->property("themeStatus").toString(), QString("missing"));

        QVERIFY(!m_manager->setOption("themeWallpaper", 1));
        QVERIFY(!m_manager->setOption("themeWallpaper", QString(201, QLatin1Char('x'))));

        // The source is persisted, but safe mode never publishes or creates its QML.
        QVERIFY(m_manager->setOption("themeWallpaper", "test.wallpaper/default"));
        QVERIFY(m_manager->setOption("autoRestore", true));
        m_manager.reset();
        qputenv("NEKOTUNE_SAFE_MODE", "1");
        setup();
        QCOMPARE(m_manager->settings().value("themeWallpaper").toString(), QString("test.wallpaper/default"));
        QVERIFY(!m_manager->requestedEnabled());
        publish("running", true);
        QTRY_VERIFY(m_controllers->theme->wallpaperSlots().isEmpty());
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        QTRY_COMPARE(window()->property("themeStatus").toString(), QString("safe-mode"));
        QVERIFY(!themeView()->ready());
        QVERIFY(window()->findChild<QObject *>("wallpaperLyrics"));
    }
    void themeWallpaperContextTracksPlaybackLyricsAndConnection() {
        qputenv("NEKOTUNE_SAFE_MODE", "0");
        setup();
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QTRY_VERIFY(m_controllers->extensions->appearanceReady());
        const auto example = QFINDTESTDATA("../examples/extensions/theme-qml/Wallpaper.qml");
        QVERIFY(!example.isEmpty());
        const auto directory = QFileInfo(example).absolutePath();
        publishTheme(directory);
        QVERIFY(m_manager->setOption("style", "theme"));
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        QTRY_COMPARE(window()->property("themeStatus").toString(), QString("active"));
        const auto context = themeView()->wallpaperContext;
        auto *item = themeView()->item();
        auto *lyrics = item->findChild<QObject *>("themeWallpaperLyrics");
        QVERIFY(lyrics);
        QTRY_COMPARE(lyrics->property("activeIndex").toInt(), 0);
        QVERIFY(item->property("motionAllowed").toBool());
        send({{"event", "player.seeked"}, {"position", 7000}, {"playback_rate", 2.0}});
        QTRY_COMPARE(lyrics->property("activeIndex").toInt(), 1);
        QCOMPARE(context->property("playbackRate").toDouble(), 2.0);
        QCOMPARE(lyrics->property("playbackRate").toDouble(), 2.0);
        send({{"event", "player.state_changed"}, {"state", "paused"}});
        QTRY_VERIFY(!context->property("playing").toBool());
        QVERIFY(!item->property("motionAllowed").toBool());
        send({{"event", "player.state_changed"}, {"state", "playing"}});
        QTRY_VERIFY(item->property("motionAllowed").toBool());
        QVERIFY(m_manager->setOption("intensity", 0.0));
        QVERIFY(!item->property("motionAllowed").toBool());
        QVERIFY(m_manager->setOption("intensity", 0.5));
        QVERIFY(m_manager->setOption("updateHz", 60));
        QCOMPARE(lyrics->property("updateInterval").toInt(), 16);
        QVERIFY(m_manager->setOption("nextLine", false));
        QVERIFY(!lyrics->property("showNext").toBool());
        QVERIFY(m_manager->setOption("lyrics", false));
        QVERIFY(!lyrics->property("running").toBool());
        QVERIFY(m_manager->setOption("lyrics", true));
        window()->hide();
        QVERIFY(!context->property("updating").toBool());
        QVERIFY(!item->property("motionAllowed").toBool());
        QVERIFY(!lyrics->property("running").toBool());
        window()->show();
        QTRY_VERIFY(context->property("updating").toBool());
        send({{"event", "player.track_changed"},
              {"song", QJsonObject{{"resource_key", "track-b"}, {"title", "B"}}}});
        QTRY_COMPARE(lyrics->property("songIdentity").toString(), QString("track-b"));
        QCOMPARE(lyrics->property("activeIndex").toInt(), -1);
        send({{"event", "lyrics.changed"}, {"lyrics", snapshot().value("data").toObject().value("lyrics")}});
        QTest::qWait(30);
        QCOMPARE(lyrics->property("activeIndex").toInt(), -1);
        QPointer<QQuickItem> previous = item;
        const auto nativeId = window()->winId();
        m_peer->disconnectFromServer();
        QTRY_VERIFY(!m_manager->dataReady());
        QVERIFY(!context->property("connected").toBool());
        QVERIFY(!context->property("updating").toBool());
        QTRY_VERIFY(previous.isNull());
        QTRY_VERIFY(m_client->connected());
        QVERIFY(!m_manager->dataReady());
        send(snapshot("track-b"));
        QTRY_VERIFY(m_manager->dataReady());
        QTRY_VERIFY(m_controllers->extensions->appearanceReady());
        publishTheme(directory, "Wallpaper.qml", 2);
        QTRY_COMPARE(window()->property("themeStatus").toString(), QString("active"));
        lyrics = themeView()->item()->findChild<QObject *>("themeWallpaperLyrics");
        QVERIFY(lyrics);
        QCOMPARE(lyrics->property("activeIndex").toInt(), 0);
        QCOMPARE(window()->winId(), nativeId);
        QVERIFY(!m_requests.contains("player.status"));
    }
    void vinylAnimationPausesAndStopsWhenInactive() {
        setup();
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QVERIFY(m_manager->setOption("style", "vinyl"));
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        auto *record = window()->findChild<QObject *>("wallpaperRecord");
        auto *animation = window()->findChild<QObject *>("wallpaperRecordAnimation");
        QVERIFY(record && animation);
        QTRY_VERIFY(record->property("rotation").toDouble() > 0);
        send({{"event", "player.state_changed"}, {"state", "paused"}});
        QTRY_VERIFY(animation->property("paused").toBool());
        const auto angle = record->property("rotation");
        QTest::qWait(100);
        QCOMPARE(record->property("rotation"), angle);
        send({{"event", "player.state_changed"}, {"state", "playing"}});
        QTRY_VERIFY(!animation->property("paused").toBool());
        QTRY_VERIFY(record->property("rotation").toDouble() > angle.toDouble());
        QVERIFY(m_manager->setOption("intensity", 0.0));
        QVERIFY(!animation->property("running").toBool());
        QVERIFY(m_manager->setOption("intensity", 0.5));
        QTRY_VERIFY(animation->property("running").toBool());
        qputenv("NEKOTUNE_SAFE_MODE", "0");
        // A theme cannot reduce motion on the user's behalf.
        send({
            {"event", "extensions.changed"},
            {"selections", QJsonObject{{"theme", "test/still"}}},
            {"extensions", QJsonArray{QJsonObject{
                {"id", "test"}, {"state", "running"},
                {"themes", QJsonArray{QJsonObject{
                    {"id", "test/still"},
                    {"tokens", QJsonObject{{"reducedMotion", true}}}}}}}}}});
        QTRY_COMPARE(m_controllers->theme->activeId(), QString("test/still"));
        QVERIFY(!m_controllers->theme->tokens().value("reducedMotion").toBool());
        QVERIFY(animation->property("running").toBool());
        // The user preference does.
        m_controllers->theme->setReducedMotion(true);
        QTRY_VERIFY(!animation->property("running").toBool());
        QVERIFY(!window()->findChild<QObject *>("lyricsFrameTimer")->property("running").toBool());
        m_controllers->theme->setReducedMotion(false);
        QTRY_VERIFY(animation->property("running").toBool());
        send({{"event", "extensions.changed"}});
        window()->hide();
        QVERIFY(!animation->property("running").toBool());
        window()->show();
        QTRY_VERIFY(animation->property("running").toBool());
        m_peer->disconnectFromServer();
        QTRY_VERIFY(!animation->property("running").toBool());
    }
    void newStylesFitLandscapeAndPortrait_data() {
        QTest::addColumn<QString>("style");
        QTest::addColumn<QSize>("size");
        for (const auto &style : {"vinyl", "lyrics"}) {
            QTest::newRow(qPrintable(QString(style) + "-landscape")) << QString(style) << QSize(800, 600);
            QTest::newRow(qPrintable(QString(style) + "-portrait")) << QString(style) << QSize(720, 1280);
        }
    }
    void newStylesFitLandscapeAndPortrait() {
        QFETCH(QString, style);
        QFETCH(QSize, size);
        setup();
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QVERIFY(m_manager->setOption("style", style));
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        window()->resize(size);
        QTest::qWait(100); // Polish the actual layouts after changing orientation.
        for (const auto &name : {"wallpaperTitle", "wallpaperLyrics"}) {
            auto *item = window()->findChild<QQuickItem *>(name);
            QVERIFY(item && item->isVisible());
            const auto bounds = item->mapRectToScene(item->boundingRect());
            QVERIFY2(bounds.width() > 100 && bounds.height() > 0, name);
            QVERIFY2(bounds.left() >= 0 && bounds.top() >= 0 &&
                         bounds.right() <= size.width() && bounds.bottom() <= size.height(), name);
        }
        const auto directory = qEnvironmentVariable("NEKOTUNE_TEST_SCREENSHOT_DIR");
        if (!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(window()->grabWindow().save(directory + "/layout-" + QTest::currentDataTag() + ".png"));
        }
    }
    void externalWallpaper_data() {
        QTest::addColumn<QSize>("size");
        QTest::newRow("reference") << QSize(1296, 734);
        QTest::newRow("desktop") << QSize(1920, 1080);
        QTest::newRow("portrait") << QSize(720, 1280);
        QTest::newRow("small") << QSize(800, 600);
    }
    void externalWallpaper() {
        const auto directory = qEnvironmentVariable("NEKOTUNE_TEST_WALLPAPER_THEME_DIR");
        if (directory.isEmpty()) QSKIP("Set NEKOTUNE_TEST_WALLPAPER_THEME_DIR to test an external theme");
        QFETCH(QSize, size);
        qunsetenv("NEKOTUNE_SAFE_MODE");
        const bool native = qEnvironmentVariable("NEKOTUNE_TEST_NATIVE_WALLPAPER") == "1";
        setup(native);
        auto data = snapshot();
        auto payload = data["data"].toObject();
        payload["position"] = 45000; payload["duration"] = 163000;
        auto song = payload["song"].toObject();
        song["title"] = "T氏の話を信じるな"; song["artist"] = "ピノキオピー feat. 初音ミク・重音テト";
        song["cover_url"] = "qrc:/artwork/default-cover.png"; payload["song"] = song;
        payload["lyrics"] = QJsonObject{{"track_id", "track-a"}, {"state", "ready"},
            {"document", QJsonObject{{"lines", QJsonArray{
                QJsonObject{{"time_ms", 0}, {"text", "信じなきゃ狂っちゃうわ"}},
                QJsonObject{{"time_ms", 40000}, {"text", "T氏の話を信じるな"}},
                QJsonObject{{"time_ms", 80000}, {"text", "信じなきゃ狂っちゃうわ"}}}}}}};
        data["data"] = payload; send(data);
        QTRY_VERIFY(m_manager->dataReady());
        QTRY_VERIFY(m_controllers->extensions->appearanceReady());
        publishTheme(directory);
        QVERIFY(m_manager->setOption("style", "theme"));
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE_WITH_TIMEOUT(m_manager->runningCount(), 1, 10000);
        if (!native) window()->resize(size);
        QTRY_COMPARE_WITH_TIMEOUT(window()->property("themeStatus").toString(), QString("active"), 5000);
        QTest::qWait(100);
        QVERIFY(themeView());
        QPointer<QQuickItem> item = themeView()->item(); QVERIFY(item);
        for (const auto &name : {"lumenMusicCard", "lumenLyrics", "lumenTrackInfo", "lumenSpectrum"}) {
            auto *child = item->findChild<QQuickItem *>(name); QVERIFY2(child, name);
            const auto bounds = child->mapRectToScene(child->boundingRect());
            QVERIFY2(bounds.width() > 0 && bounds.height() > 0 && bounds.left() >= 0 && bounds.top() >= 0
                && bounds.right() <= window()->width() && bounds.bottom() <= window()->height(), name);
        }
        auto *spectrum = item->findChild<QObject *>("lumenSpectrumController"); QVERIFY(spectrum);
        QTRY_VERIFY(!spectrum->property("token").toString().isEmpty());
        QJsonArray bands;
        for (int i = 0; i < 48; ++i) bands.append(std::max(0., (.5 + .5 * std::sin(i * 1.8)) * std::sin(i * 3.14159 / 48)));
        send({{"event", "extension.nekotune.spectrum.frame"}, {"data", QJsonObject{
            {"version", 1}, {"generation", 1}, {"sequence", 1}, {"bands", bands}}}});
        QTRY_VERIFY(spectrum->property("lastFrame").toDouble() > 0);
        QTest::qWait(100);
        const auto picture = window()->grabWindow(); QVERIFY(!picture.isNull());
        QCOMPARE(picture.pixelColor(0, 0).alpha(), 0);
        const auto output = qEnvironmentVariable("NEKOTUNE_TEST_SCREENSHOT_DIR");
        if (!output.isEmpty()) {
            QVERIFY(QDir().mkpath(output)); QVERIFY(picture.save(output + "/lumen-alpha-" + QTest::currentDataTag() + ".png"));
            // A test-only image behind the transparent surface. Never packaged in the theme.
            QQmlComponent backdrop(m_engine.get());
            backdrop.setData(R"(import QtQuick; Image { anchors.fill: parent; z: -100; source: "qrc:/artwork/default-cover.png"; fillMode: Image.PreserveAspectCrop })", QUrl());
            auto *background = qobject_cast<QQuickItem *>(backdrop.create()); QVERIFY(background);
            background->setParentItem(window()->contentItem());
            QTest::qWait(80);
            QVERIFY(window()->grabWindow().save(output + "/lumen-" + QTest::currentDataTag() + ".png"));
            delete background;
        }
        if (native) QTest::qWait(std::clamp(qEnvironmentVariableIntValue("NEKOTUNE_TEST_PREVIEW_MS"), 0, 10000));
        QVERIFY(m_manager->setOption("enabled", false)); QVERIFY(!window());
    }
    void nativeWallpaper() {
        if (qEnvironmentVariable("NEKOTUNE_TEST_NATIVE_WALLPAPER") != "1")
            QSKIP("Explicit native desktop acceptance only");
        setup(true);
        QVERIFY2(m_manager->supported(), qPrintable(m_manager->unavailableReason()));
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QQuickWindow ordinary;
        ordinary.setTitle("NekoTune ordinary window isolation test");
        ordinary.resize(400, 260);
        ordinary.show();
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE_WITH_TIMEOUT(m_manager->runningCount(), 1, 10000);
        QVERIFY(ordinary.isVisible());
        QVERIFY(!window()->isActive());
        const auto directory = qEnvironmentVariable("NEKOTUNE_TEST_SCREENSHOT_DIR");
        QTest::qWait(1000);
        const auto picture = window()->grabWindow();
        QVERIFY(!picture.isNull());
        QCOMPARE(picture.pixelColor(0, 0).alpha(), m_manager->transparentOverlaySupported() ? 0 : 255);
        if (!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));
            QVERIFY(picture.save(directory + "/native-wallpaper.png"));
        }
        auto song = m_controllers->playback->song();
        song["cover_url"] = "qrc:/artwork/default-cover.png";
        m_controllers->playback->apply({{"song", QJsonObject::fromVariantMap(song)}});
        QVERIFY(m_manager->setOption("coverBackground", true));
        QVERIFY(m_manager->setOption("blur", true));
        QTRY_VERIFY(window()->findChild<QObject *>("wallpaperBlur"));
        auto *blur = window()->findChild<QObject *>("wallpaperBlur");
        QTRY_VERIFY(blur->property("active").toBool());
        QTest::qWait(500);
        if (!directory.isEmpty())
            QVERIFY(window()->grabWindow().save(directory + "/native-wallpaper-blur.png"));
        QVERIFY(m_manager->setOption("blur", false));
        QVERIFY(!blur->property("active").toBool());
        QVERIFY(!blur->property("item").value<QObject *>());
        QVERIFY(m_manager->setOption("coverBackground", false));
        QTRY_VERIFY(!window()->findChild<QObject *>("wallpaperBlur"));
        const auto nativeId = window()->winId();
        for (const auto &style : {"vinyl", "lyrics", "classic"}) {
            QVERIFY(m_manager->setOption("style", style));
            QTest::qWait(350);
            QCOMPARE(window()->winId(), nativeId);
            QVERIFY(!window()->isActive());
            QVERIFY(window()->findChild<QObject *>("wallpaperLyrics"));
            if (!directory.isEmpty())
                QVERIFY(window()->grabWindow().save(directory + "/native-wallpaper-" + style + ".png"));
        }
        qputenv("NEKOTUNE_SAFE_MODE", "0");
        const auto example = QFINDTESTDATA("../examples/extensions/theme-qml/Wallpaper.qml");
        QVERIFY(!example.isEmpty());
        const auto themeDirectory = QFileInfo(example).absolutePath();
        publishTheme(themeDirectory);
        QVERIFY(m_manager->setOption("style", "theme"));
        QTRY_COMPARE(window()->property("themeStatus").toString(), QString("active"));
        QCOMPARE(window()->winId(), nativeId);
        QPointer<QQuickItem> previous = themeView()->item();
        publishTheme(themeDirectory, "Wallpaper.qml", 2);
        QTRY_VERIFY(previous.isNull());
        QTRY_VERIFY(themeView()->ready());
        QCOMPARE(window()->winId(), nativeId);
        QVERIFY(!window()->isActive());
        QTest::qWait(350);
        if (!directory.isEmpty())
            QVERIFY(window()->grabWindow().save(directory + "/native-wallpaper-theme.png"));
        QPointer<QQuickWindow> first = window();
        QVERIFY(m_manager->setOption("enabled", false));
        QVERIFY(first.isNull());
        QVERIFY(ordinary.isVisible());
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QQuickWindow::setDefaultAlphaBuffer(true);
    WallpaperTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "wallpaper_test.moc"
