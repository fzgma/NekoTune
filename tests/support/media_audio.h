#pragma once
#include "audio_backend.h"
#include <optional>

class MediaAudio final : public TestAudioBackend {
  public:
    QUrl url;
    qint64 clock = 0;
    double level = .8;
    bool allowSeek = true;
    bool deferSeek = false;
    std::optional<qint64> pendingSeek;
    void setSource(const QUrl &value) override {
        pendingSeek.reset(); url = value; clock = 0; emit seekableChanged();
    }
    QUrl source() const override { return url; }
    void play() override { emit stateChanged(nekotune::PlayerState::Playing); }
    void pause() override { emit stateChanged(nekotune::PlayerState::Paused); }
    void stop() override {
        pendingSeek.reset(); clock = 0; emit positionChanged(clock); emit stateChanged(nekotune::PlayerState::Stopped);
    }
    void seek(qint64 value) override {
        pendingSeek = value;
        if (!deferSeek) confirmSeek();
    }
    void confirmSeek() {
        if (!pendingSeek) return;
        clock = *pendingSeek; pendingSeek.reset(); emit positionChanged(clock); emit seeked(clock);
    }
    bool seekable() const override { return allowSeek && !url.isEmpty(); }
    void setVolume(double value) override { level = value; }
    qint64 position() const override { return clock; }
    qint64 duration() const override { return 30000; }
    double volume() const override { return level; }
    nekotune::AudioMetadata metadata() const override { return {}; }
};
