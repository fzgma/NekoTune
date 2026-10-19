#include "application/playback/audio_settings_service.h"
namespace nekotune {
AudioSettingsService::AudioSettingsService(IPlaybackBackend &backend, PlayerEngine &player, AudioSettings settings,
                                           Saver save)
    : m_backend(backend), m_player(player), m_settings(std::move(settings)), m_startupPreference(m_settings.backend),
      m_save(std::move(save)) {
    m_player.setCrossfadeSeconds(m_settings.crossfadeSeconds);
    m_backend.setEqualizer(m_settings.equalizer);
    m_backend.setReplayGain(m_settings.replayGain);
    connect(&backend, &IPlaybackBackend::backendChanged, this, &AudioSettingsService::changed);
    connect(&backend, &IPlaybackBackend::replayGainChanged, this, &AudioSettingsService::changed);
}
Result<void> AudioSettingsService::update(const AudioSettings &settings) {
    auto valid = validateAudioSettings(settings);
    if (!valid)
        return valid;
    if (settings == m_settings)
        return {};
    if (m_save && !m_save(settings))
        return failure("Unable to save audio settings", ErrorCode::Storage);
    m_settings = settings;
    m_player.setCrossfadeSeconds(settings.crossfadeSeconds);
    m_backend.setEqualizer(settings.equalizer);
    m_backend.setReplayGain(settings.replayGain);
    emit changed();
    return {};
}
} // namespace nekotune
