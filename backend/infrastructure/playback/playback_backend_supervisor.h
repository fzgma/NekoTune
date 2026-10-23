#pragma once
#include "domain/playback/playback_backend.h"
#include <QSet>
#include <memory>
namespace nekotune {
// Stable identity for services. Recreating an adapter never invalidates their references.
class PlaybackBackendSupervisor final : public IPlaybackBackend {
    Q_OBJECT
  public:
    using Factory = std::function<std::unique_ptr<IPlaybackBackend>()>;
    explicit PlaybackBackendSupervisor(QString preference = QStringLiteral("ffmpeg"), Factory preferred = {},
                                       Factory fallback = {});
    ~PlaybackBackendSupervisor() override;
    QString backendName() const override { return m_active->backendName(); }
    BackendCapabilities backendCapabilities() const override { return m_active->backendCapabilities(); }
    bool supportsPcmCapture() const override { return m_active->supportsPcmCapture(); }
    void setPcmCaptureEnabled(bool value) override { m_captureEnabled = value; m_active->setPcmCaptureEnabled(value); }
    QVector<sdk::PcmBlock> takePcmBlocks() override { return m_active->takePcmBlocks(); }
    void setSource(const QUrl &) override;
    QUrl source() const override { return m_active->source(); }
    void play() override {
        m_playIntent = true;
        m_active->play();
    }
    void pause() override {
        m_playIntent = false;
        m_active->pause();
    }
    void stop() override {
        m_playIntent = false;
        ++m_generation;
        m_active->stop();
    }
    void seek(qint64 value) override {
        ++m_generation;
        m_active->seek(value);
    }
    bool seekable() const override { return m_active->seekable(); }
    void setVolume(double value) override {
        m_volume = value;
        m_active->setVolume(value);
    }
    double volume() const override { return m_active->volume(); }
    void setPlaybackRate(double value) override {
        m_rate = value;
        m_active->setPlaybackRate(value);
    }
    double playbackRate() const override { return m_active->playbackRate(); }
    bool supportsPlaybackRate() const override { return m_active->supportsPlaybackRate(); }
    qint64 position() const override { return m_active->position(); }
    qint64 duration() const override { return m_active->duration(); }
    AudioMetadata metadata() const override { return m_active->metadata(); }
    QList<AudioOutputDevice> audioOutputs() const override { return m_active->audioOutputs(); }
    QString activeAudioOutputId() const override { return m_active->activeAudioOutputId(); }
    bool audioOutputAvailable() const override { return m_active->audioOutputAvailable(); }
    void setAudioOutput(const QString &id, const QString &port = {}) override {
        m_device = id;
        m_port = port;
        m_active->setAudioOutput(id, port);
    }
    void setAudioOutputPort(const QString &id, const QString &port, OutputCompletion done) override {
        m_active->setAudioOutputPort(id, port, std::move(done));
    }
    void shutdown() override {
        ++m_generation;
        m_playIntent = false;
        m_active->shutdown();
        releaseExcept({});
    }
    void suspendAudioOutput() override {
        m_playIntent = false;
        m_device.clear();
        m_port.clear();
        m_active->suspendAudioOutput();
    }
    void preloadNext(const PreloadRequest &request) override {
        m_sources.insert(request.source);
        m_active->preloadNext(request);
    }
    void cancelPreload() override { m_active->cancelPreload(); }
    void finishAfterCurrent() override { m_active->finishAfterCurrent(); }
    void authorizeTransition(quint64 token) override { m_active->authorizeTransition(token); }
    void setEqualizer(const EqualizerSettings &settings) override {
        m_equalizer = settings;
        m_active->setEqualizer(settings);
    }
    int outputSampleRate() const override { return m_active->outputSampleRate(); }
    void setReplayGain(const ReplayGainSettings &settings) override {
        m_replayGain = settings; m_active->setReplayGain(settings);
    }
    ReplayGainData replayGainData() const override { return m_active->replayGainData(); }
    void updateReplayGainData(const QUrl &source, const ReplayGainData &data) override {
        m_active->updateReplayGainData(source, data);
    }

  private:
    void install(std::unique_ptr<IPlaybackBackend> backend, const QUrl &keep = {});
    void releaseExcept(const QUrl &keep);
    void decoderFailed(const QString &message, bool eligible);
    QString m_preference, m_device, m_port;
    Factory m_preferredFactory, m_fallbackFactory;
    std::unique_ptr<IPlaybackBackend> m_active;
    QSet<QUrl> m_sources;
    EqualizerSettings m_equalizer;
    ReplayGainSettings m_replayGain;
    double m_volume = .8, m_rate = 1;
    bool m_captureEnabled = false;
    bool m_playIntent = false, m_triedFallback = false;
    quint64 m_generation = 0;
};
} // namespace nekotune
