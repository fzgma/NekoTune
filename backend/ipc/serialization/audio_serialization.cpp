#include "ipc/serialization/audio_serialization.h"
#include <QJsonArray>
namespace nekotune {
QJsonObject audioSettingsJson(const AudioSettings &settings) {
    QJsonArray gains;
    for (double gain : settings.equalizer.gains)
        gains.append(gain);
    return {{"backend", settings.backend},
            {"crossfade_seconds", settings.crossfadeSeconds},
            {"replay_gain", QJsonObject{{"mode", settings.replayGain.mode}, {"preamp_db", settings.replayGain.preampDb}}},
            {"equalizer", QJsonObject{{"enabled", settings.equalizer.enabled},
                                      {"auto_headroom", settings.equalizer.autoHeadroom},
                                      {"preamp_db", settings.equalizer.preampDb},
                                      {"gains", gains}}}};
}
Result<AudioSettings> parseAudioSettings(const QJsonObject &patch, AudioSettings settings) {
    if (patch.contains("replay_gain")) {
        if (!patch.value("replay_gain").isObject()) return failure("replay_gain must be an object");
        const auto gain = patch.value("replay_gain").toObject();
        if (gain.contains("mode")) {
            if (!gain.value("mode").isString()) return failure("ReplayGain mode must be a string");
            settings.replayGain.mode = gain.value("mode").toString();
        }
        if (gain.contains("preamp_db")) {
            if (!gain.value("preamp_db").isDouble()) return failure("ReplayGain preamp_db must be a number");
            settings.replayGain.preampDb = gain.value("preamp_db").toDouble();
        }
    }
    if (patch.contains("backend")) {
        if (!patch.value("backend").isString())
            return failure("backend must be a string");
        settings.backend = patch.value("backend").toString();
    }
    if (patch.contains("crossfade_seconds")) {
        if (!patch.value("crossfade_seconds").isDouble())
            return failure("crossfade_seconds must be a number");
        settings.crossfadeSeconds = patch.value("crossfade_seconds").toDouble();
    }
    if (patch.contains("equalizer")) {
        if (!patch.value("equalizer").isObject())
            return failure("equalizer must be an object");
        const auto eq = patch.value("equalizer").toObject();
        for (const auto &key : {"enabled", "auto_headroom"}) {
            if (eq.contains(key) && !eq.value(key).isBool())
                return failure(QString::fromUtf8(key) + " must be a boolean");
        }
        if (eq.contains("enabled"))
            settings.equalizer.enabled = eq.value("enabled").toBool();
        if (eq.contains("auto_headroom"))
            settings.equalizer.autoHeadroom = eq.value("auto_headroom").toBool();
        if (eq.contains("preamp_db")) {
            if (!eq.value("preamp_db").isDouble())
                return failure("preamp_db must be a number");
            settings.equalizer.preampDb = eq.value("preamp_db").toDouble();
        }
        if (eq.contains("gains") && eq.contains("preset"))
            return failure("Choose gains or preset, not both");
        if (eq.contains("preset")) {
            if (!eq.value("preset").isString())
                return failure("preset must be a string");
            const auto name = eq.value("preset").toString();
            if (name != "flat" && name != "bass" && name != "vocal" && name != "bright")
                return failure("Unknown EQ preset");
            settings.equalizer.gains = equalizerPreset(name);
        }
        if (eq.contains("gains")) {
            if (!eq.value("gains").isArray() || eq.value("gains").toArray().size() != 10)
                return failure("gains must contain 10 numbers");
            const auto gains = eq.value("gains").toArray();
            for (int i = 0; i < 10; ++i) {
                if (!gains[i].isDouble())
                    return failure("gains must contain 10 numbers");
                settings.equalizer.gains[size_t(i)] = gains[i].toDouble();
            }
        }
    }
    auto valid = validateAudioSettings(settings);
    if (!valid)
        return valid.error();
    return settings;
}
} // namespace nekotune
