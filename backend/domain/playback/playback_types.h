#pragma once
#include "domain/playback/playback_backend.h"
#include "domain/playback/playback_mode.h"
#include "domain/playback/player_queue.h"
namespace nekotune {
struct PlaybackCapabilities {
    bool canPlay = false;
    bool canPause = false;
    bool canSeek = false;
    bool canGoNext = false;
    bool canGoPrevious = false;
    bool operator==(const PlaybackCapabilities &) const = default;
};
/// A value snapshot joins selection, decoder position and stored song timing.
/// An absent song means the queue currently has no selected occurrence.
/// Stopped/paused describe playback state rather than file membership.
/// Duration comes from persisted song metadata; zero means unknown.
/// Volume zero is a meaningful mute value, never a missing-value marker.
/// The IPC serializer converts this aggregate without exposing adapters.
struct PlaybackSnapshot {
    PlayerState state = PlayerState::Stopped;
    qint64 position = 0;
    qint64 duration = 0;
    double volume = .8;
    std::optional<QueueItem> song;
    AudioMetadata metadata;
    PlaybackMode playbackMode = PlaybackMode::Sequential;
    PlaybackCapabilities capabilities;
    double playbackRate = 1.0;
    bool playbackRateSupported = false;
};
} // namespace nekotune
