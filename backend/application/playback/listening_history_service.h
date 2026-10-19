#pragma once
#include "application/playback/player_engine.h"
#include "domain/playback/listening_history.h"
#include <QElapsedTimer>
#include <QTimeZone>

namespace nekotune {
// One service per backend. Frontends can query/clear, never submit listening time.
class ListeningHistoryService final : public QObject {
    Q_OBJECT
  public:
    using Clock = std::function<qint64()>;
    ListeningHistoryService(PlayerEngine &, IPlaybackBackend &, IListeningHistoryRepository &,
                            Clock utcClock = {}, Clock monotonicClock = {});
    Result<void> recover() { return m_repository.recover(); }
    Result<HistoryPage> list(const HistoryFilter &filter);
    Result<ListeningStats> statistics(const QTimeZone &zone);
    Result<void> clear();
  signals:
    void changed();
    void errorOccurred(const QString &message);

  private:
    void start(const QueueItem &item);
    void finish(const QString &reason);
    void sample(bool force = false);
    void baseline();
    Result<void> flush(bool notify = true);
    void report(const Result<void> &result);
    IPlaybackBackend &m_audio;
    IListeningHistoryRepository &m_repository;
    Clock m_utc, m_monotonic;
    QElapsedTimer m_clock;
    QTimer m_checkpoint;
    std::optional<ListeningSession> m_session;
    QVector<ListeningInterval> m_pending, m_coverage;
    struct PendingWrite {
        ListeningSession session;
        QVector<ListeningInterval> intervals;
    };
    QList<PendingWrite> m_retry;
    bool m_playing = false, m_seeking = false, m_dirty = false;
    qint64 m_position = 0, m_notifiedPosition = 0, m_tick = 0, m_seekTick = 0;
    double m_rate = 1.0;
};
} // namespace nekotune
