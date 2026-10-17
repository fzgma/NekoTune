#pragma once
#include "domain/playback/playback_backend.h"
#include "infrastructure/playback/audio_output_devices.h"
#include <QThread>
#include <memory>
namespace nekotune {
class PcmOutputWorker;
class FFmpegPlaybackBackend final : public IPlaybackBackend {
    Q_OBJECT
  public:
    FFmpegPlaybackBackend();
    ~FFmpegPlaybackBackend() override;
    QString backendName() const override { return QStringLiteral("ffmpeg"); }
    BackendCapabilities backendCapabilities() const override { return {true, true, true}; }
    void setSource(const QUrl &url) override;
    QUrl source() const override { return m_source; }
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 position) override;
    bool seekable() const override { return m_seekable; }
    void setVolume(double volume) override;
    double volume() const override { return m_volume; }
    void setPlaybackRate(double) override {}
    double playbackRate() const override { return 1; }
    bool supportsPlaybackRate() const override { return false; }
    qint64 position() const override { return m_position; }
    qint64 duration() const override { return m_duration; }
    AudioMetadata metadata() const override { return m_metadata; }
    QList<AudioOutputDevice> audioOutputs() const override { return m_outputs.outputs(); }
    QString activeAudioOutputId() const override { return m_outputs.activeId(); }
    bool audioOutputAvailable() const override { return m_outputs.available(); }
    void setAudioOutput(const QString &id, const QString &port = {}) override { m_outputs.select(id, port); }
    void setAudioOutputPort(const QString &id, const QString &port, OutputCompletion done) override {
        m_outputs.selectPort(id, port, std::move(done));
    }
    void suspendAudioOutput() override;
    void shutdown() override;
    void preloadNext(const PreloadRequest &request) override;
    void cancelPreload() override;
    void authorizeTransition(quint64 token) override;
    void finishAfterCurrent() override;
    void setEqualizer(const EqualizerSettings &settings) override;
    int outputSampleRate() const override { return m_sampleRate; }

  private:
    friend class PcmOutputWorker;
    void dispatch(std::function<void(PcmOutputWorker &)> operation);
    AudioOutputDevices m_outputs;
    QThread m_thread;
    PcmOutputWorker *m_worker = nullptr;
    QUrl m_source;
    AudioMetadata m_metadata;
    qint64 m_position = 0, m_duration = 0;
    quint64 m_generation = 0;
    double m_volume = .8;
    bool m_seekable = false;
    int m_sampleRate = 0;
};
} // namespace nekotune
