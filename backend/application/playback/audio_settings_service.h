#pragma once
#include "application/playback/player_engine.h"
namespace nekotune {
class AudioSettingsService final : public QObject {
    Q_OBJECT
  public:
    using Saver = std::function<bool(const AudioSettings &)>;
    AudioSettingsService(IPlaybackBackend &backend, PlayerEngine &player, AudioSettings settings = {}, Saver save = {});
    AudioSettings settings() const { return m_settings; }
    QString activeBackend() const { return m_backend.backendName(); }
    BackendCapabilities capabilities() const { return m_backend.backendCapabilities(); }
    bool restartRequired() const { return m_settings.backend != m_startupPreference; }
    int sampleRate() const { return m_backend.outputSampleRate(); }
    Result<void> update(const AudioSettings &settings);
    ReplayGainStatus replayGainStatus() const {
        return resolveReplayGain(m_settings.replayGain, m_backend.replayGainData(), capabilities().replayGain);
    }
    using AnalysisStarter = std::function<Result<void>(const QVector<int> &)>;
    void setAnalysisActions(AnalysisStarter start, std::function<void()> cancel) {
        m_startAnalysis = std::move(start); m_cancelAnalysis = std::move(cancel);
    }
    Result<void> startAnalysis(const QVector<int> &ids) {
        return m_startAnalysis ? m_startAnalysis(ids) : Result<void>(failure("Analysis unavailable", ErrorCode::Unavailable));
    }
    void cancelAnalysis() { if (m_cancelAnalysis) m_cancelAnalysis(); }
    LoudnessAnalysisStatus analysisStatus() const { return m_analysis; }
    void setAnalysisStatus(const LoudnessAnalysisStatus &status) { m_analysis = status; emit changed(); }
  signals:
    void changed();

  private:
    IPlaybackBackend &m_backend;
    PlayerEngine &m_player;
    AudioSettings m_settings;
    QString m_startupPreference;
    Saver m_save;
    AnalysisStarter m_startAnalysis;
    std::function<void()> m_cancelAnalysis;
    LoudnessAnalysisStatus m_analysis;
};
} // namespace nekotune
