#include "application/library/library_service.h"
#include "application/library/audio_availability.h"
#include "application/transaction.h"
#include <QSet>
namespace nekotune {
LibraryService::LibraryService(ISongRepository &songs, ITagRepository &tags, ITransaction &transaction)
    : m_songs(songs), m_tags(tags), m_transaction(transaction) {}
// Read songs, paths and tags in bulk before assembling the view.
// Choose a usable path without dropping records whose files vanished.
// The empty path is the frontend's explicit availability indication.
// Display metadata remains tied to the song's stable identity.
LibrarySnapshot LibraryService::snapshot() const {
    LibrarySnapshot result;
    result.tags = m_tags.tags();
    const auto paths = m_songs.songPaths();
    const auto memberships = m_tags.songTags();
    for (const auto &song : m_songs.songs()) {
        if (song.isRemote())
            continue;
        QString path;
        for (const auto &candidate : paths.value(song.id)) {
            if (isAvailableAudioFile(candidate)) {
                path = candidate;
                break;
            }
        }
        result.songs.append({song, path, memberships.value(song.id)});
    }
    return result;
}
Result<SongMetadata> LibraryService::metadata(int id) const {
    auto song = m_songs.songById(id);
    if (!song)
        return failure(QStringLiteral("Song metadata not found"), ErrorCode::NotFound);
    return *song;
}
QVector<SongTag> LibraryService::tagsFor(int id) const { return m_tags.songTags().value(id); }
QString LibraryService::availablePath(int id) const {
    for (const auto &path : m_songs.pathsForSong(id)) {
        if (isAvailableAudioFile(path))
            return path;
    }
    return {};
}
Result<SongMetadata> LibraryService::importFile(const ImportedFile &file, const QString &title,
                                                const QString &artist) {
    Transaction tx(m_transaction);
    if (!tx)
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    auto song = m_songs.getOrCreateSong(file.hash, file.path, title, artist, file.sourceName, file.durationMs,
                                      file.importSource, file.metadata);
    if (!song || !tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    emit durationUpdated(*song);
    emit changed();
    return *song;
}
Result<void> LibraryService::recordPlayback(int id) {
    Transaction tx(m_transaction);
    if (!tx || !m_songs.incrementPlayCount(id))
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    const auto song = m_songs.songById(id);
    if (!song || !tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    emit statisticsUpdated(*song);
    emit changed();
    return {};
}
Result<void> LibraryService::backfillDuration(int id, const QString &hash, qint64 durationMs) {
    // A scan may finish after deletion or another import; never recreate or replace a song here.
    auto song = m_songs.songById(id);
    if (!song || song->hash != hash || song->durationMs > 0 || durationMs <= 0)
        return {};
    Transaction tx(m_transaction);
    if (!tx || !m_songs.updateDuration(id, durationMs) || !tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    song->durationMs = durationMs;
    emit durationUpdated(*song);
    emit changed();
    return {};
}
Result<SongMetadata> LibraryService::update(int id, const MetadataPatch &patch) {
    const auto current = metadata(id);
    if (!current) return current.error();
    auto result = updateMany({id}, patch);
    return result ? Result<SongMetadata>(result.value().first()) : Result<SongMetadata>(result.error());
}
Result<QVector<SongMetadata>> LibraryService::updateMany(const QVector<int> &ids, const MetadataPatch &patch,
                                                         TagEditMode tagMode) {
    if (ids.isEmpty()) return failure("No songs selected");
    if (patch.tags)
        for (const auto &tag : *patch.tags)
            if (tag.trimmed().isEmpty() || tag.trimmed().size() > 64) return failure("Tag name must contain 1 to 64 characters");
    for (const auto &number : {patch.year, patch.trackNumber, patch.discNumber})
        if (number && (*number < 0 || *number > 9999)) return failure("Metadata number must be between 0 and 9999");
    QSet<int> seen;
    QVector<SongMetadata> songs;
    for (int id : ids) {
        const auto song = m_songs.songById(id);
        if (id <= 0 || seen.contains(id) || !song) return failure("Invalid or missing song id");
        seen.insert(id);
        songs.append(*song);
    }
    Transaction tx(m_transaction);
    if (!tx) return failure(m_transaction.errorString(), ErrorCode::Storage);
    const auto memberships = patch.tags && tagMode != TagEditMode::Replace ? m_tags.songTags() : QHash<int, QVector<SongTag>>{};
    for (auto &song : songs) {
        song.customTitle = patch.title.value_or(song.customTitle);
        song.artist = patch.artist.value_or(song.artist);
        song.lyrics = patch.lyrics.value_or(song.lyrics);
        song.album = patch.album.value_or(song.album);
        song.albumArtist = patch.albumArtist.value_or(song.albumArtist);
        song.genre = patch.genre.value_or(song.genre);
        song.year = patch.year.value_or(song.year);
        song.trackNumber = patch.trackNumber.value_or(song.trackNumber);
        song.discNumber = patch.discNumber.value_or(song.discNumber);
        if (!m_songs.updateMetadata(song)) return failure(m_songs.errorString(), ErrorCode::Storage);
        if (patch.tags) {
            QStringList tags = *patch.tags;
            if (tagMode != TagEditMode::Replace) {
                QSet<QString> requested;
                for (const auto &tag : *patch.tags) requested.insert(tag.trimmed().toCaseFolded());
                tags.clear();
                for (const auto &tag : memberships.value(song.id))
                    if (tagMode == TagEditMode::Add || !requested.contains(tag.name.toCaseFolded())) tags.append(tag.name);
                if (tagMode == TagEditMode::Add) tags.append(*patch.tags);
            }
            if (!m_tags.replaceSongTags(song.id, tags)) return failure(m_tags.errorString(), ErrorCode::Storage);
        }
    }
    if (!tx.commit()) return failure(m_transaction.errorString(), ErrorCode::Storage);
    if (songs.size() == 1) emit metadataChanged(songs.first());
    else emit metadataBatchChanged(songs);
    emit changed();
    return songs;
}
} // namespace nekotune
