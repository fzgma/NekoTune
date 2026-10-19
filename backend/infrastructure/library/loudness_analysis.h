#pragma once
#include "domain/playback/replay_gain.h"
#include "domain/result.h"
#include <QObject>
#include <QThread>
#include <atomic>
#include <memory>

namespace nekotune {
ReplayGainData readLoudnessCache(const QString &path);
// One cancellable job and one bounded decoder at a time. No database or audio-file writes.
class LoudnessAnalysis final : public QObject {
    Q_OBJECT
  public:
    ~LoudnessAnalysis() override;
    Result<void> start(const QVector<LoudnessAnalysisGroup> &groups);
    void cancel();
    void setPlaying(bool playing) { m_playing = playing; }
    LoudnessAnalysisStatus status() const { return m_status; }
  signals:
    void changed();
    void resultReady(const QString &path, const nekotune::ReplayGainData &data);

  private:
    std::unique_ptr<QThread> m_worker;
    std::atomic_bool m_cancelled{false}, m_playing{false};
    LoudnessAnalysisStatus m_status;
};
} // namespace nekotune
