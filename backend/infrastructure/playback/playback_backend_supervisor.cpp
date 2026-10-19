#include "infrastructure/playback/playback_backend_supervisor.h"
#include "infrastructure/playback/ffmpeg_playback_backend.h"
#include "infrastructure/playback/qt_playback_backend.h"
#include <QTimer>
namespace nekotune {
PlaybackBackendSupervisor::PlaybackBackendSupervisor(QString preference, Factory preferred, Factory fallback)
    : m_preference(std::move(preference)), m_preferredFactory(std::move(preferred)),
      m_fallbackFactory(std::move(fallback)) {
    if (!m_preferredFactory) {
        const bool qt = m_preference == "qt";
        m_preferredFactory = [qt]() -> std::unique_ptr<IPlaybackBackend> {
            if (qt)
                return std::make_unique<QtPlaybackBackend>();
            return std::make_unique<FFmpegPlaybackBackend>();
        };
    }
    if (!m_fallbackFactory)
        m_fallbackFactory = [] { return std::make_unique<QtPlaybackBackend>(); };
    install(m_preferredFactory());
}
PlaybackBackendSupervisor::~PlaybackBackendSupervisor() { m_active.reset(); }
void PlaybackBackendSupervisor::releaseExcept(const QUrl &keep) {
    const auto sources = m_sources;
    for (const auto &source : sources)
        if (source != keep) {
            m_sources.remove(source);
            emit sourceReleased(source);
        }
}
void PlaybackBackendSupervisor::install(std::unique_ptr<IPlaybackBackend> backend, const QUrl &keep) {
    m_active.reset();
    releaseExcept(keep);
    m_active = std::move(backend);
    m_active->setParent(this);
    auto *active = m_active.get();
#define FORWARD(SIGNAL, SIGNATURE, ARGUMENTS)                                                                          \
    connect(active, &IPlaybackBackend::SIGNAL, this, [this, active] SIGNATURE {                                        \
        if (m_active.get() == active)                                                                                  \
            emit SIGNAL ARGUMENTS;                                                                                     \
    });
    FORWARD(stateChanged, (PlayerState state), (state))
    FORWARD(positionChanged, (qint64 value), (value))
    FORWARD(seeked, (qint64 value), (value))
    FORWARD(seekableChanged, (), ())
    FORWARD(durationChanged, (qint64 value), (value))
    FORWARD(metadataChanged, (), ())
    FORWARD(ended, (), ())
    FORWARD(failed, (const QString &message), (message))
    FORWARD(audioOutputsChanged, (), ())
    FORWARD(audioOutputAvailabilityChanged, (bool available), (available))
    FORWARD(audioOutputError, (const QString &message), (message))
    FORWARD(nextReady, (quint64 token), (token))
    FORWARD(nextFailed, (quint64 token, const QString &message), (token, message))
    FORWARD(backendChanged, (), ())
    FORWARD(replayGainChanged, (), ())
#undef FORWARD
    connect(active, &IPlaybackBackend::nextTrackStarted, this, [this, active](quint64 token) {
        if (m_active.get() != active)
            return;
        m_triedFallback = false;
        emit nextTrackStarted(token);
    });
    connect(active, &IPlaybackBackend::sourceReleased, this, [this](const QUrl &source) {
        if (m_sources.remove(source))
            emit sourceReleased(source);
    });
    connect(active, &IPlaybackBackend::decoderFailure, this, &PlaybackBackendSupervisor::decoderFailed);
    active->setVolume(m_volume);
    active->setPlaybackRate(m_rate);
    active->setEqualizer(m_equalizer);
    active->setReplayGain(m_replayGain);
    active->setAudioOutput(m_device, m_port);
}
void PlaybackBackendSupervisor::setSource(const QUrl &url) {
    ++m_generation;
    m_triedFallback = false;
    m_playIntent = false;
    if (!url.isEmpty() && m_active->backendName() != m_preference) {
        install(m_preferredFactory(), url);
        emit backendChanged();
    }
    if (!url.isEmpty())
        m_sources.insert(url);
    const auto previous = m_active->source();
    m_active->setSource(url);
    if (m_active->backendName() == "qt" && previous != url && m_sources.remove(previous))
        emit sourceReleased(previous);
}
void PlaybackBackendSupervisor::decoderFailed(const QString &message, bool eligible) {
    if (!eligible || m_triedFallback || m_preference != "ffmpeg") {
        emit failed(message);
        return;
    }
    m_triedFallback = true;
    const auto generation = m_generation;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation != m_generation)
            return;
        const auto url = source();
        const bool resume = m_playIntent;
        install(m_fallbackFactory(), url);
        m_active->setSource(url);
        emit backendChanged();
        emit seekableChanged();
        if (resume && audioOutputAvailable())
            m_active->play();
    });
}
} // namespace nekotune
