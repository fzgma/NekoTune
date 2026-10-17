#pragma once
#include "domain/playback/audio_settings.h"
#include <QJsonObject>
namespace nekotune {
QJsonObject audioSettingsJson(const AudioSettings &settings);
Result<AudioSettings> parseAudioSettings(const QJsonObject &patch, AudioSettings settings = {});
} // namespace nekotune
