#include "application/lyrics/lyrics_controller.h"
#include "application/playback/queue_service.h"
#include "infrastructure/library/audio_duration.h"
#include "infrastructure/playback/qt_playback_backend.h"
#include "infrastructure/lyrics/lyrics_storage.h"
#include "ipc/serialization/serialization.h"
#include "runtime/library_scanner.h"
#include "support/audio_backend.h"
#include "support/store_fixture.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace nekotune;
namespace {
QByteArray wav(int milliseconds = 1500) {
  constexpr quint32 sampleRate = 8000;
  const quint32 size = milliseconds * sampleRate / 1000 * 2;
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream.setByteOrder(QDataStream::LittleEndian);
  stream.writeRawData("RIFF", 4);
  stream << quint32(36 + size);
  stream.writeRawData("WAVEfmt ", 8);
  stream << quint32(16) << quint16(1) << quint16(1) << sampleRate
         << quint32(sampleRate * 2) << quint16(2) << quint16(16);
  stream.writeRawData("data", 4);
  stream << size;
  bytes.append(QByteArray(size, '\0'));
  return bytes;
}
QString hash(const QByteArray &bytes) {
  return QString::fromLatin1(
      QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
void write(const QString &path, const QByteArray &bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write(bytes), bytes.size());
}

class DurationAudio final : public TestAudioBackend {
public:
  QUrl url;
  qint64 decodedDuration = 9000;
  qint64 clock = 0;
  void setSource(const QUrl &source) override { url = source; }
  QUrl source() const override { return url; }
  void play() override { emit stateChanged(PlayerState::Playing); }
  void pause() override { emit stateChanged(PlayerState::Paused); }
  void stop() override { emit stateChanged(PlayerState::Stopped); }
  void seek(qint64 position) override {
    clock = position;
    emit positionChanged(position);
  }
  void setVolume(double) override {}
  qint64 position() const override { return clock; }
  qint64 duration() const override { return decodedDuration; }
  double volume() const override { return .8; }
  AudioMetadata metadata() const override { return {}; }
};

class StoredDurationProvider final : public LyricsProvider {
public:
  void cancel() override {}
  void request(const LyricsQuery &query, quint64 token, bool) override {
    LyricsDocument document;
    document.source = "lrclib";
    document.matched = query;
    document.plainLyrics = "Stored metadata is ready";
    emit completed(token, {{document}});
  }
};
} // namespace

class AudioDurationTest final : public QObject {
  Q_OBJECT
private slots:
  void nativePlaybackRateRetainsMediaTimeline() {
    QTemporaryDir dir;
    const auto path = dir.filePath("rate.wav");
    write(path, wav(2500));
    QtPlaybackBackend backend;
    if (!backend.supportsPlaybackRate()) {
      backend.setPlaybackRate(1.5);
      QCOMPARE(backend.playbackRate(), 1.0);
      QSKIP("This Qt backend has no pitch-preserving playback rate support");
    }
    backend.setSource(QUrl::fromLocalFile(path));
    QTRY_COMPARE(backend.duration(), 2500);
    QTRY_VERIFY(backend.seekable());
    backend.seek(1000);
    QTRY_COMPARE(backend.position(), 1000);
    for (double rate : {.5, .75, 1.0, 1.25, 1.5, 1.75, 2.0}) {
      backend.setPlaybackRate(rate);
      QCOMPARE(backend.playbackRate(), rate);
      QCOMPARE(backend.position(), 1000);
      QCOMPARE(backend.duration(), 2500);
    }
    backend.stop();
    backend.setSource({});
    QCOMPARE(backend.playbackRate(), 2.0);
    backend.setSource(QUrl::fromLocalFile(path));
    QTRY_COMPARE(backend.duration(), 2500);
    QCOMPARE(backend.playbackRate(), 2.0);
  }
  void embeddedTagsSurviveManagedImportAndUserEdits() {
    QTemporaryDir dir;
    const auto source = dir.filePath("tags.flac");
    QVERIFY(QFile::copy(QFINDTESTDATA("fixtures/tagged.flac"), source));
    const std::atomic_bool cancelled{false};
    const auto probed = readAudioFileInfo(source, cancelled);
    QVERIFY(probed.durationMs > 0);
    QCOMPARE(probed.metadata.title, QString("夜空 02"));
    QCOMPARE(probed.metadata.artist, QString("示例歌手"));
    QCOMPARE(probed.metadata.album, QString("月光专辑"));
    QCOMPARE(probed.metadata.albumArtist, QString("Various Artists"));
    QCOMPARE(probed.metadata.genre, QString("Pop"));
    QCOMPARE(probed.metadata.year, 2024);
    QCOMPARE(probed.metadata.trackNumber, 2);
    QCOMPARE(probed.metadata.discNumber, 1);
    QCOMPARE(probed.metadata.lyrics, QString("Embedded lyrics"));
    StoreFixture store(dir.filePath("songs.sqlite3"));
    MusicDirectory music(store.db, dir.filePath("music"));
    ImportExecutor imports;
    imports.setRoot(music.directory());
    imports.setMapper([&](const ImportedFile &file) { return music.manage(file); });
    std::optional<ImportedFile> file;
    imports.inspect(source, [&](Result<ImportedFile> result) { QVERIFY(result); file = result.value(); });
    QTRY_VERIFY(file.has_value());
    QCOMPARE(file->metadata.albumArtist, probed.metadata.albumArtist);
    auto saved = store.library.importFile(*file);
    QVERIFY(saved);
    QCOMPARE(saved.value().album, probed.metadata.album);
    QCOMPARE(saved.value().trackNumber, 2);
    QCOMPARE(saved.value().lyrics, probed.metadata.lyrics);
    const auto before = file->hash;
    MetadataPatch patch;
    patch.title = "Manual title";
    patch.album = "Manual album";
    patch.genre = QString("");
    patch.trackNumber = 8;
    QVERIFY(store.library.update(saved.value().id, patch));
    auto repeated = store.library.importFile(*file);
    QVERIFY(repeated);
    QCOMPARE(repeated.value().id, saved.value().id);
    QCOMPARE(repeated.value().customTitle, QString("Manual title"));
    QCOMPARE(repeated.value().album, QString("Manual album"));
    QCOMPARE(repeated.value().genre, QString());
    QCOMPARE(repeated.value().trackNumber, 8);
    QFile original(source);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(hash(original.readAll()), before);
    StoreFixture restored(dir.filePath("songs.sqlite3"));
    QCOMPARE(restored.songById(saved.value().id)->album, QString("Manual album"));
    QCOMPARE(restored.songById(saved.value().id)->albumArtist, QString("Various Artists"));
    QCOMPARE(restored.songById(saved.value().id)->year, 2024);
    const std::atomic_bool stopped{true};
    QCOMPARE(readAudioFileInfo(source, stopped).metadata.title, QString());
    QCOMPARE(readAudioFileInfo(dir.filePath("missing.flac"), cancelled).durationMs, 0);
  }

  void metadataBatchIsAtomicAndKeepsPerSongFields() {
    QTemporaryDir dir;
    StoreFixture store(dir.filePath("songs.sqlite3"));
    auto a = store.getOrCreateSong("a", "/a.flac", "A", "First");
    auto b = store.getOrCreateSong("b", "/b.flac", "B", "Second");
    QVERIFY(a && b);
    QVERIFY(store.updateMetadata(a->id, "A", "First", "Lyrics A", QStringList{"Keep", "Old"}));
    QVERIFY(store.updateMetadata(b->id, "B", "Second", "Lyrics B", QStringList{"Other"}));
    QSignalSpy changed(&store.library, &LibraryService::metadataBatchChanged);
    MetadataPatch patch;
    patch.album = "Shared album";
    patch.albumArtist = "Various Artists";
    patch.year = 2025;
    patch.tags = QStringList{"New"};
    QSqlQuery query(store.db.database());
    QVERIFY(query.exec(QString("CREATE TRIGGER reject_batch BEFORE UPDATE ON songs WHEN NEW.id=%1 BEGIN SELECT RAISE(FAIL,'rejected'); END").arg(b->id)));
    QVERIFY(!store.library.updateMany({a->id, b->id}, patch, TagEditMode::Add));
    QCOMPARE(store.songById(a->id)->album, QString());
    QCOMPARE(store.tags().size(), 3);
    QCOMPARE(changed.count(), 0);
    QVERIFY(query.exec("DROP TRIGGER reject_batch"));
    QVERIFY(!store.library.updateMany({a->id, 999999}, patch));
    QVERIFY(!store.library.updateMany({a->id, a->id}, patch));
    QVERIFY(store.library.updateMany({a->id, b->id}, patch, TagEditMode::Add));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(store.songById(a->id)->customTitle, QString("A"));
    QCOMPARE(store.songById(b->id)->lyrics, QString("Lyrics B"));
    QCOMPARE(store.songById(b->id)->albumArtist, QString("Various Artists"));
    QCOMPARE(store.library.tagsFor(a->id).size(), 3);
    patch = {};
    patch.tags = QStringList{"OLD"};
    QVERIFY(store.library.updateMany({a->id, b->id}, patch, TagEditMode::Remove));
    QCOMPARE(store.library.tagsFor(a->id).size(), 2);
    QCOMPARE(store.library.tagsFor(b->id).size(), 2);
    patch.tags = QStringList{};
    patch.album = QString("");
    QVERIFY(store.library.updateMany({a->id, b->id}, patch));
    QCOMPARE(store.library.tagsFor(a->id).size(), 0);
    QCOMPARE(store.songById(a->id)->album, QString());
    QCOMPARE(store.songById(a->id)->year, 2025);
  }

  void restoredPlaybackAndEventsUseStoredDuration() {
    QTemporaryDir dir;
    const auto database = dir.filePath("db.sqlite3");
    const auto firstPath = dir.filePath("first.wav");
    const auto secondPath = dir.filePath("second.wav");
    write(firstPath, wav(1500));
    write(secondPath, wav(2500));
    {
      StoreFixture store(database);
      auto first = store.library.importFile({firstPath, "first", "First", 1500}, "First", "Artist");
      auto second = store.library.importFile({secondPath, "second", "Second", 2500}, "Second", "Artist");
      QVERIFY(first);
      QVERIFY(second);
      QVERIFY(store.saveQueue({{{firstPath, first.value().id}, {secondPath, second.value().id}}, 0}));
    }
    StoreFixture restored(database);
    QueueService queue(restored.queueRepo, restored.songRepo, restored.db);
    DurationAudio audio;
    PlayerEngine player(audio, queue);
    QVERIFY(audio.source().isEmpty());
    QCOMPARE(toJson(player.snapshot())["duration"].toInteger(), 1500);
    QSignalSpy progress(&player, &PlayerEngine::positionChanged);
    QSignalSpy duration(&player, &PlayerEngine::durationChanged);
    QVERIFY(player.play());
    audio.seek(500);
    QCOMPARE(progress.last().at(1).toLongLong(), 1500);
    audio.decodedDuration = 0;
    emit audio.durationChanged(0);
    QCOMPARE(duration.last().first().toLongLong(), 1500);
    QVERIFY(player.next());
    QCOMPARE(toJson(player.snapshot())["duration"].toInteger(), 2500);
    audio.decodedDuration = 9000;
    emit audio.durationChanged(9000);
    audio.seek(700);
    QCOMPARE(duration.last().first().toLongLong(), 2500);
    QCOMPARE(progress.last().at(1).toLongLong(), 2500);
    QVERIFY(player.clear());
    QCOMPARE(player.snapshot().duration, 0);
  }

  void durationBackfillUpdatesCurrentPlaybackAndLyricsWithoutDecoder() {
    QTemporaryDir dir;
    const auto path = dir.filePath("song.wav");
    write(path, wav());
    StoreFixture store(dir.filePath("db.sqlite3"));
    auto song = store.getOrCreateSong("hash", path, "Song", "Artist");
    QVERIFY(song);
    QVERIFY(store.saveQueue({{{path, song->id}}, 0}));
    QueueService queue(store.queueRepo, store.songRepo, store.db);
    DurationAudio audio;
    PlayerEngine player(audio, queue);
    connect(&store.library, &LibraryService::durationUpdated, &player, &PlayerEngine::metadataUpdated);
    auto *provider = new StoredDurationProvider;
    auto *service = new LyricsService({provider}, std::make_unique<LyricsStorage>(dir.filePath("cache")));
    provider->setParent(service);
    LyricsController lyrics(player, service);
    QTRY_COMPARE(lyrics.snapshot().state, QString("waiting_metadata"));
    QCOMPARE(player.snapshot().duration, 0);
    QSignalSpy progress(&player, &PlayerEngine::positionChanged);
    audio.seek(400);
    QCOMPARE(progress.last().at(1).toLongLong(), 0);
    QSignalSpy tracks(&player, &PlayerEngine::trackChanged);
    QVERIFY(store.library.backfillDuration(song->id, "hash", 1500));
    QCOMPARE(tracks.count(), 1);
    QCOMPARE(player.snapshot().duration, 1500);
    QTRY_COMPARE(lyrics.snapshot().state, QString("ready"));
    QVERIFY(lyrics.snapshot().document);
    QCOMPARE(lyrics.snapshot().document->matched.durationMs, 1500);
    QVERIFY(audio.source().isEmpty());
    QCOMPARE(queue.queue().currentIndex(), 0);
    QCOMPARE(queue.queue().size(), 1);
  }

  void restoredLyricsUseStoredMetadataWithoutDecoder() {
    QTemporaryDir dir;
    StoreFixture store(dir.filePath("db.sqlite3"));
    const auto path = dir.filePath("song.wav");
    write(path, wav());
    auto song = store.library.importFile({path, "hash", "Song", 1500}, "Song", "Artist");
    QVERIFY(song);
    QVERIFY(store.saveQueue({{{path, song.value().id}}, 0}));
    QueueService queue(store.queueRepo, store.songRepo, store.db);
    DurationAudio audio;
    PlayerEngine player(audio, queue);
    auto *provider = new StoredDurationProvider;
    auto *service = new LyricsService({provider}, std::make_unique<LyricsStorage>(dir.filePath("cache")));
    provider->setParent(service);
    LyricsController lyrics(player, service);
    QTRY_COMPARE(lyrics.snapshot().state, QString("ready"));
    QVERIFY(lyrics.snapshot().document);
    QCOMPARE(lyrics.snapshot().document->matched.durationMs, 1500);
    QVERIFY(audio.source().isEmpty());
  }

  // The generated WAV has a known duration without depending on an external media fixture.
  // Import probes the source while the managed library records the linked audio identity.
  // Missing, cancelled and invalid probes return unknown duration rather than failing import.
  // Reimporting an unknown value must not erase an already persisted positive duration.
  // Hashing the source again checks that probing and registration did not alter its bytes.
  // The queue should consume stored metadata rather than reopening audio in the UI thread.
  void probesOffThreadAndPreservesManagedFile() {
    QTemporaryDir dir;
    const auto path = dir.filePath("原曲.wav");
    const auto bytes = wav();
    write(path, bytes);
    StoreFixture store(dir.filePath("db.sqlite3"));
    MusicDirectory music(store.db, dir.filePath("music"));
    ImportExecutor imports;
    imports.setRoot(music.directory());
    imports.setMapper(
        [&](const ImportedFile &file) { return music.manage(file); });
    std::optional<ImportedFile> inspected;
    imports.inspect(path, [&](Result<ImportedFile> result) {
      QVERIFY(result);
      inspected = result.value();
    });
    QTRY_VERIFY(inspected.has_value());
    QCOMPARE(inspected->durationMs, 1500);
    QCOMPARE(inspected->hash, hash(bytes));
    QVERIFY(!QFileInfo(inspected->path).isSymLink());
    QVERIFY(inspected->path != path);
    const auto imported = store.library.importFile(*inspected);
    QVERIFY(imported);
    QCOMPARE(imported.value().durationMs, 1500);
    QCOMPARE(toJson(store.library.snapshot())["songs"]
                 .toArray()[0]
                 .toObject()["duration_ms"]
                 .toInteger(),
             1500);
    std::atomic_bool cancelled{true};
    QCOMPARE(readAudioDuration(path, cancelled), 0);
    cancelled = false;
    const auto corrupt = dir.filePath("broken.mp3");
    write(corrupt, "not audio");
    QCOMPARE(readAudioDuration(corrupt, cancelled), 0);
    QCOMPARE(readAudioDuration(dir.filePath("missing.wav"), cancelled), 0);
    QFile original(path);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), bytes);
    // A failed probe during re-import must not erase the known value.
    inspected->durationMs = 0;
    const auto repeated = store.library.importFile(*inspected);
    QVERIFY(repeated);
    QCOMPARE(repeated.value().durationMs, 1500);
  }
  // An old songs table intentionally omits the duration column.
  // Schema repair must retain its existing identity and user-edited metadata.
  // Backfilling duration should update that row instead of creating a replacement song.
  // Reopening the database verifies persistence beyond the current repository instance.
  // The migration and backfill have separate responsibilities and are exercised together here.
  void migratesLegacySchemaAndPreservesDurationAcrossRestart() {
    QTemporaryDir dir;
    const auto path = dir.filePath("db.sqlite3");
    {
      auto db = QSqlDatabase::addDatabase("QSQLITE", "duration-legacy");
      db.setDatabaseName(path);
      QVERIFY(db.open());
      QSqlQuery query(db);
      QVERIFY(query.exec(
          "CREATE TABLE songs (id INTEGER PRIMARY KEY, hash TEXT UNIQUE NOT "
          "NULL, "
          "first_path TEXT NOT NULL, custom_title TEXT NOT NULL DEFAULT '', "
          "artist TEXT NOT NULL DEFAULT '', lyrics TEXT NOT NULL DEFAULT '', "
          "created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT "
          "CURRENT_TIMESTAMP)"));
      QVERIFY(query.exec(
          "INSERT INTO songs(id,hash,first_path,custom_title,artist,lyrics) "
          "VALUES(7,'legacy','/music/song.wav','Saved "
          "title','Artist','Lyrics')"));
    }
    QSqlDatabase::removeDatabase("duration-legacy");
    {
      StoreFixture store(path);
      QVERIFY2(store.isReady(), qPrintable(store.errorString()));
      QCOMPARE(store.songById(7)->durationMs, 0);
      QVERIFY(store.library.backfillDuration(7, "legacy", 192000));
      const auto song = store.songById(7);
      QCOMPARE(song->durationMs, 192000);
      QCOMPARE(song->customTitle, QString("Saved title"));
      QCOMPARE(song->artist, QString("Artist"));
      QCOMPARE(song->lyrics, QString("Lyrics"));
    }
    StoreFixture restored(path);
    QVERIFY(restored.isReady());
    QCOMPARE(restored.songById(7)->durationMs, 192000);
    QCOMPARE(restored.songs().first().durationMs, 192000);
  }
  // Legacy paths and managed links must both be eligible for duration probing.
  // Unavailable files and content whose hash has changed remain unknown.
  // Duration enrichment cannot replace custom lyrics, tags or collection membership.
  // Existing queue records should receive the updated metadata for their song identity.
  // Another scan must be idempotent once the positive duration has been persisted.
  // The assertions separate metadata repair from queue construction and playback selection.
  void backfillsLegacyAndManagedSongsWithoutChangingCollections() {
    QTemporaryDir dir;
    const auto path = dir.filePath("outside.wav");
    const auto managedRoot = dir.filePath("music");
    write(path, wav());
    write(managedRoot + "/new.wav", wav(2500));
    StoreFixture store(dir.filePath("db.sqlite3"));
    const auto song =
        store.getOrCreateSong(hash(wav()), path, "Custom", "Artist");
    QVERIFY(song);
    QVERIFY(store.updateMetadata(song->id, "Custom", "Artist", "Lyrics",
                                 QStringList{"Favorite"}));
    const auto missing =
        store.getOrCreateSong("missing", dir.filePath("missing.wav"));
    QVERIFY(missing);
    const auto changed = dir.filePath("changed.wav");
    write(changed, wav(3500));
    const auto stale = store.getOrCreateSong(hash(wav(4500)), changed);
    QVERIFY(stale);
    const int playlist = store.createPlaylist("Saved list");
    QVERIFY(store.addPlaylistSong(playlist, {path, song->id}));
    QVERIFY(store.saveQueue({{{path, song->id}}, 0}));
    QueueService queue(store.queueRepo, store.songRepo, store.db);
    connect(&store.library, &LibraryService::durationUpdated, &queue,
            &QueueService::updateMetadata);
    MusicDirectory music(store.db, managedRoot);
    const auto owned = music.manage({managedRoot + "/new.wav", hash(wav(2500))});
    QVERIFY(owned);
    QVERIFY(store.library.importFile(owned.value()));
    ImportExecutor imports;
    CommandScheduler commands;
    LibraryScanner scanner(imports, music, store.library, commands);
    QSignalSpy finished(&scanner, &LibraryScanner::finished);
    QVERIFY(scanner.start());
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(store.songById(song->id)->durationMs, 1500);
    QCOMPARE(store.songById(song->id)->firstPath, path);
    QCOMPARE(store.songById(song->id)->customTitle, QString("Custom"));
    QCOMPARE(store.songById(song->id)->lyrics, QString("Lyrics"));
    QCOMPARE(store.songTags()[song->id].first().name, QString("Favorite"));
    QCOMPARE(store.songById(missing->id)->durationMs, 0);
    QCOMPARE(store.songById(stale->id)->durationMs, 0);
    QCOMPARE(store.songRepo.songs().last().durationMs, 2500);
    QCOMPARE(store.loadQueue().currentIndex, 0);
    QCOMPARE(store.loadQueue().items.size(), 1);
    QCOMPARE(store.loadQueue().items.first().path, path);
    QCOMPARE(store.playlistById(playlist)->items.first().songId, song->id);
    QCOMPARE(
        queueJson(queue.queue()).first().toObject()["duration_ms"].toInteger(),
        1500);
    QVERIFY(scanner.start());
    QTRY_COMPARE(finished.count(), 2);
    QCOMPARE(store.songs().size(), 4);
    QCOMPARE(store.songById(song->id)->durationMs, 1500);
    imports.shutdown();
  }
  // A worker result belongs to the hash observed when its probe was scheduled.
  // The wrong hash therefore cannot enrich a different or subsequently replaced song.
  // A rejecting SQL trigger must leave both stored duration and change notifications untouched.
  // A valid write emits a change only when it actually fills an unknown duration.
  // Existing positive values are not replaced by later probe results.
  // Deleting the song before completion must not resurrect it or clear scan suppression.
  void rejectsStaleResultsAndRollsBackFailedWrites() {
    QTemporaryDir dir;
    StoreFixture store(dir.filePath("db.sqlite3"));
    auto song = store.getOrCreateSong("hash", "/music/song.wav");
    QVERIFY(song);
    QVERIFY(store.library.backfillDuration(song->id, "wrong-hash", 1000));
    QCOMPARE(store.songById(song->id)->durationMs, 0);
    QSignalSpy changed(&store.library, &LibraryService::durationUpdated);
    QSqlQuery query(store.db.database());
    QVERIFY(query.exec(
        "CREATE TRIGGER fail_duration BEFORE UPDATE OF duration_ms ON songs "
        "BEGIN SELECT RAISE(ABORT,'fixture failure'); END"));
    QVERIFY(!store.library.backfillDuration(song->id, "hash", 1000));
    QCOMPARE(store.songById(song->id)->durationMs, 0);
    QCOMPARE(changed.count(), 0);
    QVERIFY(query.exec("DROP TRIGGER fail_duration"));
    QVERIFY(store.library.backfillDuration(song->id, "hash", 1000));
    QCOMPARE(changed.count(), 1);
    QVERIFY(store.library.backfillDuration(song->id, "hash", 2000));
    QCOMPARE(store.songById(song->id)->durationMs, 1000);
    QVERIFY(store.songRepo.erase(song->id));
    QVERIFY(store.library.backfillDuration(song->id, "hash", 1000));
    QVERIFY(store.songs().isEmpty());
    QVERIFY(query.exec("SELECT 1 FROM scan_ignored WHERE hash='hash'"));
    QVERIFY(query.next());
  }
};
QTEST_GUILESS_MAIN(AudioDurationTest)
#include "audio_duration_test.moc"
