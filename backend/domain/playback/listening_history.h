#pragma once
#include "domain/result.h"
#include <QString>
#include <QVector>

namespace nekotune {
// All timestamps are UTC epoch milliseconds. Snapshots survive library deletion.
struct ListeningSession {
    QString id, songKey, title, artist, reason;
    int songId = 0, queueId = 0;
    qint64 startedAt = 0, updatedAt = 0, listenedMs = 0, durationMs = 0;
    bool completed = false;
};
struct ListeningInterval {
    qint64 start = 0, end = 0;
};
struct HistoryFilter {
    qint64 from = 0, to = 0;
    QString search;
    bool completedOnly = false;
    int limit = 50, offset = 0;
};
struct HistoryPage {
    QVector<ListeningSession> items;
    qint64 total = 0;
};
struct ListeningDay {
    QString date;
    qint64 from = 0, to = 0, listenedMs = 0;
};
struct FrequentSong {
    QString songKey, title, artist;
    int songId = 0;
    qint64 plays = 0, listenedMs = 0;
};
struct ListeningStats {
    QVector<ListeningDay> days;
    QVector<FrequentSong> songs;
    qint64 weekMs = 0, totalSessions = 0;
};
class IListeningHistoryRepository {
  public:
    virtual ~IListeningHistoryRepository() = default;
    virtual Result<void> recover() = 0;
    virtual Result<void> save(const ListeningSession &, const QVector<ListeningInterval> &) = 0;
    virtual Result<HistoryPage> list(const HistoryFilter &) const = 0;
    virtual Result<ListeningStats> statistics(const QVector<ListeningDay> &, qint64 weekStart) const = 0;
    virtual Result<void> clear() = 0;
};
} // namespace nekotune
