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
  signals:
    void changed();

  private:
    IPlaybackBackend &m_backend;
    PlayerEngine &m_player;
    AudioSettings m_settings;
    QString m_startupPreference;
    Saver m_save;
};
} // namespace nekotune
