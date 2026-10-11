#include "support/media_audio.h"
#include "support/store_fixture.h"
#include "app_paths.h"
#include "i18n.h"
#include "platform/system_media_bridge.h"
#include "platform/system_tray_controller.h"
#include "runtime/backend_runtime.h"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QMenu>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QtTest>

using namespace nekotune;
class SystemTrayTest final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<BackendRuntime> m_runtime;
    std::unique_ptr<IpcClient> m_client;
    std::unique_ptr<PlaybackController> m_playback;
    std::unique_ptr<SystemMediaBridge> m_bridge;
    std::unique_ptr<I18n> m_translator;
    std::unique_ptr<SystemTrayController> m_tray;
    std::unique_ptr<QWindow> m_window;
    MediaAudio *m_audio = nullptr;
    bool m_available = true;
    QAction *action(const char *name) {
        auto *icon = m_tray->findChild<QSystemTrayIcon *>();
        return icon->contextMenu()->findChild<QAction *>(QString::fromLatin1(name));
    }
    void startBackend() {
        {
            StoreFixture store;
            auto song = store.library.importFile({m_dir->filePath("song.wav"), "tray-song", "Test", 30000},
                                                 "Cat & Moon", "测试艺人");
            QVERIFY(song);
            QVERIFY(store.saveQueue({{{m_dir->filePath("song.wav"), song.value().id},
                                      {m_dir->filePath("song.wav"), song.value().id}}, 0}));
        }
        m_runtime = std::make_unique<BackendRuntime>([this] {
            auto audio = std::make_unique<MediaAudio>(); m_audio = audio.get(); return audio;
        });
        QVERIFY2(m_runtime->start(), qPrintable(m_runtime->errorString()));
        QTRY_VERIFY(m_bridge->ready());
    }
  private slots:
    void init() {
        QApplication::setQuitOnLastWindowClosed(false);
        m_available = true;
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        qputenv("NEKOTUNE_HOME", m_dir->filePath("home").toUtf8());
        qputenv("NEKOTUNE_DB_PATH", m_dir->filePath("media.sqlite3").toUtf8());
        qputenv("NEKOTUNE_SOCKET", m_dir->filePath("backend.sock").toUtf8());
        qputenv("NEKOTUNE_SAFE_MODE", "1");
        qputenv("NEKOTUNE_LANGUAGE", "zh");
        QVERIFY(AppPaths::prepare());
        QVERIFY(AppPaths::saveSetting("lyrics_offline", true));
        m_client = std::make_unique<IpcClient>();
        m_playback = std::make_unique<PlaybackController>(*m_client);
        m_bridge = std::make_unique<SystemMediaBridge>(*m_client, *m_playback);
        m_translator = std::make_unique<I18n>();
        m_tray = std::make_unique<SystemTrayController>(*m_bridge, *m_translator, nullptr,
                                                       [this] { return m_available; });
        m_window = std::make_unique<QWindow>();
        m_window->show();
        m_tray->attachWindow(m_window.get());
    }
    void cleanup() {
        m_tray.reset(); m_window.reset(); m_bridge.reset(); m_playback.reset(); m_client.reset();
        m_runtime.reset(); m_translator.reset(); m_dir.reset();
    }
    void closeHidesAndActivationRestores() {
        QSignalSpy quits(m_tray.get(), &SystemTrayController::quitRequested);
        QVERIFY(m_tray->closeToTray());
        m_window->close();
        QVERIFY(!m_window->isVisible());
        QCOMPARE(quits.count(), 0);
        QCOMPARE(action("trayToggleWindow")->text(), QStringLiteral("显示窗口"));
        auto *icon = m_tray->findChild<QSystemTrayIcon *>();
        QCOMPARE(icon->icon().availableSizes().size(), 7);
        emit icon->activated(QSystemTrayIcon::Context);
        QVERIFY(!m_window->isVisible());
        emit icon->activated(QSystemTrayIcon::Trigger);
        QVERIFY(m_window->isVisible());
        action("trayToggleWindow")->trigger();
        QVERIFY(!m_window->isVisible());
        emit icon->activated(QSystemTrayIcon::DoubleClick);
        QVERIFY(m_window->isVisible());
    }
    void restoresMaximizedAndMinimizedWindows() {
        m_window->setWindowState(Qt::WindowMaximized);
        m_window->close();
        m_tray->showWindow();
        QCOMPARE(m_window->windowState(), Qt::WindowMaximized);
        m_window->setWindowState(Qt::WindowMinimized);
        action("trayToggleWindow")->trigger();
        QCOMPARE(m_window->windowState(), Qt::WindowMaximized);
        m_window->setWindowState(Qt::WindowNoState);
        m_window->setWindowState(Qt::WindowMinimized);
        m_window->close();
        m_tray->showWindow();
        QCOMPARE(m_window->windowState(), Qt::WindowNoState);
        m_window->setWindowState(Qt::WindowFullScreen);
        m_window->close();
        m_tray->showWindow();
        QCOMPARE(m_window->windowState(), Qt::WindowFullScreen);
    }
    void disabledPreferenceAndMissingTrayQuit_data() {
        QTest::addColumn<bool>("available"); QTest::addColumn<bool>("closeToTray");
        QTest::newRow("preference-disabled") << true << false;
        QTest::newRow("no-tray") << false << true;
    }
    void disabledPreferenceAndMissingTrayQuit() {
        QFETCH(bool, available); QFETCH(bool, closeToTray);
        m_available = available; m_tray->setCloseToTray(closeToTray);
        QSignalSpy quits(m_tray.get(), &SystemTrayController::quitRequested);
        m_window->close();
        QCOMPARE(quits.count(), 1);
        QVERIFY(!m_window->isVisible());
        m_tray->quit();
        QCOMPARE(quits.count(), 1);
    }
    void availabilityPollRestoresHiddenWindow() {
        QSignalSpy changes(m_tray.get(), &SystemTrayController::trayAvailableChanged);
        m_window->close();
        m_available = false;
        QTRY_VERIFY_WITH_TIMEOUT(m_window->isVisible(), 3500);
        QVERIFY(!m_tray->trayAvailable());
        QCOMPARE(changes.count(), 1);
        QVERIFY(!action("trayToggleWindow")->isEnabled());
        m_tray->toggleWindow();
        QVERIFY(m_window->isVisible());
        m_available = true;
        QVERIFY(QMetaObject::invokeMethod(m_tray.get(), "refreshAvailability"));
        QVERIFY(action("trayToggleWindow")->isEnabled());
        QCOMPARE(changes.count(), 2);
    }
    void preferenceRoundTripPreservesOtherSettings() {
        QSignalSpy changes(m_tray.get(), &SystemTrayController::closeToTrayChanged);
        QVERIFY(AppPaths::saveSetting("language", "en"));
        m_tray->setCloseToTray(false);
        m_tray->setCloseToTray(false);
        QCOMPARE(changes.count(), 1);
        QCOMPARE(AppPaths::setting("close_to_tray"), QJsonValue(false));
        QCOMPARE(AppPaths::setting("language"), QJsonValue(QStringLiteral("en")));
        SystemTrayController reloaded(*m_bridge, *m_translator);
        QVERIFY(!reloaded.closeToTray());
        QVERIFY(AppPaths::saveSetting("close_to_tray", "invalid"));
        SystemTrayController invalid(*m_bridge, *m_translator);
        QVERIFY(invalid.closeToTray());
    }
    void unavailableAtStartupCanRecover() {
        m_tray.reset();
        m_available = false;
        m_tray = std::make_unique<SystemTrayController>(*m_bridge, *m_translator, nullptr,
                                                       [this] { return m_available; });
        m_tray->attachWindow(m_window.get());
        QVERIFY(!m_tray->trayAvailable());
        QVERIFY(!action("trayToggleWindow")->isEnabled());
        m_tray->toggleWindow();
        QVERIFY(m_window->isVisible());
        m_available = true;
        QVERIFY(QMetaObject::invokeMethod(m_tray.get(), "refreshAvailability"));
        QVERIFY(m_tray->trayAvailable());
        action("trayToggleWindow")->trigger();
        QVERIFY(!m_window->isVisible());
        m_tray->showWindow();
        QVERIFY(m_window->isVisible());
    }
    void extensionWindowCloseDoesNotQuit() {
        QSignalSpy quits(m_tray.get(), &SystemTrayController::quitRequested);
        QWindow recovery;
        m_window->close();
        recovery.show(); recovery.close();
        QCOMPARE(quits.count(), 0);
        QVERIFY(!m_window->isVisible());
        m_tray->showWindow();
        QVERIFY(m_window->isVisible());
    }
    void trayAndMediaQuitShareLifecycle() {
        QSignalSpy quits(m_tray.get(), &SystemTrayController::quitRequested);
        connect(m_bridge.get(), &SystemMediaBridge::quitRequested, m_tray.get(), &SystemTrayController::quit);
        m_window->close();
        action("trayQuit")->trigger();
        QCOMPARE(quits.count(), 1);
        QVERIFY(!m_tray->findChild<QSystemTrayIcon *>()->isVisible());
        m_bridge->quit();
        QCOMPARE(quits.count(), 1);
    }
    void mediaRaiseRestoresHiddenWindow() {
        connect(m_bridge.get(), &SystemMediaBridge::raiseRequested, m_tray.get(), &SystemTrayController::showWindow);
        m_window->close(); m_bridge->raise();
        QVERIFY(m_window->isVisible());
    }
    void menuUsesConfirmedPlaybackAndCapabilities() {
        QVERIFY(!action("trayPlayPause")->isEnabled());
        QVERIFY(!action("trayNext")->isEnabled());
        QCOMPARE(action("trayTrack")->text(), QStringLiteral("未选择歌曲"));
        startBackend();
        QVERIFY(action("trayPlayPause")->isEnabled());
        QVERIFY(action("trayNext")->isEnabled());
        QVERIFY(!action("trayPrevious")->isEnabled());
        QCOMPARE(action("trayTrack")->text(), QStringLiteral("Cat && Moon — 测试艺人"));
        auto *icon = m_tray->findChild<QSystemTrayIcon *>();
        QVERIFY(icon->toolTip().contains("Cat & Moon"));
        action("trayPlayPause")->trigger();
        QVERIFY(m_playback->state() != "playing"); // No optimistic frontend update.
        QTRY_COMPARE(m_playback->state(), "playing");
        QCOMPARE(action("trayPlayPause")->text(), QStringLiteral("暂停"));
        m_window->close();
        QVERIFY(!m_window->isVisible());
        QCOMPARE(m_playback->state(), "playing");
        action("trayPlayPause")->trigger();
        QTRY_COMPARE(m_playback->state(), "paused");
        action("trayNext")->trigger();
        QTRY_COMPARE(m_playback->song().value("queue_id").toInt(), 2);
        QVERIFY(action("trayPrevious")->isEnabled());
        action("trayPrevious")->trigger();
        QTRY_COMPARE(m_playback->song().value("queue_id").toInt(), 1);
        m_translator->setLanguage("en");
        QCOMPARE(action("trayQuit")->text(), "Quit");
        QCOMPARE(action("trayPlayPause")->text(), "Pause");
        QMetaObject::invokeMethod(m_audio, [this] { m_audio->suspendAudioOutput(); }, Qt::BlockingQueuedConnection);
        QTRY_COMPARE(m_playback->state(), "paused");
        QVERIFY(!action("trayPlayPause")->isEnabled());
        m_runtime->stop();
        QTRY_VERIFY(!m_bridge->ready());
        QVERIFY(!action("trayNext")->isEnabled());
        QVERIFY(!action("trayPrevious")->isEnabled());
        QCOMPARE(action("trayTrack")->text(), "No song selected");
        QVERIFY(m_runtime->start());
        QTRY_VERIFY(m_bridge->ready());
        QVERIFY(action("trayPlayPause")->isEnabled());
        QCOMPARE(m_playback->song().value("queue_id").toInt(), 1);
    }
};
QTEST_MAIN(SystemTrayTest)
#include "system_tray_test.moc"
