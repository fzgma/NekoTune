#include "storage/queue_repository.h"
#include "storage/resource_repository.h"
#include <QSet>
#include <QSqlError>
namespace nekotune {
// Replace the table within the caller's surrounding transaction.
// The delete/insert sequence must not be used as separate commits.
// Each row repeats the selected index for legacy schema compatibility.
// Persist positional order rather than runtime queue occurrence IDs.
// The application adopts memory only after its transaction commits.
bool QueueRepository::saveQueue(const QueueSnapshot &snapshot) {
    if (!m_session.isReady()) {
        return false;
    }

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM queue_items"))) {
        setError(query.lastError().text());
        return false;
    }

    query.prepare(QStringLiteral("INSERT INTO queue_items (position, path, song_id, current_index, play_next) "
                                 "VALUES (:position, :path, :song_id, :current_index, :play_next)"));
    for (int index = 0; index < snapshot.items.size(); ++index) {
        const auto &item = snapshot.items.at(index);
        query.bindValue(QStringLiteral(":position"), index);
        query.bindValue(QStringLiteral(":path"),
                        ResourceRepository(m_session).hasAudio(item.songId) || item.path.isNull()
                            ? QStringLiteral("")
                            : item.path);
        query.bindValue(QStringLiteral(":song_id"), item.songId);
        query.bindValue(QStringLiteral(":current_index"), snapshot.currentIndex);
        query.bindValue(QStringLiteral(":play_next"), item.playNext);
        if (!query.exec()) {
            setError(query.lastError().text());
            return false;
        }
    }

    return true;
}

// Restore ordered records even when their files are currently unavailable.
// QueueService later joins song metadata and filters stale references.
// The selected index is read from the legacy per-row representation.
// An empty table has no selected item regardless of default row values.
QueueSnapshot QueueRepository::loadQueue() const {
    QueueSnapshot snapshot;
    if (!m_session.isReady()) {
        return snapshot;
    }

    QSqlQuery query(m_db);
    if (!query.exec(
            QStringLiteral("SELECT path, song_id, current_index, play_next FROM queue_items ORDER BY position ASC"))) {
        return snapshot;
    }

    const auto locations = ResourceRepository(m_session).audioLocations();
    while (query.next()) {
        snapshot.items.append({query.value(0).toString(), query.value(1).toInt(), query.value(3).toBool()});
        if (locations.contains(snapshot.items.last().songId))
            snapshot.items.last().path = locations.value(snapshot.items.last().songId).second;
        snapshot.currentIndex = query.value(2).toInt();
    }
    if (snapshot.items.isEmpty()) {
        snapshot.currentIndex = -1;
    }
    return snapshot;
}

} // namespace nekotune
