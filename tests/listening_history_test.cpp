#include "application/playback/listening_history_service.h"
#include "controllers/history_controller.h"
#include "ipc/api/api_context.h"
#include "ipc/ipc_server.h"
#include "storage/listening_history_repository.h"
#include "support/audio_backend.h"
#include "support/store_fixture.h"
#include <QDateTime>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace nekotune;
class HistoryAudio final : public TestAudioBackend {
  public:
    QUrl url;
    qint64 pos = 0, length = 10000;
    bool deferSeek = false, gapless = false;
    quint64 token = 0;
    BackendCapabilities backendCapabilities() const override {
        BackendCapabilities c;
        c.gapless = gapless;
        return c;
    }
    void preloadNext(const PreloadRequest &request) override { token = request.token; }
    void setSource(const QUrl &value) override {
        url = value;
        pos = 0;
    }
    QUrl source() const override { return url; }
    void play() override { emit stateChanged(PlayerState::Playing); }
    void pause() override { emit stateChanged(PlayerState::Paused); }
    void stop() override {
        pos = 0;
        emit positionChanged(pos);
        emit stateChanged(PlayerState::Stopped);
    }
    void seek(qint64 value) override {
        if (!deferSeek) {
            pos = value;
            emit positionChanged(pos);
            emit seeked(pos);
        }
    }
    bool seekable() const override { return true; }
    void setVolume(double) override {}
    qint64 position() const override { return pos; }
    qint64 duration() const override { return length; }
    double volume() const override { return .8; }
    AudioMetadata metadata() const override { return {}; }
};
struct HistoryFixture {
    QTemporaryDir dir;
    StoreFixture store;
    explicit HistoryFixture(const QString &path = {})
        : store(path.isEmpty() ? dir.filePath("history.sqlite") : path) {}
    QueueService queue{store.queueRepo, store.songRepo, store.db};
    HistoryAudio audio;
    PlayerEngine player{audio, queue};
    ListeningHistoryRepository repository{store.db};
    qint64 utc = QDateTime(QDate(2026, 10, 19), QTime(12, 0), QTimeZone::UTC).toMSecsSinceEpoch(), tick = 0;
    ListeningHistoryService history{player, audio, repository, [this] { return utc; },
                                    [this] { return tick; }};
    void advance(qint64 ms, qint64 media = -1) {
        tick += ms;
        utc += ms;
        audio.pos += media < 0 ? qint64(ms * audio.rate) : media;
        emit audio.positionChanged(audio.pos);
    }
    bool start(int count = 1) {
        auto song = store.getOrCreateSong("hash", "/music/song.wav", "Night Song", "歌手");
        if (!song)
            return false;
        song->durationMs = audio.length;
        PlayerQueue candidate;
        for (int i = 0; i < count; ++i)
            candidate.add(song->firstPath, *song);
        candidate.setCurrentIndex(0);
        return bool(player.replaceQueue(candidate));
    }
    HistoryFilter all() const { return {0, utc + 86400000, {}, false, 100, 0}; }
    HistoryPage page() {
        auto r = history.list(all());
        if (!r)
            qFatal("History query failed: %s", qPrintable(r.error().message));
        return r.value();
    }
};

class ListeningHistoryTest : public QObject {
    Q_OBJECT
  private slots:
    void pauseStallSeekRateAndCompletion() {
        HistoryFixture f;
        QVERIFY(f.start());
        f.advance(2000);
        QVERIFY(f.player.pause());
        f.advance(10000, 0);
        QVERIFY(f.player.play());
        QCOMPARE(f.page().total, 1);
        f.advance(1000);
        f.advance(20000, 0); // backend can remain Playing while stalled
        f.advance(1000);
        QVERIFY(f.player.seek(9000));
        f.advance(1000);
        emit f.audio.ended();
        auto s = f.page().items.first();
        QCOMPARE(s.listenedMs, 5000);
        QCOMPARE(s.reason, "ended");
        QVERIFY(!s.completed); // skipped audio is not coverage
        QVERIFY(f.player.play());
        QVERIFY(f.player.setPlaybackRate(2));
        f.advance(5000);
        emit f.audio.ended();
        const auto rows = f.page();
        QCOMPARE(rows.total, 2);
        QCOMPARE(rows.items[0].listenedMs, 5000);
        QVERIFY(rows.items[0].completed);
        QVERIFY(rows.items[0].id != s.id);
    }
    void duplicateOccurrencesManualNavigationAndFailure() {
        HistoryFixture f;
        QVERIFY(f.start(2));
        f.advance(1000);
        const auto first = f.page().items.first();
        QSqlQuery q(f.store.db.database());
        QVERIFY(q.exec(
            "CREATE TRIGGER fail_queue BEFORE DELETE ON queue_items BEGIN SELECT RAISE(FAIL,'fail'); END"));
        QVERIFY(!f.player.next());
        QCOMPARE(f.page().total, 1);
        QCOMPARE(f.page().items.first().reason, "");
        QVERIFY(q.exec("DROP TRIGGER fail_queue"));
        QVERIFY(f.player.next());
        f.advance(2000);
        QVERIFY(f.player.stop());
        const auto rows = f.page();
        QCOMPARE(rows.total, 2);
        QCOMPARE(rows.items[1].reason, "skipped");
        QCOMPARE(rows.items[1].listenedMs, 1000);
        QCOMPARE(rows.items[0].listenedMs, 2000);
        QCOMPARE(rows.items[0].reason, "stopped");
        QCOMPARE(rows.items[0].songKey, first.songKey);
        QVERIFY(rows.items[0].queueId != first.queueId);
        QVERIFY(rows.items[0].id != first.id);
    }
    void gaplessAndRepeatOneAreSeparateSessions() {
        HistoryFixture f;
        f.audio.gapless = true;
        QVERIFY(f.start(2));
        emit f.audio.metadataChanged();
        QVERIFY(f.audio.token > 0);
        f.advance(10000);
        f.audio.pos = 0;
        emit f.audio.nextTrackStarted(f.audio.token);
        f.advance(1000);
        auto rows = f.page();
        QCOMPARE(rows.total, 2);
        QVERIFY(rows.items[1].completed);
        QCOMPARE(rows.items[1].listenedMs, 10000);
        QCOMPARE(rows.items[0].listenedMs, 1000);
        QVERIFY(f.player.setPlaybackMode(PlaybackMode::RepeatOne));
        f.advance(9000);
        emit f.audio.ended();
        f.advance(1000);
        rows = f.page();
        QCOMPARE(rows.total, 3);
        QCOMPARE(rows.items[0].queueId, rows.items[1].queueId);
        QVERIFY(rows.items[0].id != rows.items[1].id);
    }
    void midnightLocalTimezoneDstAndWeeklyAggregation() {
        HistoryFixture f;
        const QTimeZone taipei("Asia/Taipei");
        f.utc = QDateTime(QDate(2026, 10, 19), QTime(23, 59, 58), taipei).toMSecsSinceEpoch();
        QVERIFY(f.start());
        f.advance(5000);
        QVERIFY(f.player.stop());
        const auto result = f.history.statistics(taipei);
        QVERIFY(result);
        QCOMPARE(result.value().weekMs, 5000);
        QCOMPARE(result.value().days.last().listenedMs, 3000);
        QCOMPARE(result.value().days[result.value().days.size() - 2].listenedMs, 2000);
        QCOMPARE(result.value().songs.first().listenedMs, 5000);
        const auto utcStats = f.history.statistics(QTimeZone(QTimeZone::UTC));
        QVERIFY(utcStats);
        QCOMPARE(utcStats.value().days.last().listenedMs, 5000);

        HistoryFixture dst;
        const QTimeZone ny("America/New_York");
        dst.utc = QDateTime(QDate(2026, 11, 1), QTime(0, 0), ny).toMSecsSinceEpoch();
        QVERIFY(dst.start());
        dst.audio.length = 25 * 3600000;
        dst.advance(25 * 3600000);
        QVERIFY(dst.player.stop());
        const auto stats = dst.history.statistics(ny);
        QVERIFY(stats);
        QCOMPARE(stats.value().days[28].to - stats.value().days[28].from, 25 * 3600000);
        QCOMPARE(stats.value().days[28].listenedMs, 25 * 3600000);
        QCOMPARE(stats.value().days.last().listenedMs, 0);
    }
    void crashRecoveryShutdownClearAndLegacyCounts() {
        HistoryFixture f;
        QVERIFY(f.start());
        f.advance(3000);
        const auto before = f.page().items.first(); // durable checkpoint
        QVERIFY(f.repository.recover());
        const auto recovered = f.repository.list(f.all());
        QVERIFY(recovered);
        QCOMPARE(recovered.value().items.first().reason, "interrupted");
        QCOMPARE(recovered.value().items.first().listenedMs, before.listenedMs);
        QVERIFY(f.history.clear());
        f.advance(1000);
        QVERIFY(f.player.pause());
        QVERIFY(f.player.play());
        f.advance(1000);
        QCOMPARE(f.page().total, 0); // current occurrence cannot resurrect erased history
        QVERIFY(f.player.stop());
        QVERIFY(f.player.play());
        f.advance(2000);
        f.player.shutdown();
        QCOMPARE(f.page().items.first().reason, "shutdown");
        QSqlQuery sql(f.store.db.database());
        QVERIFY(sql.exec("UPDATE songs SET play_count=42"));
        QVERIFY(f.history.clear());
        QCOMPARE(f.store.songs().first().playCount, 42);
        QCOMPARE(f.page().total, 0);
        QVERIFY(sql.exec("SELECT COUNT(*) FROM listening_intervals"));
        QVERIFY(sql.next());
        QCOMPARE(sql.value(0).toInt(), 0);
    }
    void reopenDoesNotCountOfflineTimeOrInventLegacyHistory() {
        QTemporaryDir dir;
        const auto path = dir.filePath("restart.sqlite");
        QString id;
        {
            HistoryFixture f(path);
            QVERIFY(f.start());
            f.advance(4000);
            id = f.page().items.first().id;
            // Destroy without PlayerEngine::shutdown, like an ungraceful backend exit.
        }
        {
            HistoryFixture f(path);
            QVERIFY(f.history.recover());
            f.advance(60000, 0);
            auto rows = f.page();
            QCOMPARE(rows.total, 1);
            QCOMPARE(rows.items.first().id, id);
            QCOMPARE(rows.items.first().reason, "interrupted");
            QCOMPARE(rows.items.first().listenedMs, 4000);
            QVERIFY(f.player.snapshot().state == PlayerState::Stopped);
            QVERIFY(f.player.play());
            f.advance(1000);
            QCOMPARE(f.page().total, 2);
            QVERIFY(f.history.clear());
            QSqlQuery q(f.store.db.database());
            QVERIFY(q.exec("UPDATE songs SET play_count=123, last_played_at='2026-10-18T12:00:00.000Z'"));
        }
        HistoryFixture migrated(path);
        QVERIFY(migrated.history.recover());
        QCOMPARE(migrated.page().total, 0);
        QCOMPARE(migrated.store.songs().first().playCount, 123);
    }
    void delayedSeekBacktrackingAndFinalWriteRetry() {
        HistoryFixture f;
        QVERIFY(f.start());
        f.advance(2000);
        f.audio.deferSeek = true;
        QVERIFY(f.player.seek(8000));
        f.advance(500, 500); // still reports the old position before asynchronous confirmation
        f.audio.pos = 8000;
        emit f.audio.positionChanged(8000);
        emit f.audio.seeked(8000);
        f.advance(1000);
        QCOMPARE(f.page().items.first().listenedMs, 3000);
        f.audio.deferSeek = false;
        QVERIFY(f.player.seek(0));
        f.advance(5000);
        QCOMPARE(f.page().items.first().listenedMs, 8000);
        QSqlQuery q(f.store.db.database());
        QVERIFY(q.exec("CREATE TRIGGER fail_final BEFORE UPDATE ON listening_sessions BEGIN SELECT "
                       "RAISE(FAIL,'fail'); END"));
        f.advance(1000);
        QVERIFY(f.player.stop());
        QVERIFY(q.exec("DROP TRIGGER fail_final"));
        const auto rows = f.page();
        QCOMPARE(rows.total, 1);
        QCOMPARE(rows.items.first().reason, "stopped");
        QCOMPARE(rows.items.first().listenedMs, 9000);
        QVERIFY(!rows.items.first().completed);
    }
    void subsecondGaplessTailAndPauseAreFlushed() {
        HistoryFixture f;
        f.audio.gapless = true;
        f.audio.length = 10500;
        QVERIFY(f.start(2));
        emit f.audio.metadataChanged();
        for (int i = 0; i < 21; ++i)
            f.advance(500);
        f.audio.pos = 0;
        emit f.audio.nextTrackStarted(f.audio.token);
        f.advance(250);
        QVERIFY(f.player.pause());
        const auto rows = f.page();
        QCOMPARE(rows.items[1].listenedMs, 10500);
        QVERIFY(rows.items[1].completed);
        QCOMPARE(rows.items[0].listenedMs, 250);
    }
    void storageAtomicityPaginationSearchAndDeletion() {
        HistoryFixture f;
        QVERIFY(f.start());
        f.advance(1000);
        QSqlQuery sql(f.store.db.database());
        QVERIFY(sql.exec("CREATE TRIGGER fail_history BEFORE INSERT ON listening_intervals BEGIN SELECT "
                         "RAISE(FAIL,'disk failure'); END"));
        QVERIFY(!f.history.list(f.all()));
        auto stored = f.repository.list(f.all());
        QVERIFY(stored);
        QCOMPARE(stored.value().items.first().listenedMs, 0);
        QVERIFY(sql.exec("DROP TRIGGER fail_history"));
        QCOMPARE(f.page().items.first().listenedMs, 1000);
        QCOMPARE(f.page().items.first().listenedMs, 1000); // repeated readers never add accounting
        QVERIFY(f.player.stop());
        for (int i = 0; i < 54; ++i) {
            QVERIFY(f.player.play());
            f.advance(10000);
            emit f.audio.ended();
        }
        auto filter = f.all();
        filter.limit = 50;
        auto first = f.repository.list(filter);
        QVERIFY(first);
        QCOMPARE(first.value().items.size(), 50);
        QCOMPARE(first.value().total, 55);
        filter.offset = 50;
        auto second = f.repository.list(filter);
        QVERIFY(second);
        QCOMPARE(second.value().items.size(), 5);
        QVERIFY(first.value().items.last().id != second.value().items.first().id);
        filter.offset = 0;
        filter.search = "night";
        filter.completedOnly = true;
        auto completed = f.repository.list(filter);
        QVERIFY(completed);
        QCOMPARE(completed.value().total, 54);
        filter.search = "%";
        auto literal = f.repository.list(filter);
        QVERIFY(literal);
        QCOMPARE(literal.value().total, 0);
        QVERIFY(f.player.clear());
        QVERIFY(f.store.songRepo.erase(f.store.songs().first().id));
        QCOMPARE(f.page().total, 55);
        QCOMPARE(f.page().items.first().title, "Night Song");
    }
    void ipcValidationAndTwoFrontends() {
        HistoryFixture f;
        f.utc = QDateTime::currentMSecsSinceEpoch();
        qputenv("NEKOTUNE_SOCKET", f.dir.filePath("ipc.sock").toUtf8());
        IpcRouter router;
        registerHistoryApi(router, f.history);
        IpcServer server(router, [] { return QJsonObject{}; });
        QVERIFY(server.listen());
        IpcClient client1, client2;
        HistoryController frontend1(client1), frontend2(client2);
        client1.connectBackend();
        client2.connectBackend();
        QTRY_VERIFY(client1.connected() && client2.connected());
        QVERIFY(f.start());
        f.advance(2000);
        frontend1.refresh();
        frontend2.refresh();
        QTRY_COMPARE(frontend1.total(), 1);
        QTRY_COMPARE(frontend2.total(), 1);
        QCOMPARE(f.page().items.first().listenedMs, 2000);
        for (const QJsonObject &params :
             {QJsonObject{{"days", 2}}, QJsonObject{{"limit", 500}}, QJsonObject{{"offset", -1}},
              QJsonObject{{"timezone", "invalid"}}, QJsonObject{{"completed_only", "true"}}}) {
            QJsonObject response;
            router.dispatch({{"method", "history.list"}, {"params", params}}, [&](auto r) { response = r; });
            QCOMPARE(response.value("status").toString(), "error");
        }
        QJsonObject response;
        router.dispatch({{"method", "history.clear"}}, [&](auto r) { response = r; });
        QCOMPARE(response.value("status").toString(), "error");
        QCOMPARE(f.page().total, 1);
        server.shutdown();
    }
};
QTEST_GUILESS_MAIN(ListeningHistoryTest)
#include "listening_history_test.moc"
