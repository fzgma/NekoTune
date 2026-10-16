#include "domain/playback/player_queue.h"
namespace nekotune {
bool PlayerQueue::isEmpty() const { return m_items.isEmpty(); }

int PlayerQueue::size() const { return m_items.size(); }

int PlayerQueue::currentIndex() const { return m_currentIndex; }

bool PlayerQueue::setCurrentIndex(int index) {
    if (index < -1 || index >= m_items.size()) {
        return false;
    }

    m_currentIndex = index;
    if (index >= 0)
        m_items[index].playNext = false;
    return true;
}

int PlayerQueue::add(const QString &path, const SongMetadata &metadata) {
    return insert(m_items.size(), path, metadata);
}

int PlayerQueue::insert(int position, const QString &path, const SongMetadata &metadata) {
    if (position < 0 || position > m_items.size())
        return 0;
    const int id = m_nextQueueId++;
    m_items.insert(position, {
        id,
        metadata,
        path,
        QStringLiteral("queued"),
    });
    if (m_currentIndex >= position)
        ++m_currentIndex;
    return id;
}

int PlayerQueue::insertAfterCurrent(const QString &path, const SongMetadata &metadata) {
    const int position = m_currentIndex + 1;
    const int id = insert(position, path, metadata);
    m_items[position].playNext = true;
    return id;
}

bool PlayerQueue::move(int queueId, int position) {
    const int from = indexById(queueId);
    if (from < 0 || position < 0 || position >= m_items.size())
        return false;
    const int currentId = m_currentIndex < 0 ? 0 : m_items.at(m_currentIndex).id;
    m_items.move(from, position);
    m_currentIndex = currentId ? indexById(currentId) : -1;
    return true;
}

bool PlayerQueue::removeAt(int index) {
    if (index < 0 || index >= m_items.size()) {
        return false;
    }

    const int previousCurrentIndex = m_currentIndex;
    m_items.removeAt(index);

    if (index == previousCurrentIndex) {
        // Choosing a successor belongs to the playback operation, not to generic removal.
        m_currentIndex = -1;
    } else if (index < previousCurrentIndex) {
        --m_currentIndex;
    } else if (m_currentIndex >= m_items.size()) {
        m_currentIndex = -1;
    }

    return true;
}

void PlayerQueue::clear() {
    m_items.clear();
    m_currentIndex = -1;
    // Keep the ID counter: late UI actions must not address a new entry using an old ID.
}

void PlayerQueue::markCurrent() {
    for (auto &item : m_items) {
        item.state = QStringLiteral("queued");
    }

    if (m_currentIndex >= 0 && m_currentIndex < m_items.size()) {
        m_items[m_currentIndex].state = QStringLiteral("current");
    }
}

void PlayerQueue::updateSongMetadata(const SongMetadata &metadata) {
    for (auto &item : m_items) {
        if (item.metadata.id == metadata.id) {
            item.metadata = metadata;
        }
    }
}

QueueItem &PlayerQueue::operator[](int index) { return m_items[index]; }

const QueueItem &PlayerQueue::at(int index) const { return m_items.at(index); }

int PlayerQueue::indexById(int queueId) const {
    for (int index = 0; index < m_items.size(); ++index) {
        if (m_items.at(index).id == queueId) {
            return index;
        }
    }

    return -1;
}

QVector<QueueRecord> PlayerQueue::records() const {
    QVector<QueueRecord> records;
    records.reserve(m_items.size());
    for (const auto &item : m_items) {
        records.append({item.path, item.metadata.id, item.playNext});
    }
    return records;
}

void PlayerQueue::restore(const QVector<QueueItem> &items, int currentIndex) {
    m_items = items;
    m_currentIndex = -1;
    m_nextQueueId = 1;
    for (const auto &item : m_items) {
        m_nextQueueId = qMax(m_nextQueueId, item.id + 1);
    }
    setCurrentIndex(currentIndex);
    markCurrent();
}

} // namespace nekotune
