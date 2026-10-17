#include "controllers/playback_controller.h"
PlaybackController::PlaybackController(IpcClient &client) : FeatureController(client) {
    connect(&client, &IpcClient::eventReceived, this, [this](const QJsonObject &event) {
        auto name = event.value("event").toString();
        if (name.startsWith("player."))
            apply(event);
        else if (name == "server.connected")
            apply(event.value("data").toObject());
        if (name == "player.seeked")
            emit seeked(event.value("position").toInteger(), event.value("queue_id").toInt());
    });
    connect(&client, &IpcClient::responseReceived, this, [this](const QString &, const QJsonObject &data) {
        if ((data.contains("state") && data.contains("volume")) || data.contains("audio_settings"))
            apply(data);
    });
}
void PlaybackController::apply(const QJsonObject &data) {
    if (data.contains("audio_settings") && m_audioSettings != data.value("audio_settings").toObject().toVariantMap()) {
        m_audioSettings = data.value("audio_settings").toObject().toVariantMap();
        emit audioSettingsChanged();
    }
    if (data.contains("playback_rate_supported") && m_playbackRateSupported != data.value("playback_rate_supported").toBool()) {
        m_playbackRateSupported = data.value("playback_rate_supported").toBool();
        emit playbackRateSupportedChanged();
    }
    if (data.contains("playback_rate") && m_playbackRate != data.value("playback_rate").toDouble()) {
        m_playbackRate = data.value("playback_rate").toDouble();
        emit playbackRateChanged();
    }
    if (data.contains("capabilities") && m_capabilities != data.value("capabilities").toObject().toVariantMap()) {
        m_capabilities = data.value("capabilities").toObject().toVariantMap();
        emit capabilitiesChanged();
    }
    // Events are partial patches, while status replies are full snapshots; omitted fields retain
    // their values, and an explicit zero volume must still be applied as mute.
    if (data.contains("playback_mode") && m_playbackMode != data.value("playback_mode").toString()) {
        m_playbackMode = data.value("playback_mode").toString();
        emit playbackModeChanged();
    }
    if (data.contains("state") && m_state != data.value("state").toString()) {
        m_state = data.value("state").toString();
        emit stateChanged();
    }
    if (data.contains("position") && m_position != data.value("position").toDouble()) {
        m_position = data.value("position").toDouble();
        emit positionChanged();
    }
    if (data.contains("duration") && m_duration != data.value("duration").toDouble()) {
        m_duration = data.value("duration").toDouble();
        emit durationChanged();
    }
    if (data.contains("volume") && m_volume != data.value("volume").toDouble()) {
        if (m_volume > 0)
            m_lastVolume = m_volume;
        m_volume = data.value("volume").toDouble();
        emit volumeChanged();
    }
    if (data.contains("song") && m_song != data.value("song").toObject().toVariantMap()) {
        m_song = data.value("song").toObject().toVariantMap();
        emit songChanged();
    }
}
