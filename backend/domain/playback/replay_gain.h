#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <optional>

namespace nekotune {
struct ReplayGainSettings {
    QString mode = QStringLiteral("off");
    double preampDb = 0;
    bool operator==(const ReplayGainSettings &) const = default;
};
struct ReplayGainData {
    std::optional<double> trackGainDb, albumGainDb, trackPeak, albumPeak;
    QString trackSource, albumSource;
    bool operator==(const ReplayGainData &) const = default;
};
struct ReplayGainStatus {
    double gainDb = 0;
    QString source = QStringLiteral("off");
    bool peakLimited = false;
};
inline ReplayGainStatus resolveReplayGain(const ReplayGainSettings &settings, const ReplayGainData &data,
                                          bool supported = true) {
    if (!supported)
        return {0, QStringLiteral("unavailable"), false};
    if (settings.mode == "off")
        return {};
    const bool album = settings.mode == "album";
    const auto gain = album ? data.albumGainDb : data.trackGainDb;
    const auto peak = album ? data.albumPeak : data.trackPeak;
    if (!gain || !std::isfinite(*gain))
        return {0, QStringLiteral("missing"), false};
    double applied = std::clamp(*gain + settings.preampDb, -60.0, 60.0);
    bool limited = false;
    if (peak && std::isfinite(*peak) && *peak > 0) {
        const double ceiling = -20 * std::log10(*peak);
        limited = applied > ceiling;
        applied = std::min(applied, ceiling);
    }
    return {applied, album ? data.albumSource : data.trackSource, limited};
}
// Only missing modes are filled; peaks must belong to the same measurement as their gain.
inline ReplayGainData withReplayGainFallback(ReplayGainData tags, const ReplayGainData &analysis) {
    if (!tags.trackGainDb) {
        tags.trackGainDb = analysis.trackGainDb;
        tags.trackPeak = analysis.trackPeak;
        tags.trackSource = analysis.trackSource;
    }
    if (!tags.albumGainDb) {
        tags.albumGainDb = analysis.albumGainDb;
        tags.albumPeak = analysis.albumPeak;
        tags.albumSource = analysis.albumSource;
    }
    return tags;
}
inline ReplayGainData replaceReplayGainAnalysis(ReplayGainData data, const ReplayGainData &analysis) {
    if (data.trackSource != "tag") {
        data.trackGainDb.reset();
        data.trackPeak.reset();
        data.trackSource.clear();
    }
    if (data.albumSource != "tag") {
        data.albumGainDb.reset();
        data.albumPeak.reset();
        data.albumSource.clear();
    }
    return withReplayGainFallback(data, analysis);
}
struct LoudnessAnalysisGroup {
    QStringList paths;
    bool album = false;
};
struct LoudnessAnalysisStatus {
    bool running = false, cancelling = false, cancelled = false;
    int completed = 0, total = 0;
    QString error;
};
} // namespace nekotune
