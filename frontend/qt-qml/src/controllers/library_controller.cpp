#include "controllers/library_controller.h"
#include <QJsonArray>
#include <QSet>
#include <QFileInfo>
#include <QCollator>
#include <QJsonDocument>
#include <QMap>
#include <algorithm>
namespace {
QString albumKey(const QVariantMap &song) {
    const auto album = song.value("album").toString().trimmed().toCaseFolded();
    auto artist = song.value("album_artist").toString().trimmed();
    if (artist.isEmpty()) artist = song.value("artist").toString().trimmed();
    return QString::fromUtf8(QJsonDocument(QJsonArray{album, album.isEmpty() ? QString() : artist.toCaseFolded()})
                                .toJson(QJsonDocument::Compact));
}
QString importSourceFor(const QVariantMap &song) {
    const auto origin = song.value("import_source").toString().trimmed();
    if (!origin.isEmpty())
        return origin;
    const auto provider = song.value("source").toMap().value("provider_id").toString();
    return provider.isEmpty() ? QStringLiteral("local") : provider.section('/', 0, 0);
}
}
LibraryController::LibraryController(IpcClient &client) : FeatureController(client) {
    connect(&client, &IpcClient::eventReceived, this, [this](const QJsonObject &event) {
        auto name = event.value("event").toString();
        if (name == "server.connected") send("library.scan", {});
        if (name == "library.changed" || name == "server.connected")
            refreshLibrary();
    });
}
void LibraryController::refreshLibrary() {
    if (m_loading) {
        // Coalesce event bursts but request one fresh snapshot after the in-flight response.
        m_reload = true;
        return;
    }
    m_loading = true;
    emit loadStateChanged();
    send("library.list", {}, [this](const QJsonObject &data, const QString &error) {
        m_loading = false;
        if (error.isEmpty()) {
            apply(data.value("library").toObject());
            m_loaded = true;
        }
        emit loadStateChanged();
        if (m_reload) {
            m_reload = false;
            refreshLibrary();
        }
    });
}
void LibraryController::apply(const QJsonObject &data) {
    m_songs.update(data.value("songs").toArray().toVariantList());
    m_tags.update(data.value("tags").toArray().toVariantList());
    QSet<int> tags, songs;
    for (const auto &item : m_tags.items())
        tags.insert(item.toMap().value("id").toInt());
    for (const auto &item : m_songs.items())
        songs.insert(item.toMap().value("song_id").toInt());
    auto nextTags = m_tagIds;
    nextTags.removeIf([&](const QVariant &id) { return !tags.contains(id.toInt()); });
    if (nextTags != m_tagIds) {
        m_tagIds = nextTags;
        clearSelection();
        emit filterChanged();
    }
    auto selected = m_songIds;
    // Prune deleted song IDs without discarding the rest of the current selection.
    selected.removeIf([&](const QVariant &id) { return !songs.contains(id.toInt()); });
    if (selected != m_songIds) {
        m_songIds = selected;
        emit selectionChanged();
    }
    filter();
}
// Apply tag intersection and text search to the same source snapshot.
// Keep the underlying library model independent of these view filters.
// Remove selection IDs that are no longer visible after reconciliation.
// Playback later sends this resulting order rather than recreating search
// rules independently in the backend or selecting only highlighted rows.
void LibraryController::filter() {
    QVariantList items;
    QSet<QString> sources;
    const auto query = m_searchText.trimmed();
    for (const auto &value : m_songs.items()) {
        auto song = value.toMap();
        const auto source = importSourceFor(song);
        sources.insert(source);
        const auto count = song.value("play_count").toLongLong();
        if ((!m_importSource.isEmpty() && source != m_importSource)
            || (m_playFilter == "never_played" && count != 0)
            || (m_playFilter == "played" && count == 0))
            continue;
        QSet<int> assigned;
        for (const auto &tag : song.value("tags").toList())
            assigned.insert(tag.toMap().value("id").toInt());
        bool match = true;
        for (const auto &id : m_tagIds)
            if (!assigned.contains(id.toInt())) {
                match = false;
                break;
            }
        const auto searchable = song.value("title").toString() + " " + song.value("artist").toString()
                                + " " + song.value("album").toString() + " " + song.value("album_artist").toString()
                                + " " + QFileInfo(song.value("path").toString()).fileName();
        if (match && (query.isEmpty() || searchable.contains(query, Qt::CaseInsensitive)))
            items.append(value);
    }
    auto sourceOptions = sources.values();
    std::sort(sourceOptions.begin(), sourceOptions.end());
    if (sourceOptions != m_importSources) {
        m_importSources = sourceOptions;
        emit importSourcesChanged();
    }
    // The C locale ignores numeric collation; use natural ordering in headless sessions too.
    QCollator collator(QLocale().language() == QLocale::C ? QLocale(QLocale::English) : QLocale());
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(items.begin(), items.end(), [&](const QVariant &left, const QVariant &right) {
        const auto a = left.toMap(), b = right.toMap();
        const int aId = a.value("song_id").toInt(), bId = b.value("song_id").toInt();
        if (m_sortOrder == "added_desc") return aId > bId;
        if (m_sortOrder == "added_asc") return aId < bId;
        if (m_sortOrder == "track") {
            for (const auto *field : {"disc_number", "track_number"}) {
                const auto aNumber = a.value(field).toInt(), bNumber = b.value(field).toInt();
                if (aNumber != bNumber) return (aNumber > 0 ? aNumber : 10000) < (bNumber > 0 ? bNumber : 10000);
            }
            return aId < bId;
        }
        if (m_sortOrder == "title" || m_sortOrder == "artist" || m_sortOrder == "album") {
            const auto aText = a.value(m_sortOrder).toString().trimmed();
            const auto bText = b.value(m_sortOrder).toString().trimmed();
            if (aText.isEmpty() != bText.isEmpty()) return !aText.isEmpty();
            const auto compared = collator.compare(aText, bText);
            if (compared != 0) return compared < 0;
        } else {
            const bool duration = m_sortOrder.startsWith("duration_");
            const auto field = duration ? "duration_ms" : "play_count";
            const auto aValue = a.value(field).toLongLong(), bValue = b.value(field).toLongLong();
            // Unknown durations stay at the end in both directions.
            if (duration && (aValue <= 0) != (bValue <= 0)) return aValue > 0;
            if (aValue != bValue)
                return m_sortOrder == "duration_desc" || m_sortOrder == "most_played"
                    ? aValue > bValue : aValue < bValue;
        }
        return aId < bId;
    });
    QMap<QString, QVariantMap> albums;
    for (const auto &item : items) {
        const auto song = item.toMap();
        const auto key = albumKey(song);
        auto &album = albums[key];
        if (album.isEmpty()) {
            const auto artist = song.value("album_artist").toString().trimmed();
            album = {{"album_id", key}, {"title", song.value("album").toString().trimmed()},
                     {"artist", artist.isEmpty() ? song.value("artist").toString() : artist},
                     {"song_count", 0}, {"cover_url", QString()}};
            if (album.value("title").toString().isEmpty()) album["artist"] = QString();
        }
        album["song_count"] = album.value("song_count").toInt() + 1;
        if (album.value("cover_url").toString().isEmpty()) album["cover_url"] = song.value("cover_url");
    }
    QVariantList albumItems;
    for (const auto &album : albums) albumItems.append(album);
    std::sort(albumItems.begin(), albumItems.end(), [&](const auto &a, const auto &b) {
        const auto left = a.toMap(), right = b.toMap();
        const auto title = left.value("title").toString(), other = right.value("title").toString();
        if (title.isEmpty() != other.isEmpty()) return !title.isEmpty();
        const int compared = collator.compare(title, other);
        return compared ? compared < 0 : left.value("album_id").toString() < right.value("album_id").toString();
    });
    m_albums.update(albumItems);
    if (!m_albumId.isEmpty()) {
        if (albums.contains(m_albumId) && m_selectedAlbum != albums.value(m_albumId)) {
            m_selectedAlbum = albums.value(m_albumId);
            emit filterChanged();
        }
        items.removeIf([this](const auto &song) { return albumKey(song.toMap()) != m_albumId; });
    }
    m_filtered.update(items);
    auto selected = m_songIds;
    selected.removeIf([&items](const auto &id) {
        return std::none_of(items.cbegin(), items.cend(), [&id](const auto &item) {
            return item.toMap().value("song_id").toInt() == id.toInt();
        });
    });
    if (selected != m_songIds) {
        m_songIds = selected;
        emit selectionChanged();
    }
}
void LibraryController::setSortOrder(const QString &order) {
    static const QStringList allowed{"added_asc", "added_desc", "title", "artist", "album",
                                     "duration_asc", "duration_desc", "most_played", "least_played", "track"};
    if (order == m_sortOrder || !allowed.contains(order)) return;
    m_sortOrder = order;
    filter();
    emit filterChanged();
}
void LibraryController::setPlayFilter(const QString &value) {
    if (value == m_playFilter || (value != "all" && value != "played" && value != "never_played")) return;
    m_playFilter = value;
    filter();
    emit filterChanged();
}
void LibraryController::setImportSource(const QString &source) {
    if (source == m_importSource) return;
    m_importSource = source;
    filter();
    emit filterChanged();
}
void LibraryController::resetFilters() {
    m_searchText.clear();
    m_tagIds.clear();
    m_importSource.clear();
    m_playFilter = "all";
    m_sortOrder = "added_asc";
    m_albumView = false;
    m_albumId.clear();
    m_selectedAlbum.clear();
    clearSelection();
    filter();
    emit filterChanged();
}
void LibraryController::setAlbumView(bool enabled) {
    if (m_albumView == enabled) return;
    m_albumView = enabled;
    closeAlbum();
}
void LibraryController::openAlbum(const QString &id) {
    for (const auto &value : m_albums.items()) {
        const auto album = value.toMap();
        if (album.value("album_id").toString() != id) continue;
        m_albumView = true;
        m_albumId = id;
        m_selectedAlbum = album;
        m_sortOrder = "track";
        clearSelection();
        filter();
        emit filterChanged();
        return;
    }
}
void LibraryController::closeAlbum() {
    m_albumId.clear();
    m_selectedAlbum.clear();
    clearSelection();
    filter();
    emit filterChanged();
}
void LibraryController::saveBatchMetadata(const QVariantList &ids, const QVariantMap &patch, const QString &tagMode) {
    send("library.update_metadata", {{"song_ids", QJsonArray::fromVariantList(ids)},
         {"patch", QJsonObject::fromVariantMap(patch)}, {"tag_mode", tagMode}},
         [this](const QJsonObject &data, const QString &error) {
             if (error.isEmpty()) emit batchMetadataSaved(data.value("updated_count").toInt());
         });
}
void LibraryController::setSearchText(const QString &text) {
    if (text == m_searchText)
        return;
    m_searchText = text;
    clearSelection();
    filter();
    emit filterChanged();
}
void LibraryController::setSelectedTagIds(const QVariantList &ids) {
    QVariantList normalized;
    for (const auto &id : ids)
        if (!normalized.contains(id.toInt()))
            normalized.append(id.toInt());
    if (normalized == m_tagIds)
        return;
    m_tagIds = normalized;
    clearSelection();
    filter();
    emit filterChanged();
}
void LibraryController::toggleTag(int id) {
    auto next = m_tagIds;
    if (next.contains(id))
        next.removeAll(id);
    else
        next.append(id);
    setSelectedTagIds(next);
}
void LibraryController::toggleSelection(int id) {
    const auto items = m_filtered.items();
    if (std::none_of(items.cbegin(), items.cend(), [id](const auto &item) {
            return item.toMap().value("song_id").toInt() == id;
        }))
        return;
    if (m_songIds.contains(id))
        m_songIds.removeAll(id);
    else
        m_songIds.append(id);
    emit selectionChanged();
}
// Selection anchors are song identities, not transient model indices.
// Resolve both the clicked row and anchor in the current filtered model.
// Shift ranges therefore follow visible order after search/tag filtering.
// Ctrl preserves existing selections while toggling the clicked identity.
// A plain click replaces selection without requesting playback.
void LibraryController::selectRow(int id, bool extend, bool range) {
    const auto items = m_filtered.items();
    int row = -1, anchor = -1;
    for (int index = 0; index < items.size(); ++index) {
        const auto songId = items[index].toMap().value("song_id").toInt();
        if (songId == id) row = index;
        if (songId == m_selectionAnchor) anchor = index;
    }
    if (row < 0) return;
    if (range && anchor >= 0) {
        if (!extend) m_songIds.clear();
        for (int index = std::min(row, anchor); index <= std::max(row, anchor); ++index) {
            const auto songId = items[index].toMap().value("song_id").toInt();
            if (!m_songIds.contains(songId)) m_songIds.append(songId);
        }
        emit selectionChanged();
    } else if (extend) {
        toggleSelection(id);
        m_selectionAnchor = id;
    } else {
        m_songIds = {id};
        m_selectionAnchor = id;
        emit selectionChanged();
    }
}
void LibraryController::toggleSelectAll() {
    QVariantList ids;
    bool all = m_filtered.count() > 0;
    for (const auto &item : m_filtered.items()) {
        int id = item.toMap().value("song_id").toInt();
        ids.append(id);
        if (!m_songIds.contains(id))
            all = false;
    }
    m_songIds = all ? QVariantList{} : ids;
    emit selectionChanged();
}
void LibraryController::clearSelection() {
    m_selectionAnchor = 0;
    if (m_songIds.isEmpty())
        return;
    m_songIds.clear();
    emit selectionChanged();
}
void LibraryController::playLibrary(const QVariantList &tagIds, int songId) {
    QJsonObject params{{"tag_ids", QJsonArray::fromVariantList(tagIds)}};
    if (tagIds == m_tagIds) {
        // Pass the visible search/filter order so subsequent tracks match the list the user played.
        QJsonArray ids;
        for (const auto &item : m_filtered.items()) ids.append(item.toMap().value("song_id").toInt());
        params.insert("song_ids", ids);
    }
    if (songId > 0)
        params.insert("song_id", songId);
    send("library.play", params, [this](const QJsonObject &data, const QString &error) {
        if (error.isEmpty())
            emit libraryPlaybackSkipped(data.value("skipped_song_ids").toArray().size());
    });
}
void LibraryController::playSongs(const QVariantList &songIds, int startSongId) {
    // Home owns its playback order; never inherit the library page's filters.
    QJsonObject params{{"tag_ids", QJsonArray{}}, {"song_ids", QJsonArray::fromVariantList(songIds)}};
    if (startSongId > 0)
        params.insert("song_id", startSongId);
    send("library.play", params, [this](const QJsonObject &data, const QString &error) {
        if (error.isEmpty())
            emit libraryPlaybackSkipped(data.value("skipped_song_ids").toArray().size());
    });
}
void LibraryController::deleteLibrarySongs(const QVariantList &ids, bool cleanFiles) {
    send("library.delete", {{"song_ids", QJsonArray::fromVariantList(ids)}, {"clean_files", cleanFiles}},
         [this](const QJsonObject &data, const QString &error) {
             if (error.isEmpty()) {
                 clearSelection();
                 QStringList cleanupErrors;
                 for (const auto &value : data.value("cleanup_errors").toArray())
                     cleanupErrors.append(value.toString());
                 emit libraryDeletionFinished(cleanupErrors);
             }
         });
}
void LibraryController::loadMetadata(int id) {
    // List rows omit full lyrics; fetch authoritative metadata before allowing an edit/save.
    m_editingId = id;
    m_metadataReady = false;
    m_editing.clear();
    emit editingChanged();
    auto generation = ++m_generation;
    send("song.metadata", {{"song_id", id}},
         [this, id, generation](const QJsonObject &data, const QString &error) {
             // Reopening even the same song starts a new editing session; reject its earlier response.
             if (generation != m_generation || id != m_editingId)
                 return;
             if (error.isEmpty()) {
                 m_editing = data.toVariantMap();
                 m_metadataReady = true;
             }
             emit editingChanged();
         });
}
void LibraryController::saveMetadata(const QVariantMap &patch) {
    if (!m_metadataReady)
        return;
    auto data = QJsonObject::fromVariantMap(patch);
    data.insert("song_id", m_editingId);
    int id = m_editingId;
    send("song.update_metadata", data, [this, id](const auto &, const QString &error) {
        if (error.isEmpty())
            emit songMetadataSaved(id);
    });
}
