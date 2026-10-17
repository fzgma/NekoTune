#pragma once
#include "domain/library/library_types.h"
#include "domain/playback/audio_settings.h"
#include "domain/playback/playback_mode.h"
namespace nekotune {
inline TransitionStrategy transitionStrategyFor(const SongMetadata &current, const SongMetadata &next,
                                                PlaybackMode mode, bool priority, double crossfadeSeconds) {
    if (crossfadeSeconds <= 0 || mode == PlaybackMode::RepeatOne)
        return TransitionStrategy::Gapless;
    const auto artist = [](const SongMetadata &song) {
        return (song.albumArtist.trimmed().isEmpty() ? song.artist : song.albumArtist).trimmed().toCaseFolded();
    };
    const bool albumSequence = mode != PlaybackMode::Shuffle && !priority && !current.album.trimmed().isEmpty() &&
                               current.album.trimmed().toCaseFolded() == next.album.trimmed().toCaseFolded() &&
                               !artist(current).isEmpty() && artist(current) == artist(next) &&
                               current.trackNumber > 0 && next.trackNumber == current.trackNumber + 1 &&
                               current.discNumber == next.discNumber;
    return albumSequence ? TransitionStrategy::Gapless : TransitionStrategy::Crossfade;
}
} // namespace nekotune
