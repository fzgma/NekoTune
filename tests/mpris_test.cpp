#include "support/media_audio.h"
#include "support/store_fixture.h"
#include "app_paths.h"
#include "platform/linux/mpris_service.h"
#include "runtime/backend_runtime.h"
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLocalServer>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

using namespace nekotune;
namespace {
const QString playerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString objectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString baseName = QStringLiteral("org.mpris.MediaPlayer2.NekoTune");
}
class MprisTest final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_directory;
    std::unique_ptr<BackendRuntime> m_runtime;
    std::unique_ptr<IpcClient> m_client;
    std::unique_ptr<PlaybackController> m_playback;
    std::unique_ptr<SystemMediaBridge> m_bridge;
    std::unique_ptr<MprisService> m_service;
    MediaAudio *m_audio = nullptr;
    QDBusConnection m_peer{"unconnected"};
    QList<QVariantMap> m_changes;
    QList<qint64> m_seeks;
    QDBusMessage call(const QString &interface, const QString &method, const QVariantList &args = {}) {
        auto message = QDBusMessage::createMethodCall(m_service->serviceName(), objectPath, interface, method);
        message.setArguments(args);
        QDBusPendingCallWatcher watcher(m_peer.asyncCall(message, 7000));
        QElapsedTimer timer; timer.start();
        while (!watcher.isFinished() && timer.elapsed() < 8000) QTest::qWait(1);
        return QDBusPendingReply<>(watcher).reply();
    }
    QDBusMessage control(const QString &method, const QVariantList &args = {}) { return call(playerInterface, method, args); }
    QVariant property(const QString &name, const QString &interface = playerInterface) {
        const auto reply = call(propertiesInterface, "Get", {interface, name});
        return reply.type() == QDBusMessage::ReplyMessage ? qvariant_cast<QDBusVariant>(reply.arguments().first()).variant()
                                                        : QVariant{};
    }
    QDBusMessage setProperty(const QString &name, const QVariant &value) {
        return call(propertiesInterface, "Set", {playerInterface, name, QVariant::fromValue(QDBusVariant(value))});
    }
    QString state() { return property("PlaybackStatus").toString(); }
    void inAudio(std::function<void(MediaAudio &)> operation) {
        QMetaObject::invokeMethod(m_audio, [this, operation] { operation(*m_audio); }, Qt::BlockingQueuedConnection);
    }
  private slots:
    void propertiesChanged(const QString &, const QVariantMap &changed, const QStringList &) { m_changes.append(changed); }
    void seeked(qint64 position) { m_seeks.append(position); }
    void init() {
        m_directory = std::make_unique<QTemporaryDir>();
        QVERIFY(m_directory->isValid());
        qputenv("NEKOTUNE_HOME", m_directory->filePath("music").toUtf8());
        qputenv("NEKOTUNE_DB_PATH", m_directory->filePath("media.sqlite3").toUtf8());
        qputenv("NEKOTUNE_SOCKET", m_directory->filePath("backend.sock").toUtf8());
        qputenv("NEKOTUNE_SAFE_MODE", "1");
        QVERIFY(AppPaths::prepare());
        QVERIFY(AppPaths::saveSetting("lyrics_offline", true));
        {
            StoreFixture store;
            const QString file = m_directory->filePath("song.wav");
            const auto song = store.library.importFile({file, "test-hash", "原始歌名", 30000}, "测试歌曲", "测试艺人");
            QVERIFY(song);
            QVERIFY(store.saveQueue({{{file, song.value().id}, {file, song.value().id}, {file, song.value().id}}, 0}));
        }
        m_runtime = std::make_unique<BackendRuntime>([this] {
            auto audio = std::make_unique<MediaAudio>(); m_audio = audio.get(); return audio;
        });
        QVERIFY2(m_runtime->start(), qPrintable(m_runtime->errorString()));
        m_client = std::make_unique<IpcClient>();
        m_playback = std::make_unique<PlaybackController>(*m_client);
        m_bridge = std::make_unique<SystemMediaBridge>(*m_client, *m_playback);
        m_service = std::make_unique<MprisService>(*m_bridge);
        QTRY_VERIFY(m_bridge->ready());
        if (QTest::currentTestFunction() == QByteArray("withoutSessionBus")) return;
        QTRY_COMPARE(m_service->serviceName(), baseName);
        m_peer = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("mpris-test-peer"));
        QVERIFY(m_peer.isConnected());
        QVERIFY(m_peer.connect({}, objectPath, propertiesInterface, "PropertiesChanged", this,
                               SLOT(propertiesChanged(QString,QVariantMap,QStringList))));
        QVERIFY(m_peer.connect({}, objectPath, playerInterface, "Seeked", this, SLOT(seeked(qint64))));
        m_changes.clear(); m_seeks.clear();
    }
    void cleanup() {
        m_service.reset(); m_bridge.reset(); m_playback.reset(); m_client.reset(); m_runtime.reset();
        QDBusConnection::disconnectFromBus(QStringLiteral("mpris-test-peer"));
        m_directory.reset(); m_audio = nullptr;
    }
    void metadataAndWireTypes() {
        const auto reply = call(propertiesInterface, "GetAll", {playerInterface});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.signature(), "a{sv}");
        const auto values = qdbus_cast<QVariantMap>(reply.arguments().first());
        QCOMPARE(values.value("Position").metaType(), QMetaType::fromType<qlonglong>());
        QCOMPARE(values.value("Volume").metaType(), QMetaType::fromType<double>());
        const auto metadata = qdbus_cast<QVariantMap>(values.value("Metadata"));
        QCOMPARE(qvariant_cast<QDBusObjectPath>(metadata.value("mpris:trackid")).path(), m_bridge->trackId());
        QCOMPARE(metadata.value("mpris:length").toLongLong(), qint64(30000000));
        QCOMPARE(metadata.value("mpris:length").metaType(), QMetaType::fromType<qlonglong>());
        QCOMPARE(metadata.value("xesam:title").toString(), "测试歌曲");
        QCOMPARE(qdbus_cast<QStringList>(metadata.value("xesam:artist")), QStringList{"测试艺人"});
        QVERIFY(!values.contains("LoopStatus") && !values.contains("Shuffle"));
        QCOMPARE(property("Rate").toDouble(), 1.0);
        QCOMPARE(qdbus_cast<QStringList>(property("SupportedUriSchemes", "org.mpris.MediaPlayer2")), QStringList{});
        const auto xml = call("org.freedesktop.DBus.Introspectable", "Introspect");
        QCOMPARE(xml.type(), QDBusMessage::ReplyMessage);
        QVERIFY(xml.arguments().first().toString().contains("org.mpris.MediaPlayer2.Player"));
        auto song = m_playback->song(); song.insert("album", "专辑"); song.insert("cover_url", "file:///tmp/test-cover.png");
        m_playback->apply({{"song", QJsonObject::fromVariantMap(song)}});
        QTRY_VERIFY(!m_changes.isEmpty());
        const auto art = qdbus_cast<QVariantMap>(property("Metadata"));
        QCOMPARE(art.value("mpris:artUrl").toString(), "file:///tmp/test-cover.png");
        QCOMPARE(art.value("xesam:album").toString(), "专辑");
    }
    void playbackRateIsReadOnlyAndFollowsBackend() {
        QCOMPARE(property("MinimumRate").toDouble(), .5);
        QCOMPARE(property("MaximumRate").toDouble(), 2.0);
        m_playback->setPlaybackRate(1.5);
        QTRY_COMPARE(property("Rate").toDouble(), 1.5);
        QTRY_VERIFY(std::any_of(m_changes.cbegin(), m_changes.cend(), [](const auto &change) {
            return change.value("Rate").toDouble() == 1.5;
        }));
        QCOMPARE(setProperty("Rate", 1.0).errorName(), "org.freedesktop.DBus.Error.PropertyReadOnly");
        QCOMPARE(property("Rate").toDouble(), 1.5);
        const auto xml = call("org.freedesktop.DBus.Introspectable", "Introspect");
        QVERIFY(xml.arguments().first().toString().contains("name=\"Rate\" type=\"d\" access=\"read\""));
    }
    void playbackNavigationSeekAndVolume() {
        QCOMPARE(control("Play").type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(state(), "Playing");
        QCOMPARE(control("Pause").type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(state(), "Paused");
        const auto first = m_bridge->trackId();
        QCOMPARE(control("Next").type(), QDBusMessage::ReplyMessage);
        QTRY_VERIFY(m_bridge->trackId() != first);
        QCOMPARE(state(), "Paused");
        QCOMPARE(control("Previous").type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(m_bridge->trackId(), first);
        QCOMPARE(control("SetPosition", {QVariant::fromValue(QDBusObjectPath(first)), qint64(5000000)}).type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(property("Position").toLongLong(), qint64(5000000));
        QCOMPARE(control("Seek", {qint64(-2000000)}).type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(property("Position").toLongLong(), qint64(3000000));
        QCOMPARE(setProperty("Volume", -2.0).type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(property("Volume").toDouble(), 0.0);
        QCOMPARE(setProperty("Volume", 2.0).type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(property("Volume").toDouble(), 1.0);
        QCOMPARE(control("PlayPause").type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(state(), "Playing");
        QCOMPARE(setProperty("Rate", 0.0).errorName(), "org.freedesktop.DBus.Error.PropertyReadOnly");
        QCOMPARE(state(), "Playing");
        QCOMPARE(control("Pause").type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(state(), "Paused");
        QCOMPARE(control("Stop").type(), QDBusMessage::ReplyMessage);
        QCOMPARE(control("Next").type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(state(), "Stopped");
        QCOMPARE(control("Play").type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(property("Position").toLongLong(), qint64(0));
    }
    void stalePositionsAndErrors() {
        QCOMPARE(control("Play").type(), QDBusMessage::ReplyMessage);
        const auto oldTrack = m_bridge->trackId();
        QCOMPARE(control("Next").type(), QDBusMessage::ReplyMessage);
        QTRY_VERIFY(oldTrack != m_bridge->trackId());
        QCOMPARE(control("SetPosition", {QVariant::fromValue(QDBusObjectPath(oldTrack)), qint64(8000000)}).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(property("Position").toLongLong(), qint64(0));
        for (auto target : {qint64(-1), qint64(30000001)})
            QCOMPARE(control("SetPosition", {QVariant::fromValue(QDBusObjectPath(m_bridge->trackId())), target}).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(property("Position").toLongLong(), qint64(0));
        QCOMPARE(setProperty("Rate", 2.0).errorName(), "org.freedesktop.DBus.Error.PropertyReadOnly");
        QCOMPARE(setProperty("Volume", std::numeric_limits<double>::quiet_NaN()).errorName(), "org.freedesktop.DBus.Error.InvalidArgs");
        QCOMPARE(setProperty("Volume", 1).errorName(), "org.freedesktop.DBus.Error.InvalidArgs");
        QCOMPARE(setProperty("Position", qint64(0)).errorName(), "org.freedesktop.DBus.Error.PropertyReadOnly");
        QCOMPARE(control("OpenUri", {QStringLiteral("file:///tmp/a.wav")}).errorName(), "org.freedesktop.DBus.Error.NotSupported");
        QCOMPARE(control("Seek", {QStringLiteral("invalid")}).errorName(), "org.freedesktop.DBus.Error.InvalidArgs");
        QCOMPARE(call(propertiesInterface, "Get", {playerInterface, QStringLiteral("Shuffle")}).errorName(), "org.freedesktop.DBus.Error.UnknownProperty");
    }
    void progressSignalsAndDeferredSeekConfirmation() {
        QCOMPARE(control("Play").type(), QDBusMessage::ReplyMessage);
        QTest::qWait(30); m_changes.clear(); m_seeks.clear();
        inAudio([](MediaAudio &audio) { audio.clock = 2000; emit audio.positionChanged(audio.clock); audio.deferSeek = true; });
        QTRY_COMPARE(m_playback->position(), 2000);
        QTest::qWait(30);
        QCOMPARE(m_changes.size(), 0);
        QCOMPARE(m_seeks.size(), 0);
        QCOMPARE(control("Seek", {qint64(3000000)}).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(m_seeks.size(), 0);
        inAudio([](MediaAudio &audio) { audio.confirmSeek(); });
        QTRY_COMPARE(m_seeks, QList<qint64>{5000000});
        for (const auto &change : m_changes) QVERIFY(!change.contains("Position"));
        QCOMPARE(control("Seek", {qint64(3000000)}).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(control("Next").type(), QDBusMessage::ReplyMessage);
        inAudio([](MediaAudio &audio) { audio.confirmSeek(); });
        QTest::qWait(30);
        QCOMPARE(m_seeks.size(), 1);
    }
    void unavailableOutputAndMetadataClear() {
        QCOMPARE(control("Play").type(), QDBusMessage::ReplyMessage);
        inAudio([](MediaAudio &audio) { audio.suspendAudioOutput(); });
        QTRY_VERIFY(!property("CanPlay").toBool());
        QCOMPARE(state(), "Paused");
        QCOMPARE(control("Play").type(), QDBusMessage::ReplyMessage);
        QCOMPARE(state(), "Paused");
        QVERIFY(m_bridge->queueId() > 0);
        bool done = false;
        m_client->request("queue.clear", {}, [&](const auto &, const auto &error) { QVERIFY(error.isEmpty()); done = true; });
        QTRY_VERIFY(done);
        QTRY_VERIFY(qdbus_cast<QVariantMap>(property("Metadata")).isEmpty());
        QVERIFY(!property("CanPause").toBool());
    }
    void collisionAndCleanup() {
        m_service.reset();
        QVERIFY(m_peer.registerService(baseName));
        m_service = std::make_unique<MprisService>(*m_bridge);
        QCOMPARE(m_service->serviceName(), baseName + QStringLiteral(".instance%1").arg(QCoreApplication::applicationPid()));
        const auto name = m_service->serviceName();
        QCOMPARE(state(), "Stopped");
        m_service.reset();
        QVERIFY(!m_peer.interface()->isServiceRegistered(name).value());
        QVERIFY(m_peer.unregisterService(baseName));
    }
    void disconnectReconnectChangesTrackIdentity() {
        const auto first = m_bridge->trackId();
        m_runtime->stop();
        QTRY_VERIFY(!m_bridge->ready());
        QVERIFY(m_service->serviceName().isEmpty());
        QVERIFY(!m_peer.interface()->isServiceRegistered(baseName).value());
        QVERIFY(m_runtime->start());
        QTRY_VERIFY(m_bridge->ready());
        QTRY_COMPARE(m_service->serviceName(), baseName);
        QVERIFY(m_bridge->trackId() != first);
        QCOMPARE(control("Play").type(), QDBusMessage::ReplyMessage);
        QCOMPARE(control("SetPosition", {QVariant::fromValue(QDBusObjectPath(first)), qint64(5000000)}).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(property("Position").toLongLong(), qint64(0));
    }
    void raiseAndQuitRequests() {
        QSignalSpy raise(m_bridge.get(), &SystemMediaBridge::raiseRequested);
        QSignalSpy quit(m_bridge.get(), &SystemMediaBridge::quitRequested);
        QCOMPARE(call("org.mpris.MediaPlayer2", "Raise").type(), QDBusMessage::ReplyMessage);
        QCOMPARE(raise.count(), 1);
        QCOMPARE(call("org.mpris.MediaPlayer2", "Quit").type(), QDBusMessage::ReplyMessage);
        QTRY_COMPARE(quit.count(), 1);
        m_bridge->shutdown();
        QVERIFY(m_service->serviceName().isEmpty());
    }
    void commandTimeoutAndLateReply() {
        m_service.reset();
        QLocalServer server;
        const auto socketPath = m_directory->filePath("mock.sock");
        QVERIFY(server.listen(socketPath));
        qputenv("NEKOTUNE_SOCKET", socketPath.toUtf8());
        IpcClient client;
        PlaybackController playback(client);
        SystemMediaBridge bridge(client, playback);
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QLocalSocket> socket(server.nextPendingConnection());
        const auto snapshot = QJsonObject{{"state", "paused"}, {"position", 0}, {"duration", 30000}, {"volume", .8},
                                         {"song", QJsonObject{{"queue_id", 1}}}, {"capabilities", QJsonObject{{"can_pause", true}}}};
        socket->write(QJsonDocument(QJsonObject{{"event", "server.connected"}, {"data", snapshot}}).toJson(QJsonDocument::Compact) + '\n');
        socket->flush(); QTRY_VERIFY(bridge.ready());
        MprisService service(bridge);
        auto message = QDBusMessage::createMethodCall(service.serviceName(), objectPath, playerInterface, "Pause");
        // Snapshot must be playing so Pause actually reaches the unresponsive mock backend.
        playback.apply({{"state", "playing"}});
        QDBusPendingCallWatcher watcher(m_peer.asyncCall(message, 7000));
        QTRY_VERIFY_WITH_TIMEOUT(watcher.isFinished(), 6500);
        const auto reply = QDBusPendingReply<>(watcher).reply();
        QCOMPARE(reply.errorName(), "org.freedesktop.DBus.Error.Timeout");
        QTRY_VERIFY(socket->bytesAvailable() > 0);
        const auto request = QJsonDocument::fromJson(socket->readLine()).object();
        socket->write(QJsonDocument(QJsonObject{{"id", request.value("id")}, {"status", "ok"}, {"data", snapshot}}).toJson(QJsonDocument::Compact) + '\n');
        socket->flush();
        QTest::qWait(30);
        QCOMPARE(playback.state(), "playing");
        // Timed-out response correlation is discarded; authoritative events still work.
        QCOMPARE(QDBusPendingReply<>(watcher).reply().errorName(), "org.freedesktop.DBus.Error.Timeout");
    }
    void backendFailureRetainsSelection() {
        QCOMPARE(control("Play").type(), QDBusMessage::ReplyMessage);
        QCOMPARE(control("Pause").type(), QDBusMessage::ReplyMessage);
        const auto track = m_bridge->trackId();
        {
            StoreFixture store;
            QSqlQuery query(store.db.database());
            QVERIFY(query.exec("CREATE TRIGGER mpris_fail_queue BEFORE INSERT ON queue_items "
                               "BEGIN SELECT RAISE(ABORT, 'MPRIS injected storage failure'); END"));
        }
        QCOMPARE(control("Next").errorName(), "org.freedesktop.DBus.Error.Failed");
        QCOMPARE(m_bridge->trackId(), track);
        QCOMPARE(state(), "Paused");
    }
    void pendingCommandDisconnectDoesNotReplay() {
        m_service.reset();
        QLocalServer server;
        const auto socketPath = m_directory->filePath("disconnect.sock");
        QVERIFY(server.listen(socketPath));
        qputenv("NEKOTUNE_SOCKET", socketPath.toUtf8());
        IpcClient client;
        PlaybackController playback(client);
        SystemMediaBridge bridge(client, playback);
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QLocalSocket> socket(server.nextPendingConnection());
        const auto snapshot = QJsonObject{{"state", "playing"}, {"position", 0}, {"duration", 30000}, {"volume", .8},
                                         {"song", QJsonObject{{"queue_id", 1}}}, {"capabilities", QJsonObject{{"can_pause", true}}}};
        auto welcome = [&](QLocalSocket &peer) {
            peer.write(QJsonDocument(QJsonObject{{"event", "server.connected"}, {"data", snapshot}}).toJson(QJsonDocument::Compact) + '\n');
            peer.flush();
        };
        welcome(*socket); QTRY_VERIFY(bridge.ready());
        MprisService service(bridge);
        const auto track = bridge.trackId();
        auto message = QDBusMessage::createMethodCall(service.serviceName(), objectPath, playerInterface, "Pause");
        QDBusPendingCallWatcher watcher(m_peer.asyncCall(message, 7000));
        QTRY_VERIFY(socket->bytesAvailable() > 0);
        socket->abort();
        QTRY_VERIFY(watcher.isFinished());
        QCOMPARE(QDBusPendingReply<>(watcher).reply().errorName(), "org.freedesktop.DBus.Error.Failed");
        QTRY_VERIFY(!bridge.ready());
        QVERIFY(service.serviceName().isEmpty());
        QTRY_VERIFY(server.hasPendingConnections());
        socket.reset(server.nextPendingConnection());
        welcome(*socket); QTRY_VERIFY(bridge.ready());
        QVERIFY(bridge.trackId() != track);
        QTest::qWait(50);
        QCOMPARE(socket->bytesAvailable(), qint64(0));
    }
    void ipcValidationAndLegacyNavigation() {
        auto ipcCall = [&](const QString &method, const QJsonObject &params) {
            bool finished = false; QString result;
            m_client->request(method, params, [&](const auto &, const QString &error) { result = error; finished = true; }, false);
            QElapsedTimer timer; timer.start();
            while (!finished && timer.elapsed() < 3000) QTest::qWait(1);
            return finished ? result : QStringLiteral("Timed out");
        };
        QVERIFY(!ipcCall("player.next", {{"preserve_state", "bad"}}).isEmpty());
        QVERIFY(!ipcCall("player.seek", {{"position", 1}, {"offset", 2}}).isEmpty());
        QVERIFY(!ipcCall("player.seek", {{"position", 1}, {"queue_id", 1.5}}).isEmpty());
        QCOMPARE(ipcCall("player.next", {}), QString{});
        QTRY_COMPARE(state(), "Playing");
        QCOMPARE(ipcCall("player.seek", {{"position", 1000}}), QString{});
        QTRY_COMPARE(property("Position").toLongLong(), qint64(1000000));
    }
    void playerctlInteroperability() {
        if (QStandardPaths::findExecutable("playerctl").isEmpty()) QSKIP("playerctl is unavailable");
        auto run = [&](const QStringList &args) {
            QProcess process; process.start("playerctl", QStringList{"--player=NekoTune"} + args);
            QElapsedTimer timer; timer.start();
            while (process.state() != QProcess::NotRunning && timer.elapsed() < 5000) QTest::qWait(1);
            if (process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(); return QStringLiteral("TIMEOUT"); }
            return process.exitCode() == 0 ? QString::fromUtf8(process.readAllStandardOutput()).trimmed()
                                          : QString::fromUtf8(process.readAllStandardError());
        };
        QCOMPARE(run({"metadata", "title"}), "测试歌曲");
        QCOMPARE(run({"play"}), QString{});
        QCOMPARE(run({"status"}), "Playing");
        QCOMPARE(run({"pause"}), QString{});
        QCOMPARE(run({"status"}), "Paused");
        QCOMPARE(run({"position", "4"}), QString{});
        QCOMPARE(run({"position"}), "4.000000");
        QCOMPARE(run({"volume", "0.5"}), QString{});
        QCOMPARE(run({"volume"}), "0.500000");
    }
    void withoutSessionBus() {
        QVERIFY(m_bridge->ready());
        QVERIFY(m_service->serviceName().isEmpty());
        bool done = false;
        m_bridge->request("player.play", {}, [&](const QString &error) { QVERIFY(error.isEmpty()); done = true; });
        QTRY_VERIFY(done);
        QTRY_COMPARE(m_playback->state(), "playing");
    }
};
QTEST_GUILESS_MAIN(MprisTest)
#include "mpris_test.moc"
