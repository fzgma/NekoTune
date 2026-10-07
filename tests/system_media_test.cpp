#include "support/media_audio.h"
#include "support/store_fixture.h"
#include "application/playback/player_engine.h"
#include "controllers/playback_controller.h"
#include "platform/system_media_bridge.h"
#include "infrastructure/playback/qt_playback_backend.h"
#include <QDataStream>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

using namespace nekotune;
class SystemMediaTest final : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    std::unique_ptr<StoreFixture> m_store;
    std::unique_ptr<QueueService> m_queue;
    std::unique_ptr<MediaAudio> m_audio;
    std::unique_ptr<PlayerEngine> m_player;
  private slots:
    void init() {
        m_store = std::make_unique<StoreFixture>(m_dir.filePath("media.sqlite3"));
        auto song = m_store->library.importFile({"/music/test.wav", "same-song", "测试歌曲", 30000}, "测试歌曲", "艺人");
        QVERIFY(song);
        m_queue = std::make_unique<QueueService>(m_store->queueRepo, m_store->songRepo, m_store->db);
        m_audio = std::make_unique<MediaAudio>();
        m_player = std::make_unique<PlayerEngine>(*m_audio, *m_queue);
        PlayerQueue queue;
        for (int i = 0; i < 3; ++i) queue.add("/music/test.wav", song.value());
        queue.setCurrentIndex(0);
        QVERIFY(m_player->replaceQueue(queue, false));
    }
    void cleanup() { m_player.reset(); m_audio.reset(); m_queue.reset(); m_store.reset(); }
    void navigationPreservesStateAndLegacyDefault() {
        QVERIFY(m_player->next(true));
        QCOMPARE(int(m_player->snapshot().state), int(PlayerState::Paused));
        QCOMPARE(m_player->snapshot().song->id, 2);
        QVERIFY(m_player->stop());
        QVERIFY(m_player->next(true));
        QCOMPARE(int(m_player->snapshot().state), int(PlayerState::Stopped));
        QCOMPARE(m_player->snapshot().song->id, 3);
        QVERIFY(m_player->previous(true));
        QCOMPARE(int(m_player->snapshot().state), int(PlayerState::Stopped));
        QVERIFY(m_player->previous());
        QCOMPARE(int(m_player->snapshot().state), int(PlayerState::Playing));
        QVERIFY(m_player->next(true));
        QCOMPARE(int(m_player->snapshot().state), int(PlayerState::Playing));
    }
    void guardedSeeksAndRelativeBoundaries() {
        QVERIFY(m_player->seekTrack(4000, false, 1));
        QCOMPARE(m_audio->position(), 4000);
        QVERIFY(m_player->seekTrack(9000, false, 2));
        QVERIFY(m_player->seekTrack(30001, false, 1));
        QVERIFY(m_player->seekTrack(-1, false, 1));
        QCOMPARE(m_audio->position(), 4000);
        QVERIFY(m_player->seekTrack(std::numeric_limits<qint64>::min(), true, 1));
        QCOMPARE(m_audio->position(), 0);
        QVERIFY(m_player->seekTrack(30000, true, 1));
        QCOMPARE(m_player->snapshot().song->id, 1);
        QVERIFY(m_player->seekTrack(std::numeric_limits<qint64>::max(), true, 1));
        QCOMPARE(m_player->snapshot().song->id, 2);
        QCOMPARE(int(m_player->snapshot().state), int(PlayerState::Paused));
        QVERIFY(m_player->seek(123456)); // Existing unguarded absolute contract remains unchanged.
        QCOMPARE(m_audio->position(), 123456);
    }
    void capabilitiesAndSeekConfirmation() {
        auto capabilities = m_player->snapshot().capabilities;
        QVERIFY(capabilities.canPlay && capabilities.canPause && capabilities.canSeek && capabilities.canGoNext);
        QVERIFY(!capabilities.canGoPrevious);
        QSignalSpy seeks(m_player.get(), &PlayerEngine::seeked);
        m_audio->deferSeek = true;
        QVERIFY(m_player->seek(5000));
        QCOMPARE(seeks.count(), 0);
        m_audio->confirmSeek();
        QCOMPARE(seeks.count(), 1);
        QCOMPARE(seeks.at(0), QVariantList({qint64(5000), 1}));
        QVERIFY(m_player->seek(8000));
        QVERIFY(m_player->next(true));
        m_audio->confirmSeek();
        QCOMPARE(seeks.count(), 1);
        m_audio->allowSeek = false;
        emit m_audio->seekableChanged();
        QVERIFY(!m_player->snapshot().capabilities.canSeek);
        m_audio->suspendAudioOutput();
        QVERIFY(!m_player->snapshot().capabilities.canPlay);
        QVERIFY(!m_player->play());
        QVERIFY(m_player->snapshot().song.has_value());
        QVERIFY(m_player->clear());
        QVERIFY(!m_player->snapshot().capabilities.canPause);
        QVERIFY(!m_player->snapshot().capabilities.canGoNext);
    }
    void shuffleCapabilityQueriesDoNotConsumeOrder() {
        QVERIFY(m_player->setPlaybackMode(PlaybackMode::Shuffle));
        for (int i = 0; i < 50; ++i) {
            emit m_audio->seekableChanged();
            QVERIFY(m_player->snapshot().capabilities.canGoNext);
        }
        QVERIFY(m_player->next(true));
        const auto next = m_player->snapshot().song->id;
        QVERIFY(m_player->previous(true));
        QCOMPARE(m_player->snapshot().song->id, 1);
        QVERIFY(m_player->next(true));
        QCOMPARE(m_player->snapshot().song->id, next);
    }
    void passiveLoadingRetainsPauseAndStop() {
        emit m_audio->stateChanged(PlayerState::Loading);
        QCOMPARE(int(m_player->snapshot().state), int(PlayerState::Paused));
        QVERIFY(m_player->stop());
        QVERIFY(m_player->next(true));
        emit m_audio->stateChanged(PlayerState::Loading);
        QCOMPARE(int(m_player->snapshot().state), int(PlayerState::Stopped));
        QVERIFY(m_player->play());
        emit m_audio->stateChanged(PlayerState::Loading);
        QCOMPARE(int(m_player->snapshot().state), int(PlayerState::Loading));
    }
    void qtAdapterReportsObservedSeek() {
        QFile file(m_dir.filePath("seek.wav"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
        file.write("RIFF", 4); stream << quint32(36 + 88200); file.write("WAVEfmt ", 8);
        stream << quint32(16) << quint16(1) << quint16(1) << quint32(44100) << quint32(88200)
               << quint16(2) << quint16(16);
        file.write("data", 4); stream << quint32(88200); file.write(QByteArray(88200, '\0')); file.close();
        QtPlaybackBackend audio;
        QSignalSpy seeks(&audio, &IPlaybackBackend::seeked);
        audio.setSource(QUrl::fromLocalFile(file.fileName()));
        QTRY_VERIFY_WITH_TIMEOUT(audio.seekable(), 5000);
        audio.seek(500);
        QTRY_COMPARE(seeks.count(), 1);
        const auto observed = seeks.first().first().toLongLong();
        QVERIFY(observed >= 400 && observed <= 600);
        QCOMPARE(observed, audio.position());
        audio.setSource({});
        QVERIFY(!audio.seekable());
        const auto count = seeks.count();
        audio.seek(700);
        QTest::qWait(30);
        QCOMPARE(seeks.count(), count);
    }
    void bridgeMetadataUnitsAndMappings() {
        qputenv("NEKOTUNE_SOCKET", m_dir.filePath("absent.sock").toUtf8());
        IpcClient client;
        PlaybackController playback(client);
        SystemMediaBridge bridge(client, playback);
        playback.apply({{"song", QJsonObject{{"queue_id", 7}}}, {"position", 1234}, {"duration", 30000}, {"state", "loading"}});
        QCOMPARE(bridge.positionUs(), qint64(1234000));
        QCOMPARE(bridge.durationUs(), qint64(30000000));
        QCOMPARE(bridge.playbackStatus(), "Paused");
        playback.apply({{"duration", 0}});
        QCOMPARE(bridge.positionUs(), qint64(1234000));
        playback.apply({{"duration", 30000}});
        QVERIFY(bridge.trackId().endsWith("/queue7"));
        playback.apply({{"state", "error"}});
        QCOMPARE(bridge.playbackStatus(), "Stopped");
        playback.apply({{"duration", double(std::numeric_limits<qint64>::max())}});
        QCOMPARE(bridge.durationUs(), std::numeric_limits<qint64>::max());
        playback.apply({{"song", QJsonObject{}}});
        QCOMPARE(bridge.trackId(), "/org/mpris/MediaPlayer2/TrackList/NoTrack");
    }
};
QTEST_GUILESS_MAIN(SystemMediaTest)
#include "system_media_test.moc"
