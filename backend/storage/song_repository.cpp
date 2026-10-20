#include "storage/song_repository.h"
#include "domain/lyrics/krc_parser.h"
#include "domain/lyrics/lrc_parser.h"
#include "storage/resource_repository.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSqlError>
namespace nekotune {
bool SongRepository::containsAudioHash(const QString &hash) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT 1 FROM songs WHERE hash=? UNION SELECT 1 FROM song_assets WHERE audio_hash=? LIMIT 1");
    q.addBindValue(hash);
    q.addBindValue(hash);
    return q.exec() && q.next();
}
bool SongRepository::retainAssets(int id) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE audio_objects SET retained=1 WHERE hash=(SELECT audio_hash "
              "FROM song_assets WHERE song_id=?)");
    q.addBindValue(id);
    if (!q.exec()) {
        setError(q.lastError().text());
        return false;
    }
    q.prepare("UPDATE artwork_objects SET retained=1 WHERE hash=(SELECT "
              "cover_hash FROM song_assets WHERE song_id=?)");
    q.addBindValue(id);
    if (q.exec())
        return true;
    setError(q.lastError().text());
    return false;
}
QStringList SongRepository::cleanupKeys(int id) const {
    QStringList keys;
    QSqlQuery q(m_db);
    q.prepare("SELECT audio_hash,cover_hash FROM song_assets WHERE song_id=?");
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        if (!q.value(0).toString().isEmpty())
            keys << q.value(0).toString();
        if (!q.value(1).toString().isEmpty())
            keys << "cover/" + q.value(1).toString();
    }
    return keys;
}
void SongRepository::resolveAssets(SongMetadata &song) const {
    ResourceRepository resources(m_session);
    if (!resources.hasAudio(song.id))
        return;
    song.hash = resources.audioHash(song.id);
    song.firstPath = resources.audioPath(song.id);
    song.hasLocalAudio = QFileInfo(song.firstPath).isFile();
}
bool SongRepository::saveImportedAssets(int id, const ImportedFile &file) {
    QSqlQuery q(m_db);
    q.prepare("SELECT 1 FROM audio_objects WHERE hash=?");
    q.addBindValue(file.hash);
    if (!q.exec()) {
        setError(q.lastError().text());
        return false;
    }
    if (!q.next())
        return true; // Repository-only legacy callers have no managed file
                     // publication.
    ResourceRepository resources(m_session);
    if (!resources.attach(id, file.hash))
        return false;
    LyricsDocument document;
    document.source = "local";
    if (file.sidecars.contains("krc")) {
        auto text = KrcParser::read(file.sidecars.value("krc"));
        if (!text || KrcParser::parse(*text).isEmpty()) {
            setError("Invalid KRC sidecar");
            return false;
        }
        document.krcLyrics = *text;
    }
    if (file.sidecars.contains("lrc")) {
        const auto text = QString::fromUtf8(file.sidecars.value("lrc"));
        if (LrcParser::looksLikeLrc(text))
            document.syncedLyrics = text;
        else
            document.plainLyrics = text;
        if (text.trimmed().isEmpty()) {
            setError("Empty lyric sidecar");
            return false;
        }
    }
    if (file.sidecars.contains("txt")) {
        LyricsDocument plain;
        plain.source = "local";
        plain.plainLyrics = QString::fromUtf8(file.sidecars.value("txt"));
        if (plain.plainLyrics.trimmed().isEmpty()) {
            setError("Empty text lyric sidecar");
            return false;
        }
        if (document.plainLyrics.isEmpty())
            document.plainLyrics = plain.plainLyrics;
        else if (!resources.saveLyrics(id, plain, "local_txt"))
            return false;
    }
    if (!document.isEmpty() && !resources.lyrics(id, "local") &&
        !resources.saveLyrics(id, document, "local", file.sidecars.value("krc")))
        return false;
    if (file.sidecars.contains("cover_hash")) {
        q.prepare("UPDATE song_assets SET cover_hash=? WHERE song_id=? AND "
                  "cover_hash IS NULL");
        q.addBindValue(QString::fromLatin1(file.sidecars.value("cover_hash")));
        q.addBindValue(id);
        if (!q.exec()) {
            setError(q.lastError().text());
            return false;
        }
    }
    for (const auto &sql :
         {"UPDATE songs SET first_path='' WHERE id=?", "DELETE FROM song_paths WHERE song_id=?",
          "UPDATE queue_items SET path='' WHERE song_id=?",
          "UPDATE playlist_items SET path='' WHERE song_id=?"}) {
        q.prepare(sql);
        q.addBindValue(id);
        if (!q.exec()) {
            setError(q.lastError().text());
            return false;
        }
    }
    return true;
}
std::optional<SongMetadata> SongRepository::getOrCreateSong(const QString &hash, const QString &path,
                                                            const QString &customTitle, const QString &artist,
                                                            const QString &sourceName, qint64 durationMs,
                                                            const QString &importSource, const EmbeddedMetadata &metadata) {
    if (!m_session.isReady()) {
        return std::nullopt;
    }

    // Explicit import can restore a deleted hash; the scanner checks scan_ignored before calling here.
    QSqlQuery restore(m_db);
    restore.prepare("DELETE FROM scan_ignored WHERE hash=:hash");
    restore.bindValue(":hash", hash);
    if (!restore.exec()) {
        setError(restore.lastError().text());
        return std::nullopt;
    }
    if (auto existing = songByHash(hash)) {
        // A second path is still the same song; retain custom title, artist, lyrics and tag identity.
        if (!rememberSongPath(existing->id, path))
            return std::nullopt;
        if (existing->sourceName.isEmpty() && !sourceName.isEmpty()) {
            QSqlQuery update(m_db);
            update.prepare("UPDATE songs SET source_name=:name WHERE id=:id");
            update.bindValue(":name", sourceName);
            update.bindValue(":id", existing->id);
            if (!update.exec()) {
                setError(update.lastError().text());
                return std::nullopt;
            }
            existing->sourceName = sourceName;
        }
        if (durationMs > 0 && existing->durationMs != durationMs) {
            if (!updateDuration(existing->id, durationMs))
                return std::nullopt;
            existing->durationMs = durationMs;
        }
        return existing;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO songs (hash, first_path, custom_title, artist, "
                                 "source_name, duration_ms, import_source, album, album_artist, genre, year, track_number, disc_number, lyrics) "
                                 "VALUES (:hash, :first_path, :custom_title, :artist, :source_name, :duration_ms, :origin, :album, :album_artist, :genre, :year, :track, :disc, :lyrics)"));
    auto text = [](const QString &value) { return value.isNull() ? QStringLiteral("") : value.trimmed(); };
    query.bindValue(":album", text(metadata.album));
    query.bindValue(":album_artist", text(metadata.albumArtist));
    query.bindValue(":genre", text(metadata.genre));
    query.bindValue(":year", metadata.year);
    query.bindValue(":track", metadata.trackNumber);
    query.bindValue(":disc", metadata.discNumber);
    query.bindValue(":lyrics", text(metadata.lyrics));
    query.bindValue(":origin", importSource.trimmed().isEmpty() ? QStringLiteral("local") : importSource);
    query.bindValue(":duration_ms", qMax(qint64(0), durationMs));
    query.bindValue(":source_name", sourceName.isNull() ? QStringLiteral("") : sourceName);
    query.bindValue(QStringLiteral(":hash"), hash);
    query.bindValue(QStringLiteral(":first_path"), path);
    query.bindValue(QStringLiteral(":custom_title"),
                    text(customTitle.isEmpty() ? metadata.title : customTitle));
    query.bindValue(QStringLiteral(":artist"), text(artist.isEmpty() ? metadata.artist : artist));

    if (!query.exec()) {
        setError(query.lastError().text());
        return std::nullopt;
    }
    const int songId = query.lastInsertId().toInt();
    if (!rememberSongPath(songId, path)) {
        return std::nullopt;
    }
    return songById(songId);
}

std::optional<SongMetadata> SongRepository::getOrCreateRemote(const SongMetadata &song) {
    if (!m_session.isReady() || song.providerId.isEmpty() || song.providerTrackId.isEmpty())
        return {};
    QSqlQuery query(m_db);
    query.prepare("SELECT id FROM songs WHERE source_provider=:provider AND source_track_id=:track");
    query.bindValue(":provider", song.providerId);
    query.bindValue(":track", song.providerTrackId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return {};
    }
    if (query.next())
        return songById(query.value(0).toInt());
    query.finish();
    query.prepare(
        "INSERT INTO songs "
        "(hash,first_path,custom_title,artist,source_name,duration_ms,source_provider,source_track_id,album,"
        "cover_url,import_source) VALUES (NULL,'',:title,:artist,:name,:duration,:provider,:track,:album,:cover,:origin)");
    query.bindValue(":origin", song.providerId.section('/', 0, 0));
    query.bindValue(":title", song.customTitle.isNull() ? QString("") : song.customTitle);
    query.bindValue(":artist", song.artist.isNull() ? QString("") : song.artist);
    query.bindValue(":name", song.customTitle.isNull() ? QString("") : song.customTitle);
    query.bindValue(":duration", qMax(qint64(0), song.durationMs));
    query.bindValue(":provider", song.providerId);
    query.bindValue(":track", song.providerTrackId);
    query.bindValue(":album", song.album.isNull() ? QString("") : song.album);
    query.bindValue(":cover", song.coverUrl.isNull() ? QString("") : song.coverUrl);
    if (!query.exec()) {
        setError(query.lastError().text());
        return {};
    }
    return songById(query.lastInsertId().toInt());
}

QVector<SongMetadata> SongRepository::songs() const {
    QVector<SongMetadata> items;
    if (!m_session.isReady()) {
        return items;
    }

    QSqlQuery query(m_db);
    if (!query.exec(
            QStringLiteral("SELECT id, hash, first_path, custom_title, artist, lyrics, "
                           "source_name, duration_ms, source_provider, source_track_id, album, cover_url, import_source, play_count, album_artist, genre, year, track_number, disc_number, last_played_at "
                           "FROM songs ORDER BY id ASC"))) {
        return items;
    }

    const auto locations = ResourceRepository(m_session).audioLocations();
    while (query.next()) {
        items.append(SongMetadata{
            query.value(0).toInt(),
            query.value(1).toString(),
            query.value(2).toString(),
            query.value(3).toString(),
            query.value(4).toString(),
            query.value(5).toString(),
            query.value(6).toString(),
            query.value(7).toLongLong(),
            query.value(8).toString(),
            query.value(9).toString(),
            query.value(10).toString(),
            query.value(11).toString(),
            query.value(12).toString(),
            query.value(13).toLongLong(),
            query.value(14).toString(), query.value(15).toString(), query.value(16).toInt(),
            query.value(17).toInt(), query.value(18).toInt(),
            query.value(19).toString(),
        });
        auto &song = items.last();
        const auto location = locations.constFind(song.id);
        if (location != locations.cend()) {
            song.hash = location->first;
            song.firstPath = location->second;
            song.hasLocalAudio = QFileInfo(song.firstPath).isFile();
        }
    }

    return items;
}

std::optional<SongMetadata> SongRepository::songById(int songId) const {
    if (!m_session.isReady() || songId <= 0) {
        return std::nullopt;
    }

    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("SELECT id, hash, first_path, custom_title, artist, lyrics, "
                       "source_name, duration_ms, source_provider, source_track_id, album, cover_url, import_source, play_count, album_artist, genre, year, track_number, disc_number, last_played_at "
                       "FROM songs WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), songId);

    if (!query.exec()) {
        return std::nullopt;
    }

    return readSongFromQuery(query);
}

std::optional<SongMetadata> SongRepository::updateMetadata(const SongMetadata &song) {
    if (!m_session.isReady() || song.id <= 0)
        return std::nullopt;
    QSqlQuery query(m_db);
    query.prepare("UPDATE songs SET custom_title=:title, artist=:artist, lyrics=:lyrics, "
                  "album=:album, album_artist=:album_artist, genre=:genre, year=:year, "
                  "track_number=:track, disc_number=:disc, updated_at=CURRENT_TIMESTAMP WHERE id=:id");
    auto text = [](const QString &value) { return value.isNull() ? QStringLiteral("") : value; };
    query.bindValue(":title", text(song.customTitle));
    query.bindValue(":artist", text(song.artist));
    query.bindValue(":lyrics", text(song.lyrics));
    query.bindValue(":album", text(song.album));
    query.bindValue(":album_artist", text(song.albumArtist));
    query.bindValue(":genre", text(song.genre));
    query.bindValue(":year", song.year);
    query.bindValue(":track", song.trackNumber);
    query.bindValue(":disc", song.discNumber);
    query.bindValue(":id", song.id);
    if (!query.exec()) {
        setError(query.lastError().text());
        return std::nullopt;
    }
    return songById(song.id);
}

// Prefer paths already chosen by the active queue and saved playlists.
// Then consider recently imported alternatives and the original first path.
// These are lookup candidates; filesystem availability is checked by services.
// Remembering alternate paths keeps reimports useful across restarts.
// Do not replace song identity simply because one candidate disappears.
QVector<QString> SongRepository::pathsForSong(int songId) const {
    QVector<QString> paths;
    ResourceRepository resources(m_session);
    if (resources.hasAudio(songId))
        return {resources.audioPath(songId)};
    if (!m_session.isReady() || songId <= 0)
        return paths;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT path FROM queue_items WHERE song_id = :id ORDER BY position"));
    query.bindValue(QStringLiteral(":id"), songId);
    if (query.exec())
        while (query.next())
            paths.append(query.value(0).toString());
    query.prepare(
        QStringLiteral("SELECT path FROM playlist_items WHERE song_id = :id ORDER BY playlist_id, position"));
    query.bindValue(QStringLiteral(":id"), songId);
    if (query.exec())
        while (query.next())
            paths.append(query.value(0).toString());
    query.prepare(QStringLiteral("SELECT path FROM song_paths WHERE song_id = :id ORDER BY rowid DESC"));
    query.bindValue(QStringLiteral(":id"), songId);
    if (query.exec())
        while (query.next())
            paths.append(query.value(0).toString());
    query.prepare(QStringLiteral("SELECT first_path FROM songs WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), songId);
    if (query.exec() && query.next())
        paths.append(query.value(0).toString());
    return paths;
}

QHash<int, QVector<QString>> SongRepository::songPaths() const {
    QHash<int, QVector<QString>> paths;
    if (!m_session.isReady())
        return paths;
    QSqlQuery query(m_db);
    if (query.exec(QStringLiteral("SELECT song_id, path FROM queue_items ORDER BY position"))) {
        while (query.next())
            paths[query.value(0).toInt()].append(query.value(1).toString());
    }
    if (query.exec(
            QStringLiteral("SELECT song_id, path FROM playlist_items ORDER BY playlist_id, position"))) {
        while (query.next())
            paths[query.value(0).toInt()].append(query.value(1).toString());
    }
    if (query.exec(QStringLiteral("SELECT song_id, path FROM song_paths ORDER BY rowid DESC"))) {
        while (query.next())
            paths[query.value(0).toInt()].append(query.value(1).toString());
    }
    if (query.exec(QStringLiteral("SELECT id, first_path FROM songs ORDER BY id"))) {
        while (query.next())
            paths[query.value(0).toInt()].append(query.value(1).toString());
    }
    const auto locations = ResourceRepository(m_session).audioLocations();
    for (auto it = locations.cbegin(); it != locations.cend(); ++it)
        paths[it.key()] = {it.value().second};
    return paths;
}

std::optional<SongMetadata> SongRepository::songByHash(const QString &hash) const {
    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("SELECT id, hash, first_path, custom_title, artist, lyrics, "
                       "source_name, duration_ms, source_provider, source_track_id, album, cover_url, import_source, play_count, album_artist, genre, year, track_number, disc_number, last_played_at "
                       "FROM songs WHERE hash = :hash"));
    query.bindValue(QStringLiteral(":hash"), hash);

    if (!query.exec()) {
        return std::nullopt;
    }

    return readSongFromQuery(query);
}

bool SongRepository::rememberSongPath(int songId, const QString &path) {
    if (!m_session.isReady() || songId <= 0 || path.isEmpty())
        return false;
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO song_paths (song_id, path) VALUES (:id, :path)"));
    query.bindValue(QStringLiteral(":id"), songId);
    query.bindValue(QStringLiteral(":path"), path);
    if (query.exec())
        return true;
    setError(query.lastError().text());
    return false;
}

std::optional<SongMetadata> SongRepository::readSongFromQuery(QSqlQuery &query) const {
    if (!query.next()) {
        return std::nullopt;
    }

    SongMetadata song{
        query.value(0).toInt(),     query.value(1).toString(),    query.value(2).toString(),
        query.value(3).toString(),  query.value(4).toString(),    query.value(5).toString(),
        query.value(6).toString(),  query.value(7).toLongLong(),  query.value(8).toString(),
        query.value(9).toString(),  query.value(10).toString(),   query.value(11).toString(),
        query.value(12).toString(), query.value(13).toLongLong(), query.value(14).toString(),
        query.value(15).toString(), query.value(16).toInt(),      query.value(17).toInt(),
        query.value(18).toInt(),    query.value(19).toString(),
    };
    resolveAssets(song);
    return song;
}
// Only positive probe results can replace a stored duration.
// Zero is unknown and must not erase useful duration from a prior import.
// Require an affected song row so a late probe cannot claim a deleted save.
// The service additionally checks hash identity and newer duration evidence.
bool SongRepository::updateDuration(int songId, qint64 durationMs) {
    if (!m_session.isReady() || songId <= 0 || durationMs <= 0)
        return false;
    QSqlQuery query(m_db);
    query.prepare("UPDATE songs SET duration_ms=:duration, updated_at=CURRENT_TIMESTAMP WHERE id=:id");
    query.bindValue(":duration", durationMs);
    query.bindValue(":id", songId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}
bool SongRepository::incrementPlayCount(int songId) {
    if (!m_session.isReady() || songId <= 0) {
        setError(QStringLiteral("Invalid song for playback statistics"));
        return false;
    }
    QSqlQuery query(m_db);
    // Count and timestamp describe the same successful playback start and must commit together.
    query.prepare("UPDATE songs SET play_count=play_count+1, last_played_at=:played_at WHERE id=:id");
    query.bindValue(":played_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.bindValue(":id", songId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    if (query.numRowsAffected() != 1) {
        setError(QStringLiteral("Song not found for playback statistics"));
        return false;
    }
    return true;
}
bool SongRepository::erase(int songId) {
    QSqlQuery query(m_db);
    // Record the hash before deletion so retained audio is not re-imported by the next startup scan.
    query.prepare(
        "INSERT OR IGNORE INTO scan_ignored SELECT hash FROM songs WHERE id=:id AND source_provider=''");
    query.bindValue(":id", songId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    query.prepare("DELETE FROM songs WHERE id=:id");
    query.bindValue(":id", songId);
    if (!query.exec()) {
        setError(query.lastError().text());
        return false;
    }
    return query.numRowsAffected() == 1;
}
} // namespace nekotune
