#include "app_paths.h"
#include "application/lyrics/cover_service.h"
#include "i18n.h"
#include "infrastructure/lyrics/lyrics_storage.h"
#include "infrastructure/library/music_directory.h"
#include "infrastructure/library/audio_reference.h"
#include "infrastructure/library/import_executor.h"
#include "application/lyrics/lyrics_service.h"
#include "infrastructure/lyrics/sidecar_store.h"
#include "runtime/backend_runtime.h"
#include "support/store_fixture.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QNetworkReply>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest>
#include <cstring>

using namespace nekotune;
namespace {
void write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
class Rpc {
  public:
    QLocalSocket socket;
    QByteArray buffer;
    int id = 0;
    bool connect(const QString &path) {
        socket.connectToServer(path);
        return socket.waitForConnected(3000);
    }
    QJsonObject call(const QString &method, const QJsonObject &params = {}) {
        const auto next = ++id;
        socket.write(QJsonDocument(QJsonObject{{"id", next}, {"method", method}, {"params", params}})
                         .toJson(QJsonDocument::Compact) +
                     '\n');
        socket.flush();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            QCoreApplication::processEvents();
            buffer += socket.readAll();
            while (buffer.contains('\n')) {
                auto line = buffer.left(buffer.indexOf('\n'));
                buffer.remove(0, line.size() + 1);
                const auto object = QJsonDocument::fromJson(line).object();
                if (object.value("id").toInt() == next)
                    return object;
            }
            QTest::qWait(1);
        }
        return {};
    }
};
class ImageReply final : public QNetworkReply {
  public:
    ImageReply(const QNetworkRequest &request, const QByteArray &body, int delay, int status) : m_body(body) {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setHeader(QNetworkRequest::ContentTypeHeader, "image/png");
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(delay, this, [this] {
            if (m_finished)
                return;
            m_available = true;
            emit readyRead();
            m_finished = true;
            setFinished(true);
            emit finished();
        });
    }
    void abort() override {
        if (m_finished)
            return;
        m_finished = true;
        setError(OperationCanceledError, "Cancelled");
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return (m_available ? m_body.size() - m_offset : 0) + QNetworkReply::bytesAvailable();
    }

  protected:
    qint64 readData(char *data, qint64 maximum) override {
        if (!m_available)
            return 0;
        const auto count = qMin(maximum, qint64(m_body.size() - m_offset));
        if (!count)
            return -1;
        std::memcpy(data, m_body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

  private:
    QByteArray m_body;
    qint64 m_offset = 0;
    bool m_available = false, m_finished = false;
};
class ImageNetwork final : public QNetworkAccessManager {
  public:
    QByteArray body;
    int delay = 1, status = 200, calls = 0;

  protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        ++calls;
        auto *reply = new ImageReply(request, body, delay, status);
        reply->setParent(this);
        return reply;
    }
};
} // namespace

class MusicDirectoryTest final : public QObject {
    Q_OBJECT
  private:
    QTemporaryDir m_profile;
  private slots:
    void init() {
        QVERIFY(m_profile.isValid());
        const auto directory = m_profile.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
        qputenv("NEKOTUNE_HOME", (directory + "/music").toUtf8());
        qputenv("NEKOTUNE_DB_PATH", (directory + "/player.sqlite3").toUtf8());
        qputenv("NEKOTUNE_SOCKET", (directory + "/backend.sock").toUtf8());
        qunsetenv("KUGOU_ACCOUNT_API_KEY");
        qunsetenv("KUGOU_ACCOUNT_API_KEY_FILE");
    }
    // Legacy links remain readable for migration; new imports use owned copies.
    void nativeAudioLinksExposeRealBytes() {
        const auto source = m_profile.filePath("native-links/原歌曲.flac");
        const auto target = m_profile.filePath("native-links/linked.flac");
        write(source, "native link audio bytes");
        if (!createAudioSymlink(source, target))
            QSKIP("Native file symlink creation is unavailable for this user/filesystem");
        QVERIFY(QFileInfo(target).isSymbolicLink());
        QCOMPARE(read(target), QByteArray("native link audio bytes"));
        QVERIFY(QFile::remove(target));
        QCOMPARE(read(source), QByteArray("native link audio bytes"));
        QVERIFY(createAudioSymlink(source, target));
        StoreFixture store(m_profile.filePath("native-link-availability.sqlite3"));
        const auto imported = store.library.importFile({target, hash("native link audio bytes"), "Native link"});
        QVERIFY(imported);
        QVERIFY(!store.library.availablePath(imported.value().id).isEmpty());
        QVERIFY(QFile::remove(source));
        QVERIFY(store.library.availablePath(imported.value().id).isEmpty());
        QCOMPARE(store.library.snapshot().songs.size(), 1);
        QVERIFY(store.library.snapshot().songs.first().path.isEmpty());
        write(source, "native link audio bytes");
        QCOMPARE(store.library.availablePath(imported.value().id), target);
        const auto chained = m_profile.filePath("native-links/chained.flac");
        QVERIFY(createAudioSymlink(target, chained));
        const auto chainedSong = store.library.importFile({chained, hash("chain"), "Chained link"});
        QVERIFY(chainedSong);
        QCOMPARE(store.library.availablePath(chainedSong.value().id), chained);
        QVERIFY(QFile::remove(source));
        QVERIFY(store.library.availablePath(chainedSong.value().id).isEmpty());
    }

    // Only validated lyric and image content should become app-owned sidecars.
    // Delayed replies simulate a request whose track revision is no longer current.
    // Changing identity while a reply is pending must prevent an obsolete file write.
    // KRC and LRC persistence are checked separately because their representations differ.
    // An existing symlink must not turn a sidecar save into a write outside the managed root.
    // The fake transport controls timing without requiring an online lyrics account.
    void savesRealLyricsAndArtworkAndRejectsStaleRequests() {
        const auto original = m_profile.filePath("external.wav");
        write(original, "audio");
        StoreFixture db(qEnvironmentVariable("NEKOTUNE_DB_PATH"));
        MusicDirectory music(db.db);
        auto audio = music.manage({original, hash("audio"), "External"});
        QVERIFY(audio);
        const auto base = QFileInfo(audio.value().path).absolutePath() + "/" +
                          QFileInfo(audio.value().path).completeBaseName();
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::red);
        ImageNetwork network;
        QBuffer imageBuffer(&network.body);
        imageBuffer.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&imageBuffer, "PNG"));
        SidecarStore assets(&network);
        CoverService covers(std::make_unique<LyricsStorage>(m_profile.filePath("cover-cache")));
        covers.setManagedBaseResolver([base](const QString &) { return base; });
        connect(&assets, &SidecarStore::saved, &covers, &CoverService::assetsUpdated);
        QSignalSpy saved(&assets, &SidecarStore::saved);
        QSignalSpy failures(&assets, &SidecarStore::failed);
        LyricsDocument document;
        document.source = "kugou";
        document.syncedLyrics = "[00:00.00]First";
        document.krcLyrics = "[0,1000]<0,1000,0>First";
        document.coverUrl = "https://imge.kugou.com/first.png";
        assets.setCurrent(hash("audio"), 1, false);
        assets.save(hash("audio"), 1, base, document);
        QTRY_COMPARE(saved.count(), 2);
        const auto initialCover = covers.resolve(audio.value().path, hash("audio"));
        QCOMPARE(QUrl(initialCover).toLocalFile(), base + ".png");
        QCOMPARE(read(base + ".lrc"), document.syncedLyrics.toUtf8());
        QVERIFY(!QFileInfo(base + ".lrc").isSymLink());
        QVERIFY(!QFileInfo(base + ".png").isSymLink());
        QVERIFY(!QFileInfo(audio.value().path).isSymLink());
        QVERIFY(!QFileInfo(m_profile.filePath("external.lrc")).exists());
        LyricsQuery query;
        query.trackId = hash("audio");
        auto local = LyricsStorage(m_profile.filePath("cache")).readLocal(query, audio.value().path);
        QVERIFY(local && local.value());
        QCOMPARE(local.value()->krcLyrics, document.krcLyrics);
        network.delay = 100;
        assets.setCurrent(hash("audio"), 2, false);
        assets.save(hash("audio"), 2, base, document);
        assets.setCurrent(hash("audio"), 3, false);
        document.krcLyrics.clear();
        document.syncedLyrics = "[00:00.00]Replacement";
        document.coverUrl.clear();
        assets.save(hash("audio"), 3, base, document);
        QTest::qWait(150);
        QVERIFY(!QFileInfo(base + ".krc").exists());
        QVERIFY(!QFileInfo(base + ".png").exists());
        QCOMPARE(read(base + ".lrc"), document.syncedLyrics.toUtf8());
        network.delay = 1;
        network.status = 500;
        document.coverUrl = "https://imge.kugou.com/failure.png";
        assets.setCurrent(hash("audio"), 4, false);
        assets.save(hash("audio"), 4, base, document);
        QTRY_VERIFY(failures.count() > 0);
        QCOMPARE(read(original), QByteArray("audio"));
        const auto calls = network.calls;
        assets.setCurrent(hash("audio"), 5, true);
        assets.save(hash("audio"), 5, base, document);
        QCOMPARE(network.calls, calls);
        QVERIFY(QFileInfo(base + ".lrc").isFile());
        network.status = 200;
        assets.setCurrent(hash("audio"), 6, false);
        assets.save(hash("audio"), 6, base, document);
        QTRY_VERIFY(QFileInfo(base + ".png").isFile());
        QVERIFY(covers.resolve(audio.value().path, hash("audio")) != initialCover);
        document.coverUrl.clear();
        document.source = "lrclib";
        assets.setCurrent(hash("audio"), 7, false);
        assets.save(hash("audio"), 7, base, document);
        QVERIFY(QFileInfo(base + ".png").isFile());
    }
    void settingsMergeAndLanguageRestores() {
        QVERIFY(AppPaths::saveSetting("lyrics_offline", true));
        I18n first;
        first.setLanguage(first.language() == "zh" ? "en" : "zh");
        I18n restored;
        QCOMPARE(restored.language(), first.language());
        QVERIFY(AppPaths::setting("lyrics_offline").toBool());
        qputenv("NEKOTUNE_LANGUAGE", "zh");
        I18n overridden;
        QCOMPARE(overridden.language(), QString("zh"));
        qunsetenv("NEKOTUNE_LANGUAGE");
        QCOMPARE(QFileInfo(AppPaths::configDirectory()).permissions() &
                     (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                 QFileDevice::Permissions{});
    }
    // The isolated home reproduces the old storage layout without touching user data.
    // Migration copies the legacy database rather than consuming or rewriting its original bytes.
    // Tags, playlists and queue records must survive along with the audio identities.
    // Lyrics assets move to the new profile layout independently of credential migration.
    // Without an installed account plugin, credential files stay available for later secure migration.
    // Plugin-specific secure migration is verified by the external plugin suite.
    void migratesLegacyDatabaseAndPreservesCredentials() {
        QTemporaryDir directory;
        const auto project = directory.filePath("project");
        const auto oldDb = project + "/build/nekotune.sqlite3";
        const auto home = directory.filePath("home");
        const auto config = home + "/Music/NekoTune/config";
        write(project + "/CMakeLists.txt", "# fixture");
        QDir().mkpath(project + "/backend");
        const auto audio = directory.filePath("old.wav");
        write(audio, "legacy audio");
        {
            StoreFixture old(oldDb);
            QVERIFY2(old.isReady(), qPrintable(old.errorString()));
            auto song = old.getOrCreateSong(hash("legacy audio"), audio, "Saved name", "Saved artist");
            QVERIFY(song);
            QVERIFY(old.updateMetadata(song->id, "Saved name", "Saved artist", "Saved lyrics",
                                       QStringList{"Old tag"}));
            const auto playlist = old.createPlaylist("Old playlist");
            QVERIFY(playlist > 0);
            QVERIFY(old.addPlaylistSong(playlist, {audio, song->id}));
            QVERIFY(old.saveQueue({{{audio, song->id}}, 0}));
        }
        const auto oldBytes = read(oldDb);
        const auto oldKey = directory.filePath("xdg-config/NekoTune/kugou-account-key");
        const auto oldSession = directory.filePath("xdg-data/NekoTune/kugou-session.json");
        write(oldKey, "fixture-key");
        write(oldSession, "{\"version\":1,\"cookies\":{}}");
        write(directory.filePath("xdg-data/NekoTune/NekoTune/lyrics-cache/sample.json"), "{}");
        write(directory.filePath("xdg-cache/NekoTune/NekoTune/covers/sample"), "cached image");
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.remove("NEKOTUNE_HOME");
        environment.remove("NEKOTUNE_DB_PATH");
        environment.insert("HOME", home);
        environment.insert("XDG_CONFIG_HOME", directory.filePath("xdg-config"));
        environment.insert("XDG_DATA_HOME", directory.filePath("xdg-data"));
        environment.insert("XDG_CACHE_HOME", directory.filePath("xdg-cache"));
        const auto socket = directory.filePath("migration.sock");
        environment.insert("NEKOTUNE_SOCKET", socket);
        QProcess backend;
        backend.setProcessEnvironment(environment);
        backend.setWorkingDirectory(project);
        backend.start(NEKOTUNE_BACKEND_BINARY);
        QVERIFY(backend.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(socket).exists(), 5000);
        Rpc peer;
        QVERIFY(peer.connect(socket));
        const auto status = peer.call("player.status").value("data").toObject();
        QCOMPARE(status.value("database_path").toString(), config + "/nekotune.sqlite3");
        QCOMPARE(status.value("queue").toArray().size(), 1);
        QCOMPARE(status.value("playlists").toArray().first().toObject().value("name").toString(),
                 QString("Old playlist"));
        // Without an installed plugin, migration leaves credentials available for a later opt-in.
        QCOMPARE(read(oldKey), QByteArray("fixture-key"));
        QCOMPARE(read(oldSession), QByteArray("{\"version\":1,\"cookies\":{}}"));
        QVERIFY(QFileInfo(config + "/lyrics-cache/sample.json").isFile());
        QVERIFY(QFileInfo(config + "/covers/sample").isFile());
        backend.terminate();
        QVERIFY(backend.waitForFinished());
        QCOMPARE(read(oldDb), oldBytes);
    }
};
QTEST_GUILESS_MAIN(MusicDirectoryTest)
#include "music_directory_test.moc"
