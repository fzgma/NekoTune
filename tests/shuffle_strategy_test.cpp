#include "application/playback/shuffle_strategy_service.h"
#include "application/playback/player_engine.h"
#include "controllers/playback_controller.h"
#include "runtime/backend_runtime.h"
#include "support/audio_backend.h"
#include "support/store_fixture.h"
#include "app_paths.h"
#include <QJsonArray>
#include <QFile>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace nekotune;

namespace {
class ShuffleAudio final : public TestAudioBackend {
  public:
    QUrl url;
    bool gapless = false, preloading = false;
    int loads = 0, cancellations = 0;
    PreloadRequest request;
    BackendCapabilities backendCapabilities() const override { return {gapless, gapless}; }
    void setSource(const QUrl &value) override {
        url = value; ++loads;
        if (!url.isEmpty()) emit metadataChanged();
    }
    QUrl source() const override { return url; }
    void play() override { emit stateChanged(PlayerState::Playing); }
    void pause() override { emit stateChanged(PlayerState::Paused); }
    void stop() override { emit stateChanged(PlayerState::Stopped); }
    void seek(qint64 value) override { emit seeked(value); }
    void setVolume(double) override {}
    qint64 position() const override { return 0; }
    qint64 duration() const override { return 1000; }
    double volume() const override { return .8; }
    AudioMetadata metadata() const override { return {}; }
    void preloadNext(const PreloadRequest &value) override { request = value; preloading = true; }
    void cancelPreload() override { preloading = false; ++cancellations; }
    void startPreload() { url = request.source; emit nextTrackStarted(request.token); }
};

PlayerQueue sampleQueue() {
    PlayerQueue queue;
    SongMetadata song;
    song.id = 7; song.customTitle = "Title"; song.artist = "Artist"; song.album = "Album";
    song.durationMs = 1000; song.playCount = 17; song.lastPlayedAt = "2026-10-20T12:00:00.000Z";
    for (int i = 0; i < 4; ++i) queue.add(QString("/music/%1.wav").arg(i), song);
    queue.setCurrentIndex(0);
    return queue;
}
int advance(PlaybackOrderService &order, PlayerQueue &queue, PlaybackAdvance reason = PlaybackAdvance::Next) {
    auto selection = order.propose(queue, reason);
    const int id = selection.queueId;
    queue.setCurrentIndex(queue.indexById(id));
    order.confirm(std::move(selection));
    return id;
}
} // namespace

class ShuffleStrategyTest final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_home;
  private slots:
    void init() {
        m_home = std::make_unique<QTemporaryDir>();
        qputenv("NEKOTUNE_HOME", m_home->path().toUtf8());
        qputenv("NEKOTUNE_SOCKET", m_home->filePath("shuffle.sock").toUtf8());
    }
    void preferenceFallbackAndSaveFailure() {
        QString saved;
        bool writable = true;
        ShuffleStrategyService service("test/custom", [&](const QString &id) {
            if (!writable) return false;
            saved = id; return true;
        });
        QSignalSpy changes(&service, &ShuffleStrategyService::strategyChanged);
        QCOMPARE(service.effective(), "shuffle_bag");
        QCOMPARE(service.preferred(), "test/custom");
        auto selector = [](const QString &, quint64, const sdk::ShuffleRequest &) -> Result<sdk::ShuffleSelection> {
            return failure("Unused");
        };
        service.setCatalogue({{"test/custom", "Custom", 1}}, selector);
        QCOMPARE(service.effective(), "test/custom");
        QCOMPARE(changes.size(), 1);
        service.setCatalogue({{"test/custom", "Custom", 1}}, selector);
        QCOMPARE(changes.size(), 1);
        writable = false;
        QVERIFY(!service.select("shuffle_bag"));
        QCOMPARE(service.preferred(), "test/custom");
        QCOMPARE(changes.size(), 1);
        QVERIFY(!service.select("missing"));
        service.setCatalogue({}, selector);
        QCOMPARE(service.preferred(), "test/custom");
        QCOMPARE(service.effective(), "shuffle_bag");
        service.setCatalogue({{"test/custom", "Custom", 2}}, selector);
        QCOMPARE(service.effective(), "test/custom");
        QCOMPARE(changes.size(), 3);
        writable = true;
        QVERIFY(service.select("shuffle_bag"));
        QCOMPARE(saved, "shuffle_bag");
    }
    void proposalsAreTransactionalAndReceiveMetadata() {
        QList<sdk::ShuffleRequest> requests;
        ShuffleStrategyService service("test/custom");
        service.setCatalogue({{"test/custom", "Custom", 1}},
            [&](const QString &, quint64, const sdk::ShuffleRequest &request) -> Result<sdk::ShuffleSelection> {
                requests.append(request);
                const int step = request.state.value("step", 0).toInt();
                return sdk::ShuffleSelection{request.queue.at(step % request.queue.size()).queueId, {{"step", step + 1}}};
            });
        auto queue = sampleQueue();
        PlaybackOrderService order(service.createStrategy(), PlaybackMode::Shuffle);
        order.reset(queue);
        for (int i = 0; i < 3; ++i) QCOMPARE(order.propose(queue, PlaybackAdvance::Next).queueId, 1);
        QCOMPARE(requests.size(), 3);
        QCOMPARE(requests[0].seed, requests[2].seed);
        QVERIFY(requests[2].state.isEmpty());
        const auto track = requests[0].queue[1];
        QCOMPARE(track.queueId, 2); QCOMPARE(track.songId, 7);
        QCOMPARE(track.title, "Title"); QCOMPARE(track.artist, "Artist"); QCOMPARE(track.album, "Album");
        QCOMPARE(track.durationMs, 1000); QCOMPARE(track.playCount, 17);
        QCOMPARE(track.lastPlayedAt, "2026-10-20T12:00:00.000Z");
        QCOMPARE(advance(order, queue), 1); // Extensions may repeat the current occurrence.
        QCOMPARE(advance(order, queue), 2); // Same song, different occurrence.
        QCOMPARE(requests.last().state.value("step").toInt(), 1);
        QCOMPARE(advance(order, queue), 3);
        const int count = requests.size();
        QCOMPARE(advance(order, queue, PlaybackAdvance::Previous), 2);
        QCOMPARE(advance(order, queue), 3);
        QCOMPARE(requests.size(), count); // History replay never draws.
        queue[3].playNext = true;
        order.syncQueue(queue);
        QCOMPARE(advance(order, queue), 4);
        QCOMPARE(requests.size(), count); // Play-next bypasses the algorithm.
        queue[3].playNext = false;
        queue[0].metadata.artist = "Updated";
        queue.removeAt(1);
        order.syncQueue(queue);
        order.propose(queue, PlaybackAdvance::Next);
        QCOMPARE(requests.last().queue.size(), 3);
        QCOMPARE(requests.last().queue.first().artist, "Updated");
        QCOMPARE(requests.last().state.value("step").toInt(), 3);
        queue.clear(); order.reset(queue);
        const int before = requests.size();
        QCOMPARE(order.propose(queue, PlaybackAdvance::Next).queueId, 0);
        QCOMPARE(requests.size(), before);
    }
    void saveFailurePreloadingAndSwitching() {
        StoreFixture store(m_home->filePath("songs.sqlite3"));
        QVERIFY(store.isReady());
        const auto song = store.getOrCreateSong("hash", "/music/0.wav", "Title", "Artist");
        QVERIFY(song);
        auto list = sampleQueue();
        for (int i = 0; i < list.size(); ++i) list[i].metadata = *song;
        QueueService queue(store.queueRepo, store.songRepo, store.db);
        ShuffleAudio audio;
        QList<sdk::ShuffleRequest> requests;
        ShuffleStrategyService service("test/custom");
        service.setCatalogue({{"test/custom", "Custom", 1}},
            [&](const QString &, quint64, const sdk::ShuffleRequest &request) -> Result<sdk::ShuffleSelection> {
                requests.append(request);
                const int step = request.state.value("step", 0).toInt();
                return sdk::ShuffleSelection{request.queue.at((step + 1) % request.queue.size()).queueId, {{"step", step + 1}}};
            });
        PlayerEngine player(audio, queue, service.createStrategy(), PlaybackMode::Shuffle);
        QVERIFY(player.replaceQueue(list));
        const auto initialSeed = requests.last().seed;
        QSqlQuery sql(store.db.database());
        QVERIFY(sql.exec("CREATE TRIGGER fail_shuffle BEFORE DELETE ON queue_items BEGIN SELECT RAISE(FAIL, 'injected'); END"));
        QVERIFY(!player.next());
        QCOMPARE(player.snapshot().song->id, 1);
        QCOMPARE(requests.last().seed, initialSeed);
        QVERIFY(requests.last().state.isEmpty());
        QVERIFY(sql.exec("DROP TRIGGER fail_shuffle"));
        QVERIFY(player.next());
        QCOMPARE(player.snapshot().song->id, 2);
        QCOMPARE(requests.last().state.value("step").toInt(), 1); // Refresh observes the next proposal.
        audio.gapless = true;
        emit audio.metadataChanged();
        QVERIFY(audio.preloading);
        QCOMPARE(audio.request.source, QUrl::fromLocalFile("/music/2.wav"));
        const auto preloadSeed = requests.last().seed;
        emit audio.seekableChanged(); emit audio.durationChanged(1000);
        QCOMPARE(requests.last().seed, preloadSeed);
        QCOMPARE(requests.last().state.value("step").toInt(), 1);
        audio.startPreload();
        QCOMPARE(player.snapshot().song->id, 3);
        QCOMPARE(requests.last().state.value("step").toInt(), 2);
        const auto oldToken = audio.request.token;
        auto updated = *song; updated.artist = "New artist";
        player.metadataUpdated(updated);
        QVERIFY(audio.request.token != oldToken);
        QCOMPARE(requests.last().queue.first().artist, "New artist");
        const auto oldSource = audio.source();
        const auto oldLoads = audio.loads;
        player.setShuffleStrategy(std::make_unique<ShuffleBagStrategy>(9));
        QCOMPARE(audio.source(), oldSource); QCOMPARE(audio.loads, oldLoads);
        QVERIFY(player.previous());
        QCOMPARE(player.snapshot().song->id, 3); // New history starts at the audible occurrence.
    }
    void immediateRepeatRemainsNavigable() {
        StoreFixture store(m_home->filePath("repeat.sqlite3"));
        auto song = store.getOrCreateSong("hash", "/music/repeat.wav");
        QVERIFY(song);
        QueueService queue(store.queueRepo, store.songRepo, store.db);
        ShuffleAudio audio;
        ShuffleStrategyService service("test/custom");
        service.setCatalogue({{"test/custom", "Custom", 1}},
            [](const QString &, quint64, const sdk::ShuffleRequest &request) -> Result<sdk::ShuffleSelection> {
                return sdk::ShuffleSelection{request.currentId, {}};
            });
        PlayerEngine player(audio, queue, service.createStrategy(), PlaybackMode::Shuffle);
        PlayerQueue one; one.add("/music/repeat.wav", *song); one.setCurrentIndex(0);
        QVERIFY(player.replaceQueue(one));
        QVERIFY(player.snapshot().capabilities.canGoNext);
        QVERIFY(player.next());
        QVERIFY(player.snapshot().capabilities.canGoPrevious);
        QVERIFY(player.previous());
        QCOMPARE(player.snapshot().song->id, 1);
    }
    void ipcPreferenceLifecycleAndClientSync() {
        BackendRuntime runtime([] { return std::make_unique<ShuffleAudio>(); });
        QVERIFY2(runtime.start(), qPrintable(runtime.errorString()));
        IpcClient first, second;
        PlaybackController a(first), b(second);
        first.connectBackend(); second.connectBackend();
        QTRY_VERIFY(first.connected() && second.connected());
        IpcClient *requestClient = &first;
        auto request = [&](const QString &method, const QJsonObject &params = {}) {
            QJsonObject response;
            QString error;
            bool finished = false;
            requestClient->request(method, params, [&](const QJsonObject &data, const QString &message) {
                response = data; error = message; finished = true;
            }, false, 5000);
            if (!QTest::qWaitFor([&] { return finished; }, 6000)) error = "Request timed out";
            return QPair{response, error};
        };
        QCOMPARE(request("player.shuffle_strategies").first.value("shuffle_strategy").toString(), "shuffle_bag");
        const auto path = QStringLiteral(NEKOTUNE_EXAMPLE_EXTENSIONS "/example.shuffle");
        QVERIFY(request("extensions.install", {{"path", path}, {"development", true}}).second.isEmpty());
        QVERIFY(request("extensions.enable", {{"id", "example.shuffle"}, {"trusted", true}}).second.isEmpty());
        QTRY_COMPARE(a.shuffleStrategies().size(), 2);
        QVERIFY(request("player.set_shuffle_strategy", {{"id", "example.shuffle/artist"}}).second.isEmpty());
        QTRY_COMPARE(a.shuffleStrategy(), "example.shuffle/artist");
        QTRY_COMPARE(b.effectiveShuffleStrategy(), "example.shuffle/artist");
        QCOMPARE(a.playbackMode(), "sequential");
        QVERIFY(request("player.set_shuffle_strategy", {{"id", "missing"}}).second.size() > 0);
        for (const auto &params : {QJsonObject{}, QJsonObject{{"id", ""}}, QJsonObject{{"id", 42}}})
            QVERIFY(!request("player.set_shuffle_strategy", params).second.isEmpty());
        QVERIFY(request("extensions.disable", {{"id", "example.shuffle"}}).second.isEmpty());
        QTRY_COMPARE(b.effectiveShuffleStrategy(), "shuffle_bag");
        QCOMPARE(b.shuffleStrategy(), "example.shuffle/artist");
        QVERIFY(request("extensions.enable", {{"id", "example.shuffle"}}).second.isEmpty());
        QTRY_COMPARE(b.effectiveShuffleStrategy(), "example.shuffle/artist");
        runtime.stop();
        QCOMPARE(AppPaths::setting("shuffle_strategy").toString(), "example.shuffle/artist");
        QVERIFY(runtime.start());
        IpcClient reconnected;
        PlaybackController c(reconnected);
        reconnected.connectBackend();
        requestClient = &reconnected;
        QTRY_COMPARE(c.effectiveShuffleStrategy(), "example.shuffle/artist");
        QCOMPARE(c.shuffleStrategy(), "example.shuffle/artist");
        QFile settings(AppPaths::configFile("settings.json"));
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("broken"); settings.close();
        QVERIFY(!request("player.set_shuffle_strategy", {{"id", "shuffle_bag"}}).second.isEmpty());
        QCOMPARE(request("player.status").first.value("shuffle_strategy").toString(), "example.shuffle/artist");
        QCOMPARE(c.effectiveShuffleStrategy(), "example.shuffle/artist");
        runtime.stop();
    }
};

QTEST_GUILESS_MAIN(ShuffleStrategyTest)
#include "shuffle_strategy_test.moc"
