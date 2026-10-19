#pragma once
#include "domain/result.h"
#include "domain/playback/replay_gain.h"
#include <QString>
#include <array>
#include <cmath>

namespace nekotune {
inline constexpr std::array<int, 10> EqualizerFrequencies{31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
enum class TransitionStrategy { Normal, Gapless, Crossfade };
struct BackendCapabilities {
    bool gapless = false;
    bool crossfade = false;
    bool equalizer = false;
    bool replayGain = false;
    bool operator==(const BackendCapabilities &) const = default;
};
struct EqualizerSettings {
    bool enabled = false;
    bool autoHeadroom = true;
    double preampDb = 0;
    std::array<double, 10> gains{};
    bool operator==(const EqualizerSettings &) const = default;
};
struct AudioSettings {
    QString backend = QStringLiteral("ffmpeg");
    double crossfadeSeconds = 0;
    EqualizerSettings equalizer;
    ReplayGainSettings replayGain;
    bool operator==(const AudioSettings &) const = default;
};
inline Result<void> validateAudioSettings(const AudioSettings &settings) {
    if (settings.replayGain.mode != "off" && settings.replayGain.mode != "track" && settings.replayGain.mode != "album")
        return failure("ReplayGain mode must be off, track or album");
    if (!std::isfinite(settings.replayGain.preampDb) || std::abs(settings.replayGain.preampDb) > 12)
        return failure("ReplayGain preamp_db must be between -12 and 12");
    if (settings.backend != "ffmpeg" && settings.backend != "qt")
        return failure("backend must be ffmpeg or qt");
    if (!std::isfinite(settings.crossfadeSeconds) || settings.crossfadeSeconds < 0 || settings.crossfadeSeconds > 12)
        return failure("crossfade_seconds must be between 0 and 12");
    const auto validGain = [](double gain) { return std::isfinite(gain) && gain >= -12 && gain <= 12; };
    if (!validGain(settings.equalizer.preampDb))
        return failure("preamp_db must be between -12 and 12");
    for (double gain : settings.equalizer.gains)
        if (!validGain(gain))
            return failure("EQ gains must be between -12 and 12");
    return {};
}
inline std::array<double, 10> equalizerPreset(const QString &name) {
    if (name == "bass")
        return {6, 5, 4, 2, 0, 0, 0, 0, 0, 0};
    if (name == "vocal")
        return {-2, -2, -1, 0, 2, 3, 3, 2, 0, -1};
    if (name == "bright")
        return {0, 0, 0, -1, -1, 0, 1, 3, 4, 4};
    return {};
}
} // namespace nekotune
