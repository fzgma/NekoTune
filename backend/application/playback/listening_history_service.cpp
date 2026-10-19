#include "application/playback/listening_history_service.h"
#include <QDateTime>
#include <QFileInfo>
#include <QUuid>
#include <algorithm>

namespace nekotune {
ListeningHistoryService::ListeningHistoryService(PlayerEngine &player, IPlaybackBackend &audio,
                                                 IListeningHistoryRepository &repository, Clock utcClock,
                                                 Clock monotonicClock)
    : m_audio(audio), m_repository(repository), m_utc(std::move(utcClock)),
      m_monotonic(std::move(monotonicClock)) {
    m_clock.start();
    if (!m_utc)
        m_utc = [] { return QDateTime::currentMSecsSinceEpoch(); };
    if (!m_monotonic)
        m_monotonic = [this] { return m_clock.elapsed(); };
    connect(&player, &PlayerEngine::listeningStarted, this, &ListeningHistoryService::start);
    connect(&player, &PlayerEngine::listeningFinished, this, &ListeningHistoryService::finish);
    connect(&player, &PlayerEngine::seekRequested, this, [this] {
        sample(true);
        m_seeking = true;
        m_seekTick = m_monotonic();
        report(flush());
    });
    connect(&audio, &IPlaybackBackend::seeked, this, [this] {
        m_seeking = false;
        baseline();
    });
    connect(&audio, &IPlaybackBackend::positionChanged, this, [this](qint64 position) {
        m_notifiedPosition = position;
        sample();
    });
    connect(&audio, &IPlaybackBackend::stateChanged, this, [this](PlayerState state) {
        sample(true);
        m_playing = state == PlayerState::Playing;
        baseline();
        if (!m_playing)
            report(flush());
    });
    connect(&player, &PlayerEngine::playbackRateChanged, this, [this] {
        sample(true);
        baseline();
    });
    connect(&audio, &IPlaybackBackend::backendChanged, this, [this] { baseline(); });
    connect(&audio, &IPlaybackBackend::durationChanged, this, [this](qint64 duration) {
        if (m_session && duration > 0) {
            m_session->durationMs = duration;
            m_dirty = true;
        }
    });
    m_checkpoint.setInterval(5000);
    connect(&m_checkpoint, &QTimer::timeout, this, [this] {
        sample();
        report(flush());
    });
    m_checkpoint.start();
}
void ListeningHistoryService::report(const Result<void> &result) {
    if (!result)
        emit errorOccurred(result.error().message);
}
void ListeningHistoryService::baseline() {
    m_position = m_notifiedPosition = m_audio.position();
    m_tick = m_monotonic();
    m_rate = m_audio.playbackRate();
}
void ListeningHistoryService::start(const QueueItem &item) {
    if (m_session)
        finish("replaced");
    ListeningSession s;
    s.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    s.songId = item.metadata.id;
    s.queueId = item.id;
    s.songKey = item.metadata.resourceKey();
    s.title = item.metadata.customTitle;
    if (s.title.isEmpty())
        s.title = item.metadata.sourceName;
    if (s.title.isEmpty())
        s.title = QFileInfo(item.path).completeBaseName();
    s.artist = item.metadata.artist;
    s.startedAt = s.updatedAt = m_utc();
    s.durationMs = item.metadata.durationMs;
    if (s.durationMs <= 0)
        s.durationMs = m_audio.duration();
    m_session = s;
    m_pending.clear();
    m_coverage.clear();
    m_seeking = false;
    m_playing = true;
    m_dirty = true;
    baseline();
    report(flush());
}
void ListeningHistoryService::sample(bool force) {
    // Retain the final outgoing notification across a gapless handover: the backend's
    // current position already belongs to the incoming song when nextTrackStarted fires.
    const auto tick = m_monotonic(), position = m_notifiedPosition;
    if (!m_session) {
        baseline();
        return;
    }
    const auto elapsed = qMax<qint64>(0, tick - m_tick), delta = position - m_position;
    // Decoder notifications can arrive every 20 ms. Coalesce progress to avoid storing
    // thousands of tiny jitter gaps; playback boundaries still flush their final fraction.
    if (!force && elapsed < 1000 && delta >= 0)
        return;
    if (m_seeking && tick - m_seekTick >= 2500) {
        // A rejected/unconfirmed seek must not disable history indefinitely, or count its jump.
        m_seeking = false;
        baseline();
        return;
    }
    if (m_playing && !m_seeking && m_audio.audioOutputAvailable() && elapsed > 0 && delta > 0 &&
        delta <= elapsed * m_rate + 250) {
        const auto listened = qMin(elapsed, qint64(delta / m_rate));
        const auto end = m_utc();
        // UTC may be adjusted by the OS; never overlap previously accounted intervals.
        const auto start = qMax(end - listened, m_session->updatedAt);
        if (end > start) {
            if (!m_pending.isEmpty() && m_pending.last().end == start)
                m_pending.last().end = end;
            else
                m_pending.append({start, end});
            m_session->listenedMs += end - start;
            m_session->updatedAt = end;
            m_dirty = true;
        }
        m_coverage.append({m_position, position});
        std::sort(m_coverage.begin(), m_coverage.end(), [](auto a, auto b) { return a.start < b.start; });
        QVector<ListeningInterval> merged;
        for (const auto &range : m_coverage) {
            if (!merged.isEmpty() && range.start <= merged.last().end)
                merged.last().end = qMax(merged.last().end, range.end);
            else
                merged.append(range);
        }
        m_coverage = std::move(merged);
    }
    m_position = position;
    m_tick = tick;
    m_rate = m_audio.playbackRate();
}
Result<void> ListeningHistoryService::flush(bool notify) {
    while (!m_retry.isEmpty()) {
        const auto &write = m_retry.first();
        const auto result = m_repository.save(write.session, write.intervals);
        if (!result)
            return result;
        m_retry.removeFirst();
        if (notify)
            emit changed();
    }
    if (!m_session || !m_dirty)
        return {};
    const auto result = m_repository.save(*m_session, m_pending);
    if (result) {
        m_pending.clear();
        m_dirty = false;
        if (notify)
            emit changed();
    }
    return result;
}
void ListeningHistoryService::finish(const QString &reason) {
    if (!m_session)
        return;
    sample(true);
    m_session->reason = reason;
    m_dirty = true;
    m_session->updatedAt = qMax(m_session->updatedAt, m_utc());
    qint64 covered = 0;
    for (const auto &range : m_coverage)
        covered += range.end - range.start;
    const auto duration = m_session->durationMs;
    m_session->completed = reason == "ended" && duration > 0 && covered >= duration * .95;
    const auto saved = flush();
    if (!saved)
        m_retry.append({*m_session, m_pending});
    report(saved);
    m_session.reset();
    m_pending.clear();
    m_coverage.clear();
    m_playing = false;
}
Result<HistoryPage> ListeningHistoryService::list(const HistoryFilter &filter) {
    sample();
    const auto result = flush(false);
    if (!result)
        return result.error();
    return m_repository.list(filter);
}
Result<ListeningStats> ListeningHistoryService::statistics(const QTimeZone &zone) {
    sample();
    const auto result = flush(false);
    if (!result)
        return result.error();
    const auto today = QDateTime::fromMSecsSinceEpoch(m_utc(), zone).date();
    QVector<ListeningDay> days;
    for (int i = 29; i >= 0; --i) {
        const auto date = today.addDays(-i);
        days.append({date.toString(Qt::ISODate), date.startOfDay(zone).toMSecsSinceEpoch(),
                     date.addDays(1).startOfDay(zone).toMSecsSinceEpoch()});
    }
    return m_repository.statistics(days,
                                   today.addDays(1 - today.dayOfWeek()).startOfDay(zone).toMSecsSinceEpoch());
}
Result<void> ListeningHistoryService::clear() {
    const auto result = m_repository.clear();
    if (!result)
        return result;
    // Suppress the current occurrence too: pause/resume must not resurrect cleared history.
    m_session.reset();
    m_pending.clear();
    m_coverage.clear();
    m_retry.clear();
    baseline();
    emit changed();
    return {};
}
} // namespace nekotune
