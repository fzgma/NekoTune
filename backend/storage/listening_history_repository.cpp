#include "storage/listening_history_repository.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace nekotune {
namespace {
AppError sqlError(const QSqlQuery &query) { return failure(query.lastError().text(), ErrorCode::Storage); }
ListeningSession session(const QSqlQuery &q) {
    return {q.value(0).toString(),   q.value(1).toString(),    q.value(2).toString(),
            q.value(3).toString(),   q.value(4).toString(),    q.value(5).toInt(),
            q.value(6).toInt(),      q.value(7).toLongLong(),  q.value(8).toLongLong(),
            q.value(9).toLongLong(), q.value(10).toLongLong(), q.value(11).toBool()};
}
} // namespace
Result<void> ListeningHistoryRepository::recover() {
    QSqlQuery q(m_database.database());
    // A crash never accrues offline time. updated_at remains the last durable checkpoint.
    if (!q.exec("UPDATE listening_sessions SET reason='interrupted', completed=0 WHERE reason=''"))
        return sqlError(q);
    return {};
}
Result<void> ListeningHistoryRepository::save(const ListeningSession &s,
                                              const QVector<ListeningInterval> &intervals) {
    if (!m_database.begin())
        return failure(m_database.errorString(), ErrorCode::Storage);
    QSqlQuery q(m_database.database());
    q.prepare(
        "INSERT INTO listening_sessions "
        "(id,song_key,title,artist,reason,song_id,queue_id,started_at,updated_at,listened_ms,duration_ms,"
        "completed) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET updated_at=excluded.updated_at, "
        "listened_ms=excluded.listened_ms, duration_ms=excluded.duration_ms, reason=excluded.reason, "
        "completed=excluded.completed");
    for (const QVariant &v : QVariantList{s.id, s.songKey, s.title, s.artist, s.reason, s.songId, s.queueId,
                                          s.startedAt, s.updatedAt, s.listenedMs, s.durationMs, s.completed})
        q.addBindValue(v.metaType().id() == QMetaType::QString && v.toString().isNull()
                           ? QVariant(QStringLiteral(""))
                           : v);
    if (!q.exec()) {
        m_database.rollback();
        return sqlError(q);
    }
    q.prepare("INSERT INTO listening_intervals(session_id,started_at,ended_at) VALUES (?,?,?)");
    for (const auto &interval : intervals) {
        q.bindValue(0, s.id);
        q.bindValue(1, interval.start);
        q.bindValue(2, interval.end);
        if (!q.exec()) {
            m_database.rollback();
            return sqlError(q);
        }
    }
    if (!m_database.commit()) {
        m_database.rollback();
        return failure(m_database.errorString(), ErrorCode::Storage);
    }
    return {};
}
Result<HistoryPage> ListeningHistoryRepository::list(const HistoryFilter &filter) const {
    QSqlQuery q(m_database.database());
    const QString where =
        " FROM listening_sessions WHERE started_at>=? AND started_at<? "
        "AND (?=0 OR completed=1) AND (instr(lower(title),lower(?))>0 OR instr(lower(artist),lower(?))>0)";
    const auto bind = [&] {
        q.addBindValue(filter.from);
        q.addBindValue(filter.to);
        q.addBindValue(filter.completedOnly);
        q.addBindValue(filter.search.isEmpty() ? QStringLiteral("") : filter.search);
        q.addBindValue(filter.search.isEmpty() ? QStringLiteral("") : filter.search);
    };
    q.prepare("SELECT COUNT(*)" + where);
    bind();
    if (!q.exec() || !q.next())
        return sqlError(q);
    HistoryPage result;
    result.total = q.value(0).toLongLong();
    q.prepare("SELECT "
              "id,song_key,title,artist,reason,song_id,queue_id,started_at,updated_at,listened_ms,duration_"
              "ms,completed" +
              where + " ORDER BY started_at DESC,id DESC LIMIT ? OFFSET ?");
    bind();
    q.addBindValue(filter.limit);
    q.addBindValue(filter.offset);
    if (!q.exec())
        return sqlError(q);
    while (q.next())
        result.items.append(session(q));
    return result;
}
Result<ListeningStats> ListeningHistoryRepository::statistics(const QVector<ListeningDay> &days,
                                                              qint64 weekStart) const {
    ListeningStats result;
    if (days.isEmpty())
        return result;
    QSqlQuery q(m_database.database());
    QStringList buckets;
    for (qsizetype i = 0; i < days.size(); ++i)
        buckets.append("SELECT ? AS day, ? AS start, ? AS end");
    q.prepare("WITH days AS (" + buckets.join(" UNION ALL ") +
              ") SELECT days.day, "
              "COALESCE(SUM(MIN(i.ended_at,days.end)-MAX(i.started_at,days.start)),0) "
              "FROM days LEFT JOIN listening_intervals i ON i.ended_at>days.start AND i.started_at<days.end "
              "GROUP BY days.day ORDER BY days.day");
    for (const auto &day : days) {
        q.addBindValue(day.date);
        q.addBindValue(day.from);
        q.addBindValue(day.to);
    }
    if (!q.exec())
        return sqlError(q);
    result.days = days;
    qsizetype index = 0;
    while (q.next())
        result.days[index++].listenedMs = q.value(1).toLongLong();
    q.prepare("SELECT COALESCE(SUM(MIN(ended_at,?)-MAX(started_at,?)),0) FROM listening_intervals WHERE "
              "ended_at>? AND started_at<?");
    q.addBindValue(days.last().to);
    q.addBindValue(weekStart);
    q.addBindValue(weekStart);
    q.addBindValue(days.last().to);
    if (!q.exec() || !q.next())
        return sqlError(q);
    result.weekMs = q.value(0).toLongLong();
    if (!q.exec("SELECT COUNT(*) FROM listening_sessions") || !q.next())
        return sqlError(q);
    result.totalSessions = q.value(0).toLongLong();
    // Rank by actual listening in the displayed month, including sessions spanning its boundary.
    q.prepare("SELECT s.song_key,MAX(s.song_id),MAX(s.title),MAX(s.artist),COUNT(DISTINCT s.id), "
              "SUM(MIN(i.ended_at,?)-MAX(i.started_at,?)) AS listened "
              "FROM listening_intervals i JOIN listening_sessions s ON s.id=i.session_id "
              "WHERE i.ended_at>? AND i.started_at<? GROUP BY s.song_key ORDER BY listened DESC,s.song_key "
              "LIMIT 5");
    q.addBindValue(days.last().to);
    q.addBindValue(days.first().from);
    q.addBindValue(days.first().from);
    q.addBindValue(days.last().to);
    if (!q.exec())
        return sqlError(q);
    while (q.next())
        result.songs.append({q.value(0).toString(), q.value(2).toString(), q.value(3).toString(),
                             q.value(1).toInt(), q.value(4).toLongLong(), q.value(5).toLongLong()});
    return result;
}
Result<void> ListeningHistoryRepository::clear() {
    QSqlQuery q(m_database.database());
    if (!q.exec("DELETE FROM listening_sessions"))
        return sqlError(q);
    return {};
}
} // namespace nekotune
