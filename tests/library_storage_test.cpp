#include "app_paths.h"
#include "application/extensions/music_service.h"
#include "application/library/collection_service.h"
#include "domain/lyrics/krc_parser.h"
#include "infrastructure/library/audio_reference.h"
#include "infrastructure/library/import_executor.h"
#include "infrastructure/library/library_archive.h"
#include "infrastructure/library/music_directory.h"
#include "runtime/backend_runtime.h"
#include "storage/resource_repository.h"
#include "support/audio_backend.h"
#include "support/store_fixture.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>
using namespace nekotune;
namespace {
class FakeAudio final : public TestAudioBackend {
  public:
    QUrl current;
    qint64 clock = 0;
    double level = 1;
    void setSource(const QUrl &url) override { current = url; }
    QUrl source() const override { return current; }
    void play() override { emit stateChanged(PlayerState::Playing); }
    void pause() override { emit stateChanged(PlayerState::Paused); }
    void stop() override { emit stateChanged(PlayerState::Stopped); }
    void seek(qint64 value) override { clock = value; }
    void setVolume(double value) override { level = value; }
    qint64 position() const override { return clock; }
    qint64 duration() const override { return 1000; }
    double volume() const override { return level; }
    AudioMetadata metadata() const override { return {}; }
};
class DeferredExtensions final : public IExtensionBackend {
  public:
    Completion download;
    QVariantMap callback;
    void request(const QString &method, const QVariantMap &params, Completion done) override {
        if (method == "extensions.download_audio")
            download = std::move(done);
        else {
            if (method == "extensions.download_completed")
                callback = params;
            done(QVariantMap{});
        }
    }
    QVariantMap snapshot() const override { return {}; }
    void publish(const QVariantMap &) override {}
};
class DeferredInspector final : public IFileInspector {
  public:
    Completion pending;
    void inspect(const QString &, Completion done) override { pending = std::move(done); }
    void shutdown() override {}
};
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
void write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(bytes), bytes.size());
}
QByteArray read(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
std::optional<Result<ImportedFile>> inspect(ImportExecutor &imports, const QString &path) {
    std::optional<Result<ImportedFile>> result;
    imports.inspect(path, [&](auto value) { result = std::move(value); });
    QElapsedTimer timer;
    timer.start();
    while (!result && timer.elapsed() < 10000) {
        QCoreApplication::processEvents();
        QTest::qWait(1);
    }
    return result;
}
class Rpc {
  public:
    QLocalSocket socket;
    QByteArray buffer;
    int id = 0;
    bool connect(const QString &path) {
        if (socket.state() == QLocalSocket::ConnectedState)
            return true;
        if (socket.state() == QLocalSocket::UnconnectedState)
            socket.connectToServer(path);
        if (socket.waitForConnected(3000))
            return true;
        socket.abort();
        return false;
    }
    QJsonObject call(const QString &method, const QJsonObject &params = {}) {
        const int request = ++id;
        socket.write(QJsonDocument(QJsonObject{{"id", request}, {"method", method}, {"params", params}})
                         .toJson(QJsonDocument::Compact) +
                     '\n');
        socket.flush();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 10000) {
            buffer += socket.readAll();
            while (buffer.contains('\n')) {
                auto line = buffer.left(buffer.indexOf('\n'));
                buffer.remove(0, line.size() + 1);
                auto value = QJsonDocument::fromJson(line).object();
                if (value["id"].toInt() == request)
                    return value;
            }
            socket.waitForReadyRead(10);
            QCoreApplication::processEvents();
        }
        return {};
    }
    QJsonObject wait(const QString &task) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 15000) {
            auto value = call("library.task", {{"task", task}})["data"].toObject();
            if (value["state"] != "running" && value["state"] != "switching")
                return value;
            QTest::qWait(10);
        }
        return {};
    }
};
} // namespace
class LibraryStorageTest : public QObject {
    Q_OBJECT
    QTemporaryDir temp;
    QString root, db, source;
  private slots:
    void init() {
        root = temp.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
        db = root + "/config/nekotune.sqlite3";
        source = temp.filePath("external-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".flac");
        qputenv("NEKOTUNE_HOME", root.toUtf8());
        qputenv("NEKOTUNE_DB_PATH", db.toUtf8());
        qputenv("NEKOTUNE_SOCKET", (root + "/backend.sock").toUtf8());
        QDir().mkpath(root);
    }
    void ownsBytesSidecarsAndRelativeCollectionPaths() {
        StoreFixture store(db);
        QVERIFY(store.isReady());
        MusicDirectory music(store.db, root);
        ImportExecutor imports;
        imports.setRoot(root);
        imports.setMapper([&](const auto &f) { return music.manage(f); });
        write(source, "audio bytes");
        const auto base = QFileInfo(source).absolutePath() + '/' + QFileInfo(source).completeBaseName();
        write(base + ".lrc", "[00:00.00]lyrics");
        write(base + ".txt", "plain lyrics");
        QImage image(4, 4, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&buffer, "PNG"));
        write(base + ".png", png);
        auto file = inspect(imports, source);
        QVERIFY(file && *file);
        auto saved = store.library.importFile(file->value(), "名字", "歌手");
        QVERIFY(saved);
        const int id = saved.value().id;
        ResourceRepository assets(store.db);
        const auto audio = assets.audioPath(id);
        QVERIFY(audio.startsWith(root + "/media/"));
        QVERIFY(!QFileInfo(audio).isSymLink());
        QCOMPARE(read(audio), QByteArray("audio bytes"));
        QVERIFY(assets.lyrics(id, "local"));
        QCOMPARE(assets.lyrics(id, "local")->plainLyrics, QString("plain lyrics"));
        QCOMPARE(read(assets.coverPath(id)), png);
        const auto playlist = store.createPlaylist("test");
        QVERIFY(store.addPlaylistSong(playlist, {audio, id}));
        QVERIFY(store.saveQueue({{{audio, id}, {audio, id}}, 1}));
        QSqlQuery q(store.db.database());
        QVERIFY(q.exec("SELECT path FROM queue_items"));
        while (q.next())
            QVERIFY(q.value(0).toString().isEmpty());
        QVERIFY(QFile::remove(source));
        QVERIFY(QFile::remove(base + ".lrc"));
        QVERIFY(QFile::remove(base + ".txt"));
        QVERIFY(QFile::remove(base + ".png"));
        QCOMPARE(store.library.availablePath(id), audio);
        QCOMPARE(store.loadQueue().items.size(), 2);
        QCOMPARE(store.loadQueue().currentIndex, 1);
        write(source + ".mp3", "audio bytes");
        auto second = inspect(imports, source + ".mp3");
        QVERIFY(second && *second);
        QCOMPARE(second->value().path, audio);
        auto duplicate = store.library.importFile(second->value());
        QVERIFY(duplicate);
        QCOMPARE(duplicate.value().id, id);
        QCOMPARE(store.songById(id)->customTitle, QString("名字"));
    }
    void copiedFilePersistsWithVerifiedBytes_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<bool>("symlink");
        QTest::newRow("empty") << QByteArray("") << false;
        QTest::newRow("small") << QByteArray("audio\0bytes", 11) << false;
        QTest::newRow("multiple-chunks") << QByteArray(3 * 1024 * 1024 + 17, 'a') << false;
        QTest::newRow("symlink-empty") << QByteArray("") << true;
        QTest::newRow("symlink-small") << QByteArray("audio\0bytes", 11) << true;
        QTest::newRow("symlink-multiple-chunks") << QByteArray(3 * 1024 * 1024 + 17, 'a') << true;
    }
    void copiedFilePersistsWithVerifiedBytes() {
        QFETCH(QByteArray, bytes);
        QFETCH(bool, symlink);
        write(source, bytes);
        auto input = source;
        if (symlink) {
            input = root + "/source.flac";
            if (!createAudioSymlink(source, input))
                QSKIP("Native symlinks unavailable");
        }
        const auto target = root + "/copy.flac";
        const auto copied = LibraryFileIO::copyFile(input, target);
        QVERIFY2(copied, qPrintable(copied.error().message));
        QCOMPARE(copied.value(), hash(bytes));
        QVERIFY(QFileInfo::exists(target));
        QCOMPARE(read(target), bytes);
        QCOMPARE(read(source), bytes);
        if (symlink)
            QVERIFY(QFileInfo(input).isSymLink());
        QVERIFY(QDir(root).entryList({"*.copy-*"}, QDir::Files).isEmpty());
    }
    void copyRejectsRetargetedSymlink() {
        write(source, QByteArray(2 * 1024 * 1024, 'a'));
        const auto replacement = root + "/replacement.flac";
        write(replacement, read(source));
        QFile replacementFile(replacement);
        QVERIFY(replacementFile.open(QIODevice::ReadWrite));
        QVERIFY(replacementFile.setFileTime(QFileInfo(source).lastModified(), QFileDevice::FileModificationTime));
        replacementFile.close();
        const auto link = root + "/source.flac";
        if (!createAudioSymlink(source, link))
            QSKIP("Native symlinks unavailable");
        const auto target = root + "/copy.flac";
        bool retargeted = false;
        const auto copied = LibraryFileIO::copyFile(link, target, nullptr, [&](qint64) {
            if (retargeted)
                return;
            retargeted = true;
            QVERIFY(QFile::remove(link));
            QVERIFY(createAudioSymlink(replacement, link));
        });
        QVERIFY(retargeted);
        QVERIFY(!copied);
        QVERIFY(copied.error().message.contains("changed"));
        QVERIFY(!QFileInfo::exists(target));
        QCOMPARE(read(source), read(replacement));
        QVERIFY(QDir(root).entryList({"*.copy-*"}, QDir::Files).isEmpty());
    }
    void copyPreservesDestinationCreatedDuringStreaming() {
        write(source, QByteArray(2 * 1024 * 1024, 'a'));
        const auto target = root + "/copy.flac";
        bool created = false;
        const auto copied = LibraryFileIO::copyFile(source, target, nullptr, [&](qint64) {
            if (created)
                return;
            created = true;
            write(target, "do not overwrite");
        });
        QVERIFY(created);
        QVERIFY(!copied);
        QCOMPARE(copied.error().code, ErrorCode::Io);
        QCOMPARE(read(target), QByteArray("do not overwrite"));
        QVERIFY(QDir(root).entryList({"*.copy-*"}, QDir::Files).isEmpty());
    }
    void cancelledCopyAndConflictingObjectNeverOverwrite() {
        write(source, QByteArray(1024 * 1024, 'a'));
        std::atomic_bool cancel{true};
        const auto target = root + "/copy.flac";
        auto copied = LibraryFileIO::copyFile(source, target, &cancel);
        QVERIFY(!copied);
        QVERIFY(!QFileInfo::exists(target));
        StoreFixture store(db);
        MusicDirectory music(store.db, root);
        const auto h = hash(read(source));
        const auto path = root + "/media/" + h.left(2) + '/' + h + ".flac";
        write(path, "unrelated bytes");
        auto imported = music.manage({source, h});
        QVERIFY(!imported);
        QCOMPARE(read(path), QByteArray("unrelated bytes"));
        write(target, "do not overwrite");
        cancel = false;
        QVERIFY(!LibraryFileIO::copyFile(source, target, &cancel));
        QCOMPARE(read(target), QByteArray("do not overwrite"));
    }
    void copyDetectsSourceMutationAndCancellationDuringStreaming() {
        write(source, QByteArray(3 * 1024 * 1024, 'a'));
        std::atomic_bool cancel{false};
        const auto target = root + "/copy.flac";
        bool changed = false;
        auto copied = LibraryFileIO::copyFile(source, target, &cancel, [&](qint64) {
            if (changed)
                return;
            changed = true;
            QFile f(source);
            QVERIFY(f.open(QIODevice::Append));
            QCOMPARE(f.write("changed"), qint64(7));
        });
        QVERIFY(!copied);
        QVERIFY(copied.error().message.contains("changed"));
        QVERIFY(!QFileInfo::exists(target));
        copied = LibraryFileIO::copyFile(source, target, &cancel, [&](qint64) { cancel = true; });
        QVERIFY(!copied);
        QCOMPARE(copied.error().code, ErrorCode::Cancelled);
        QVERIFY(!QFileInfo::exists(target));
        QVERIFY(QDir(root).entryList({"*.copy-*"}, QDir::Files).isEmpty());
    }
    void remoteDownloadSharesObjectWithoutMergingSongs() {
        write(source, "shared");
        StoreFixture store(db);
        MusicDirectory music(store.db, root);
        auto file = music.manage({source, hash("shared")});
        QVERIFY(file);
        auto local = store.library.importFile(file.value(), "Local");
        QVERIFY(local);
        SongMetadata remote;
        remote.providerId = "test/music";
        remote.providerTrackId = "42";
        remote.customTitle = "Online";
        auto before = store.songRepo.getOrCreateRemote(remote);
        QVERIFY(before);
        auto downloaded = store.library.importDownloaded(file.value(), remote);
        QVERIFY(downloaded);
        QCOMPARE(downloaded.value().id, before->id);
        QVERIFY(downloaded.value().hasLocalAudio);
        QVERIFY(!downloaded.value().isRemote());
        QCOMPARE(store.songs().size(), 2);
        QCOMPARE(ResourceRepository(store.db).audioPath(local.value().id),
                 ResourceRepository(store.db).audioPath(before->id));
        {
            QueueService queue(store.queueRepo, store.songRepo, store.db);
            FakeAudio audio;
            PlayerEngine player(audio, queue);
            CollectionService collections(store.songRepo, store.queueRepo, store.playlistRepo, store.db,
                                          store.library, queue, player, &music);
            QVERIFY(collections.enqueueRemote(remote, true));
            QCOMPARE(audio.current, QUrl::fromLocalFile(file.value().path));
            QCOMPARE(queue.queue().at(0).metadata.id, before->id);
            QVERIFY(store.library.containsAudioHash(hash("shared")));
            QVERIFY(store.saveQueue({{}, -1}));
        }
        {
            Transaction tx(store.db);
            QVERIFY(tx);
            QVERIFY(store.songRepo.erase(local.value().id));
            auto removal = music.stageRemoval({hash("shared")});
            QVERIFY(removal);
            QVERIFY(tx.commit());
            QVERIFY(removal.value()->commit().isEmpty());
        }
        QVERIFY(QFileInfo::exists(file.value().path));
        {
            Transaction tx(store.db);
            QVERIFY(tx);
            QVERIFY(store.songRepo.erase(before->id));
            auto removal = music.stageRemoval({hash("shared")});
            QVERIFY(removal);
            QVERIFY(!QFileInfo::exists(file.value().path));
            // Leaving both handles uncommitted restores SQL and the staged file.
        }
        QVERIFY(QFileInfo::exists(file.value().path));
        QVERIFY(store.songById(before->id));
    }
    void cancelledDownloadDoesNotRegisterSong_data() {
        QTest::addColumn<bool>("inspecting");
        QTest::newRow("during-download") << false;
        QTest::newRow("during-inspection") << true;
    }
    void cancelledDownloadDoesNotRegisterSong() {
        QFETCH(bool, inspecting);
        write(source, "cancel download");
        StoreFixture store(db);
        MusicDirectory directory(store.db, root);
        auto file = directory.manage({source, hash("cancel download")});
        QVERIFY(file);
        DeferredExtensions backend;
        ExtensionService extensions(backend);
        DeferredInspector inspector;
        CommandScheduler commands;
        MusicService service(extensions, store.library, inspector, commands,
                             [&](const auto &, const auto &) -> Result<QString> { return source; });
        SongMetadata remote;
        remote.providerId = "test/music";
        remote.providerTrackId = "cancelled";
        QSignalSpy events(&service, &MusicService::taskChanged);
        const auto task = service.download(remote.providerId, remote);
        QVERIFY(backend.download);
        if (inspecting) {
            auto complete = std::move(backend.download);
            complete(QVariantMap{{"path", source}});
            QTRY_VERIFY(inspector.pending);
        }
        service.cancelDownload(task, [](auto result) { QVERIFY(result); });
        if (inspecting) {
            auto complete = std::move(inspector.pending);
            complete(file.value());
        } else {
            auto complete = std::move(backend.download);
            complete(QVariantMap{{"path", source}});
        }
        QVERIFY(store.songs().isEmpty());
        QVERIFY(backend.callback.isEmpty());
        QCOMPARE(events.last().first().toMap()["state"].toString(), QString("cancelled"));
        QVERIFY(!service.downloadActive());
    }
    void downloadCallbackUsesStagingAndKeepsAudioOnAssetFailure() {
        write(source, "download bytes");
        StoreFixture store(db);
        MusicDirectory directory(store.db, root);
        auto file = directory.manage({source, hash("download bytes")});
        QVERIFY(file);
        DeferredExtensions backend;
        ExtensionService extensions(backend);
        DeferredInspector inspector;
        CommandScheduler commands;
        MusicService service(extensions, store.library, inspector, commands,
                             [&](const auto &, const auto &) -> Result<QString> { return source; });
        SongMetadata remote;
        remote.providerId = "test/music";
        remote.providerTrackId = "callback";
        auto song = store.songRepo.getOrCreateRemote(remote);
        QVERIFY(song);
        QSignalSpy events(&service, &MusicService::taskChanged);
        service.download(remote.providerId, remote);
        auto downloaded = std::move(backend.download);
        downloaded(QVariantMap{{"path", source}});
        QTRY_VERIFY(inspector.pending);
        auto inspected = std::move(inspector.pending);
        inspected(file.value());
        QTRY_VERIFY(!backend.callback.isEmpty());
        QCOMPARE(backend.callback["path"].toString(), source);
        QCOMPARE(backend.callback["song_id"].toInt(), song->id);
        QVERIFY(backend.callback["path"].toString() != file.value().path);
        QTRY_VERIFY(inspector.pending);
        auto assets = std::move(inspector.pending);
        assets(failure("Invalid cover image"));
        QTRY_VERIFY(!service.downloadActive());
        const auto final = events.last().first().toMap();
        QCOMPARE(final["state"].toString(), QString("finished"));
        QCOMPARE(final["path"].toString(), file.value().path);
        QCOMPARE(final["song_id"].toInt(), song->id);
        QCOMPARE(final["asset_warning"].toString(), QString("Invalid cover image"));
        QCOMPARE(ResourceRepository(store.db).audioPath(song->id), file.value().path);
        QCOMPARE(store.songs().size(), 1);
    }
    void exportPreservesAudioAndUsesSafeUniqueNames() {
        write(source, "export original");
        std::atomic_bool cancel{false};
        LyricsDocument lyrics;
        lyrics.syncedLyrics = "[00:00.00]词";
        QVector<LibraryArchive::ExportSong> songs{
            {1, "CON", "", "", source, "", 1234, lyrics, {}},
            {2, "CON", "", "", source, "", 1234, {}, {}},
            {3, "missing", "", "", "", "", 0, {}, {}},
            {4, QString(180, QChar(0x6b4c)), "歌手", "", source, "", 1234, {}, {}},
            {5, "con", "", "", source, "", 1234, {}, {}}};
        auto exported = LibraryArchive::exportSongs(songs, temp.filePath("exports"), true, cancel);
        QVERIFY(exported);
        QCOMPARE(exported.value()["exported"].toInt(), 4);
        QCOMPARE(exported.value()["skipped"].toInt(), 1);
        const QDir out(exported.value()["path"].toString());
        const auto audio = out.entryList({"*.flac"}, QDir::Files);
        QCOMPARE(audio.size(), 4);
        QVERIFY(audio[0].startsWith('_'));
        QVERIFY(audio[0] != audio[1]);
        for (const auto &name : audio) {
            QVERIFY(name.toUtf8().size() < 255);
            QCOMPARE(read(out.filePath(name)), read(source));
        }
        QVERIFY(read(out.filePath("playlist.m3u8")).contains("#EXTM3U"));
    }
    void exportEncodesParsedKrcAndPreservesOriginalKrcBytes() {
        write(source, "krc audio");
        LyricsDocument lyrics;
        lyrics.krcLyrics = "[0,1000]<0,1000,0>歌词\n";
        lyrics.syncedLyrics = "[00:00.00]歌词\n";
        std::atomic_bool cancel{false};
        LibraryArchive::ExportSong song{1, "#歌曲", "", "", source, "", 1000, lyrics, {}};
        auto first = LibraryArchive::exportSongs({song}, temp.filePath("krc-export"), true, cancel);
        QVERIFY(first);
        const QDir out(first.value()["path"].toString());
        const auto raw = read(out.filePath("#歌曲.krc"));
        QVERIFY(raw.startsWith("krc1"));
        QCOMPARE(KrcParser::read(raw).value_or(QString()), lyrics.krcLyrics);
        QCOMPARE(read(out.filePath("#歌曲.lrc")), lyrics.syncedLyrics.toUtf8());
        QVERIFY(read(out.filePath("playlist.m3u8")).contains("./#歌曲.flac"));
        song.rawKrc = raw;
        auto second = LibraryArchive::exportSongs({song}, temp.filePath("krc-export"), false, cancel);
        QVERIFY(second);
        QCOMPARE(read(QDir(second.value()["path"].toString()).filePath("#歌曲.krc")), raw);
    }
    void backupRestoreRelocatesCatalogAndRejectsTampering() {
        QString backupPath;
        int id = 0;
        std::atomic_bool cancel{false};
        write(source, "backup audio");
        {
            StoreFixture store(db);
            MusicDirectory music(store.db, root);
            auto file = music.manage({source, hash("backup audio")});
            QVERIFY(file);
            auto saved = store.library.importFile(file.value(), "Keep");
            QVERIFY(saved);
            id = saved.value().id;
            QVERIFY(store.saveQueue({{{file.value().path, id}, {file.value().path, id}}, 1}));
            const auto playlist = store.createPlaylist("order");
            QVERIFY(store.addPlaylistSong(playlist, {file.value().path, id}));
            auto backed = LibraryArchive::backup(db, root, temp.filePath("backups"), cancel);
            QVERIFY2(backed, qPrintable(backed.error().message));
            backupPath = backed.value()["path"].toString();
            QVERIFY(LibraryArchive::inspectBackup(backupPath, cancel));
            QVERIFY(!LibraryArchive::backup(db, root, root + "/nested", cancel));
        }
        const auto restoredRoot = temp.filePath("relocated"),
                   restoredDb = temp.filePath("custom-db/library.sqlite");
        QDir().mkpath(restoredRoot);
        QDir().mkpath(QFileInfo(restoredDb).absolutePath());
        auto prepared = LibraryArchive::prepareRestore(backupPath, restoredRoot, restoredDb, cancel);
        QVERIFY(prepared);
        QVERIFY(LibraryArchive::applyRestore(prepared.value(), restoredRoot, restoredDb));
        QVERIFY(LibraryArchive::recoverRestore(restoredRoot, restoredDb));
        {
            StoreFixture restored(restoredDb);
            MusicDirectory music(restored.db, restoredRoot);
            QCOMPARE(restored.songById(id)->customTitle, QString("Keep"));
            QVERIFY(restored.library.availablePath(id).startsWith(restoredRoot + "/media/"));
            QCOMPARE(read(restored.library.availablePath(id)), QByteArray("backup audio"));
            QCOMPARE(restored.loadQueue().items.size(), 2);
            QCOMPARE(restored.loadQueue().currentIndex, 1);
            QCOMPARE(restored.playlists().size(), 1);
        }
        auto manifest = LibraryFileIO::readJson(backupPath + "/manifest.json");
        QVERIFY(manifest);
        auto entries = manifest.value()["files"].toArray();
        auto entry = entries[0].toObject();
        write(backupPath + '/' + entry["path"].toString(), "corrupt");
        QVERIFY(!LibraryArchive::inspectBackup(backupPath, cancel));
        entry["path"] = "../escape";
        entries[0] = entry;
        manifest.value()["files"] = entries;
        QVERIFY(LibraryFileIO::writeJson(backupPath + "/manifest.json", manifest.value()));
        QVERIFY(!LibraryArchive::inspectBackup(backupPath, cancel));
    }
    void recoveryRollsBackInterruptedSwitch_data() {
        QTest::addColumn<int>("interrupt");
        for (int point = 0; point <= 12; ++point)
            QTest::newRow(qPrintable(QString("switch-%1").arg(point))) << point;
        QTest::newRow("interrupted-rollback") << 13;
    }
    void recoveryRollsBackInterruptedSwitch() {
        QFETCH(int, interrupt);
        write(source, "new audio");
        QString backupPath;
        std::atomic_bool cancel{false};
        {
            StoreFixture store(db);
            MusicDirectory music(store.db, root);
            auto file = music.manage({source, hash("new audio")});
            QVERIFY(file);
            QVERIFY(store.library.importFile(file.value()));
            auto backed = LibraryArchive::backup(db, root, temp.filePath("recover-backups"), cancel);
            QVERIFY(backed);
            backupPath = backed.value()["path"].toString();
        }
        write(root + "/media/old-marker", "old library bytes");
        write(root + "/artwork/old-marker", "old artwork");
        const auto originalDatabase = read(db);
        auto prepared = LibraryArchive::prepareRestore(backupPath, root, db, cancel);
        QVERIFY(prepared);
        auto journal = prepared.value();
        auto actions = journal["actions"].toArray();
        int step = 0;
        const auto persist = [&] {
            journal["actions"] = actions;
            return LibraryFileIO::writeJson(root + "/config/restore-journal.json", journal);
        };
        if (interrupt > 0) {
            journal["phase"] = "switching";
            for (int i = 0; i < actions.size(); ++i) {
                auto action = actions[i].toObject();
                action["state"] = "moving_old";
                actions[i] = action;
                QVERIFY(persist());
                if (++step == interrupt)
                    break;
                QDir().mkpath(QFileInfo(action["old"].toString()).absolutePath());
                QVERIFY(QDir().rename(action["target"].toString(), action["old"].toString()));
                if (++step == interrupt)
                    break;
                action["state"] = "moving_new";
                actions[i] = action;
                QVERIFY(persist());
                if (++step == interrupt)
                    break;
                QVERIFY(QDir().rename(action["new"].toString(), action["target"].toString()));
                if (++step == interrupt)
                    break;
                action["state"] = "done";
                actions[i] = action;
            }
            if (interrupt == 13) {
                journal["phase"] = "rolling_back";
                QVERIFY(persist());
                const auto databaseAction = actions.last().toObject();
                QVERIFY(QFile::remove(db));
                QVERIFY(QFile::rename(databaseAction["old"].toString(), db));
            }
        }
        QVERIFY(LibraryArchive::recoverRestore(root, db));
        QCOMPARE(read(root + "/media/old-marker"), QByteArray("old library bytes"));
        QCOMPARE(read(root + "/artwork/old-marker"), QByteArray("old artwork"));
        QCOMPARE(read(db), originalDatabase);
        QVERIFY(!QFileInfo::exists(root + "/config/restore-journal.json"));
        QVERIFY(LibraryArchive::recoverRestore(root, db));
    }
    void backupRejectsCorruptionButIncludesExplicitlyMissingSongs() {
        write(source, "backup safety");
        StoreFixture store(db);
        MusicDirectory music(store.db, root);
        auto file = music.manage({source, hash("backup safety")});
        QVERIFY(file);
        QVERIFY(store.library.importFile(file.value()));
        std::atomic_bool cancel{false};
        const auto destination = temp.filePath("backup-errors");
        QSqlQuery q(store.db.database());
        QVERIFY(q.exec("UPDATE audio_objects SET state='corrupt'"));
        QVERIFY(!LibraryArchive::backup(db, root, destination, cancel));
        QVERIFY(q.exec("UPDATE audio_objects SET state='ready'"));
        QVERIFY(QFile::remove(file.value().path));
        QVERIFY(!LibraryArchive::backup(db, root, destination, cancel));
        QVERIFY(q.exec("UPDATE audio_objects SET state='missing'"));
        auto backed = LibraryArchive::backup(db, root, destination, cancel);
        QVERIFY(backed);
        QCOMPARE(backed.value()["missing"].toArray().size(), 1);
        auto preview = LibraryArchive::inspectBackup(backed.value()["path"].toString(), cancel);
        QVERIFY(preview);
        QCOMPARE(preview.value()["version"].toInt(), 1);
        QCOMPARE(preview.value()["songs"].toInt(), 1);
        QCOMPARE(preview.value()["missing"].toArray().size(), 1);
    }
    void legacyReferenceMigrationKeepsIdentityAndExternalOriginal_data() {
        QTest::addColumn<QString>("layout");
        for (const auto &layout : {"reference", "owned", "symlink", "direct"})
            QTest::newRow(layout) << QString(layout);
    }
    void legacyReferenceMigrationKeepsIdentityAndExternalOriginal() {
        QFETCH(QString, layout);
        write(source, "legacy audio");
        const auto base = root + "/000001/000001";
        QDir().mkpath(QFileInfo(base).absolutePath());
        QString oldPath = source;
        if (layout == "reference")
            QVERIFY(writeAudioReference(base + ".audio.json", {source, hash("legacy audio"), "Original"}));
        else if (layout != "direct") {
            oldPath = base + ".flac";
            if (layout == "symlink") {
                if (!createAudioSymlink(source, oldPath))
                    QSKIP("Native symlinks unavailable");
            } else
                QVERIFY(QFile::copy(source, oldPath));
        }
        const auto lyricBase =
            layout == "direct" ? QFileInfo(source).absolutePath() + '/' + QFileInfo(source).completeBaseName()
                               : base;
        write(lyricBase + ".lrc", "[00:00.00]old lyric");
        write(root + "/000001/unrelated.txt", "keep");
        int id = 0;
        {
            StoreFixture store(db);
            auto song = store.getOrCreateSong(hash("legacy audio"), oldPath, "Custom title", "Artist");
            QVERIFY(song);
            id = song->id;
            const auto p = store.createPlaylist("Legacy");
            QVERIFY(store.addPlaylistSong(p, {oldPath, id}));
            QVERIFY(store.saveQueue({{{oldPath, id}, {oldPath, id}}, 1}));
            QVERIFY(
                store.updateMetadata(id, "Custom title", "Artist", "custom lyrics", QStringList{"keep tag"}));
            QSqlQuery q(store.db.database());
            q.prepare("INSERT INTO "
                      "managed_resources(id,hash,path,original_path,source_name) "
                      "VALUES(1,?,?,?,'Original')");
            q.addBindValue(hash("legacy audio"));
            q.addBindValue(oldPath);
            q.addBindValue(source);
            if (layout != "direct")
                QVERIFY(q.exec());
        }
        BackendRuntime runtime([] { return std::make_unique<FakeAudio>(); });
        QVERIFY2(runtime.start(), qPrintable(runtime.errorString()));
        Rpc rpc;
        QVERIFY(rpc.connect(runtime.serverName()));
        QJsonObject storageStatus;
        QTRY_VERIFY2_WITH_TIMEOUT(([&] {
            storageStatus = rpc.call("library.storage_status")["data"].toObject();
            return storageStatus["pending_migration"].toInt(-1) == 0;
        })(), qPrintable(QString::fromUtf8(QJsonDocument(storageStatus).toJson(QJsonDocument::Compact))), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(base + ".audio.json"), 10000);
        if (layout == "owned" || layout == "symlink")
            QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(oldPath), 10000);
        QVERIFY(QFileInfo::exists(source));
        QCOMPARE(read(root + "/000001/unrelated.txt"), QByteArray("keep"));
        auto metadata = rpc.call("song.metadata", {{"song_id", id}});
        QCOMPARE(metadata["status"].toString(), QString("ok"));
        QCOMPARE(metadata["data"].toObject()["custom_title"].toString(), QString("Custom title"));
        runtime.stop();
        StoreFixture store(db);
        MusicDirectory music(store.db, root);
        QVERIFY(ResourceRepository(store.db).lyrics(id, "local"));
        QCOMPARE(store.playlists().first().items.first().songId, id);
        QCOMPARE(store.loadQueue().items.size(), 2);
        QCOMPARE(store.loadQueue().currentIndex, 1);
        QCOMPARE(store.loadQueue().items.first().songId, id);
        QCOMPARE(store.tags().first().name, QString("keep tag"));
        QCOMPARE(store.songById(id)->lyrics, QString("custom lyrics"));
        QVERIFY(QFile::remove(source));
        QVERIFY(QFileInfo(store.library.availablePath(id)).isFile());
    }
    void invalidLegacySidecarPreventsCleanup() {
        write(source, "audio with broken cover");
        const auto base = root + "/000001/000001";
        write(base + ".flac", read(source));
        write(base + ".png", "not an image");
        {
            StoreFixture store(db);
            QVERIFY(store.getOrCreateSong(hash(read(source)), base + ".flac"));
            QSqlQuery q(store.db.database());
            q.prepare("INSERT INTO managed_resources(id,hash,path,original_path,source_name) "
                      "VALUES(1,?,?,?,'broken cover')");
            q.addBindValue(hash(read(source)));
            q.addBindValue(base + ".flac");
            q.addBindValue(source);
            QVERIFY(q.exec());
        }
        BackendRuntime runtime([] { return std::make_unique<FakeAudio>(); });
        QVERIFY(runtime.start());
        Rpc rpc;
        QVERIFY(rpc.connect(runtime.serverName()));
        QTRY_VERIFY_WITH_TIMEOUT(
            !rpc.call("library.storage_status")["data"].toObject()["migration_errors"].toArray().isEmpty(),
            10000);
        QCOMPARE(read(base + ".flac"), read(source));
        QCOMPARE(read(base + ".png"), QByteArray("not an image"));
        QCOMPARE(rpc.call("library.storage_status")["data"].toObject()["pending_migration"].toInt(), 1);
        runtime.stop();
    }
    void taskApiBacksUpExportsAndRestartsAfterRestore() {
        write(source, "first song");
        BackendRuntime runtime([] { return std::make_unique<FakeAudio>(); });
        QVERIFY(runtime.start());
        Rpc rpc;
        QVERIFY(rpc.connect(runtime.serverName()));
        auto imported = rpc.call("library.import", {{"path", source}});
        QCOMPARE(imported["status"].toString(), QString("ok"));
        const int id = imported["data"].toObject()["song_id"].toInt();
        QVERIFY(AppPaths::saveSetting("storage_test_preference", "before"));
        auto exported = rpc.call("library.export",
                                 {{"song_ids", QJsonArray{id}}, {"directory", temp.filePath("api-export")}});
        QCOMPARE(exported["status"].toString(), QString("ok"));
        auto exportTask = rpc.wait(exported["data"].toObject()["task"].toString());
        QCOMPARE(exportTask["state"].toString(), QString("finished"));
        QCOMPARE(exportTask["result"].toObject()["exported"].toInt(), 1);
        auto backed = rpc.call("library.backup", {{"directory", temp.filePath("api-backup")}});
        QCOMPARE(backed["status"].toString(), QString("ok"));
        auto backupTask = rpc.wait(backed["data"].toObject()["task"].toString());
        QCOMPARE(backupTask["state"].toString(), QString("finished"));
        QVERIFY(AppPaths::saveSetting("storage_test_preference", "after"));
        write(source + ".wav", "second song");
        QCOMPARE(rpc.call("library.import", {{"path", source + ".wav"}})["status"].toString(), QString("ok"));
        auto preview =
            rpc.call("library.restore_preview", {{"directory", backupTask["result"].toObject()["path"]}});
        const auto previewId = preview["data"].toObject()["task"].toString();
        QVERIFY(!previewId.isEmpty());
        QCOMPARE(rpc.wait(previewId)["state"].toString(), QString("finished"));
        QCOMPARE(rpc.call("library.restore", {{"preview_task", previewId}})["status"].toString(),
                 QString("error"));
        auto restore = rpc.call("library.restore", {{"preview_task", previewId}, {"confirmed", true}});
        QCOMPARE(restore["status"].toString(), QString("ok"));
        const auto restoreId = restore["data"].toObject()["task"].toString();
        QTRY_VERIFY_WITH_TIMEOUT(rpc.socket.state() == QLocalSocket::UnconnectedState, 10000);
        Rpc reopened;
        QTRY_VERIFY_WITH_TIMEOUT(reopened.connect(runtime.serverName()), 10000);
        const auto restoredTask = reopened.wait(restoreId);
        QCOMPARE(restoredTask["state"].toString(), QString("finished"));
        auto status = reopened.call("library.storage_status")["data"].toObject();
        QCOMPARE(status["songs"].toInt(), 1);
        QCOMPARE(status["rollback_copies"].toArray().size(), 1);
        QCOMPARE(AppPaths::setting("storage_test_preference").toString(), QString("after"));
        auto song = reopened.call("song.metadata", {{"song_id", id}});
        QCOMPARE(song["status"].toString(), QString("ok"));
        QVERIFY(QFileInfo(song["data"].toObject()["first_path"].toString()).isFile());
        QCOMPARE(reopened.call("player.status")["data"].toObject()["state"].toString(), QString("stopped"));
        QCOMPARE(
            reopened.call("library.remove_rollback", {{"id", "../escape"}, {"confirmed", true}})["status"]
                .toString(),
            QString("error"));
        auto verified = reopened.call("library.verify");
        QCOMPARE(verified["status"].toString(), QString("ok"));
        QCOMPARE(reopened.wait(verified["data"].toObject()["task"].toString())["state"].toString(),
                 QString("finished"));
        const auto removed =
            reopened.call("library.remove_rollback",
                          {{"id", status["rollback_copies"].toArray().first()}, {"confirmed", true}});
        QCOMPARE(removed["status"].toString(), QString("ok"));
        QCOMPARE(reopened.wait(removed["data"].toObject()["task"].toString())["state"].toString(),
                 QString("finished"));
        QVERIFY(reopened.call("library.storage_status")["data"]
                    .toObject()["rollback_copies"]
                    .toArray()
                    .isEmpty());
        QCOMPARE(reopened.call("song.metadata", {{"song_id", id}})["status"].toString(), QString("ok"));
        runtime.stop();
    }
    void failedRegistrationAndInterruptedRemovalPreserveBytes() {
        write(source, "recover me");
        StoreFixture store(db);
        MusicDirectory music(store.db, root);
        auto file = music.manage({source, hash("recover me")});
        QVERIFY(file);
        QSqlQuery q(store.db.database());
        QVERIFY(q.exec("CREATE TRIGGER fail_assets BEFORE INSERT ON song_assets "
                       "BEGIN SELECT RAISE(ABORT,'failure'); END"));
        QVERIFY(!store.library.importFile(file.value()));
        QVERIFY(store.songs().isEmpty());
        QCOMPARE(read(source), QByteArray("recover me"));
        QCOMPARE(read(file.value().path), QByteArray("recover me"));
        QVERIFY(q.exec("DROP TRIGGER fail_assets"));
        auto saved = store.library.importFile(file.value());
        QVERIFY(saved);
        const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces),
                   relative = QDir(root).relativeFilePath(file.value().path);
        const auto staged = file.value().path + ".removing-" + token;
        auto journal = QJsonObject{
            {"format", "nekotune-removal"},
            {"token", token},
            {"objects", QJsonArray{QJsonObject{{"path", relative}, {"hash", hash("recover me")}}}}};
        QVERIFY(LibraryFileIO::writeJson(root + "/.staging/remove-" + token + ".json", journal));
        QVERIFY(QFile::rename(file.value().path, staged));
        MusicDirectory recovered(store.db, root);
        QCOMPARE(read(file.value().path), QByteArray("recover me"));
        QVERIFY(!QFileInfo::exists(staged));
        QVERIFY(!LibraryArchive::removeRollback(root, db, "../media"));
        QCOMPARE(read(file.value().path), QByteArray("recover me"));
    }
    void changedLegacySourceRemainsPendingAndBlocksBackup() {
        write(source, "replacement");
        int id = 0;
        {
            StoreFixture store(db);
            auto old = store.getOrCreateSong(hash("original"), source, "Keep identity");
            QVERIFY(old);
            id = old->id;
        }
        BackendRuntime runtime([] { return std::make_unique<FakeAudio>(); });
        QVERIFY(runtime.start());
        Rpc rpc;
        QVERIFY(rpc.connect(runtime.serverName()));
        QTRY_VERIFY_WITH_TIMEOUT(
            !rpc.call("library.storage_status")["data"].toObject()["migration_errors"].toArray().isEmpty(),
            10000);
        QCOMPARE(rpc.call("library.storage_status")["data"].toObject()["pending_migration"].toInt(), 1);
        QCOMPARE(rpc.call("library.backup", {{"directory", temp.filePath("incomplete-backup")}})["status"]
                     .toString(),
                 QString("error"));
        QCOMPARE(read(source), QByteArray("replacement"));
        QCOMPARE(rpc.call("song.metadata", {{"song_id", id}})["data"].toObject()["custom_title"].toString(),
                 QString("Keep identity"));
        runtime.stop();
    }
};
QTEST_MAIN(LibraryStorageTest)
#include "library_storage_test.moc"
