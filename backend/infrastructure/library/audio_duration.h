#pragma once
#include <QString>
#include <atomic>
#include "domain/library/library_types.h"
#include "domain/playback/replay_gain.h"
struct AVDictionary;

namespace nekotune {
struct AudioFileInfo {
    qint64 durationMs = 0;
    EmbeddedMetadata metadata;
    ReplayGainData replayGain;
};
ReplayGainData readReplayGainTags(AVDictionary *container, AVDictionary *stream);
AudioFileInfo readAudioFileInfo(const QString &path, const std::atomic_bool &cancelled);
// Returns milliseconds, or zero when the local audio cannot be inspected.
/// Read local audio duration without requiring a live playback session.
/// Zero represents unavailable/probe-failed duration, not a zero-length song.
/// The cancellation flag is shared with the import worker's shutdown path.
/// FFmpeg probing also has a bounded interrupt deadline and format list.
/// Tags and timing metadata are inspected; audio is not decoded for playback.
qint64 readAudioDuration(const QString &path,
                         const std::atomic_bool &cancelled);
} // namespace nekotune
