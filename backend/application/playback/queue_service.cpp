#include "application/playback/queue_service.h"
#include "application/transaction.h"
namespace nekotune {
QueueService::QueueService(IQueueRepository &repository, ISongRepository &songs, ITransaction &transaction)
    : m_repository(repository), m_transaction(transaction) {
    // Persistent records contain song identity and path, not live occurrence IDs.
    // Reconstruct occurrences in order while retaining selected-index correspondence.
    // Missing metadata is filtered, but unavailable audio paths remain for later playback handling.
    // Restoration itself never loads the decoder or resumes sound.
    const auto snapshot = repository.loadQueue();
    int current = -1;
    for (int i = 0; i < snapshot.items.size(); ++i) {
        const auto &record = snapshot.items[i];
        auto song = songs.songById(record.songId);
        if (!song)
            continue;
        // Dropped stale records shift indices; restore selection in the filtered in-memory queue.
        if (i == snapshot.currentIndex)
            current = m_queue.size();
        m_queue.add(record.path, *song);
        m_queue[m_queue.size() - 1].playNext = record.playNext;
    }
    m_queue.setCurrentIndex(current);
    m_queue.markCurrent();
}
Result<void> QueueService::move(int queueId, int position) {
    auto next = m_queue;
    if (!next.move(queueId, position))
        return failure(QStringLiteral("Invalid queue item or target position"));
    return commit(std::move(next));
}
Result<void> QueueService::commit(PlayerQueue next) {
    next.markCurrent();
    Transaction tx(m_transaction);
    if (!tx || !m_repository.saveQueue({next.records(), next.currentIndex()}) || !tx.commit())
        return failure(m_transaction.errorString(), ErrorCode::Storage);
    // Observers see the new queue only once both its items and current index are durable.
    adoptCommitted(std::move(next));
    return {};
}
void QueueService::adoptCommitted(PlayerQueue next) {
    next.markCurrent();
    m_queue = std::move(next);
    emit changed();
}
void QueueService::updateMetadata(const SongMetadata &metadata) {
    m_queue.updateSongMetadata(metadata);
    if (metadata.hasLocalAudio)
        for (int i = 0; i < m_queue.size(); ++i)
            if (m_queue[i].metadata.id == metadata.id)
                m_queue[i].path = metadata.firstPath;
    emit changed();
}
void QueueService::updateMetadataBatch(const QVector<SongMetadata> &metadata) {
    for (const auto &song : metadata) {
        m_queue.updateSongMetadata(song);
        if (song.hasLocalAudio)
            for (int i = 0; i < m_queue.size(); ++i)
                if (m_queue[i].metadata.id == song.id)
                    m_queue[i].path = song.firstPath;
    }
    emit changed();
}
} // namespace nekotune
