#include "infrastructure/playback/qt_playback_backend.h"
#include <QAudioDevice>
#include <QMediaMetaData>
#include <QPointer>
namespace nekotune {
// Translate Qt Multimedia states into stable domain state values.
// Media-status loading is separate from playback-state transitions.
// Only EndOfMedia requests natural navigation through PlayerEngine.
// Decoder errors also go through the application, which owns queue repair.
// No automatic playlist/shuffle policy is implemented inside this adapter.
QtPlaybackBackend::QtPlaybackBackend() {
    m_outputs.setParent(this);
    m_audio.setVolume(.8);
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    m_player.setPitchCompensation(true);
#endif
    m_seekTimeout.setSingleShot(true);
    m_seekTimeout.setInterval(2000);
    connect(&m_seekTimeout, &QTimer::timeout, this, [this] { m_seekTarget.reset(); });
    connect(&m_player, &QMediaPlayer::seekableChanged, this, &IPlaybackBackend::seekableChanged);
    connect(&m_outputs, &AudioOutputDevices::changed, this, &IPlaybackBackend::audioOutputsChanged);
    connect(&m_outputs, &AudioOutputDevices::availabilityChanged, this, [this](bool available) {
        emit audioOutputAvailabilityChanged(available);
        if (!available) { m_player.pause(); m_player.setAudioOutput(nullptr); }
    });
    connect(&m_outputs, &AudioOutputDevices::deviceChanged, this, [this] {
        if (m_outputs.available()) {
            m_audio.setDevice(m_outputs.device());
            m_player.setAudioOutput(&m_audio);
        }
    });
    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, [this](auto state) {
        emit stateChanged(state == QMediaPlayer::PlayingState  ? PlayerState::Playing
                          : state == QMediaPlayer::PausedState ? PlayerState::Paused
                                                               : PlayerState::Stopped);
    });
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](auto status) {
        if (status == QMediaPlayer::EndOfMedia)
            emit ended();
        else if (status == QMediaPlayer::LoadingMedia)
            emit stateChanged(PlayerState::Loading);
        else if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)
            emit metadataChanged();
    });
    connect(&m_player, &QMediaPlayer::positionChanged, this, &IPlaybackBackend::positionChanged);
    connect(&m_player, &QMediaPlayer::positionChanged, this, &QtPlaybackBackend::confirmSeek);
    connect(&m_player, &QMediaPlayer::durationChanged, this, &IPlaybackBackend::durationChanged);
    connect(&m_player, &QMediaPlayer::metaDataChanged, this, &IPlaybackBackend::metadataChanged);
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error) {
        const auto message = m_player.errorString();
        // Losing an output is not evidence that the current audio file is corrupt.
        // Keep format/decode failures on the existing media-error path.
        if (error == QMediaPlayer::ResourceError && !audioOutputAvailable()) {
            suspendAudioOutput();
            emit audioOutputError(message);
            return;
        }
        emit failed(message);
    });
}
bool QtPlaybackBackend::supportsPlaybackRate() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    return m_player.pitchCompensationAvailability() != QMediaPlayer::PitchCompensationAvailability::Unavailable
           && m_player.pitchCompensation();
#else
    // Older Qt backends cannot guarantee pitch preservation.
    return false;
#endif
}
void QtPlaybackBackend::setPlaybackRate(double rate) {
    if (rate == 1.0 || supportsPlaybackRate())
        m_player.setPlaybackRate(rate);
}
void QtPlaybackBackend::setSource(const QUrl &url) {
    m_seekTarget.reset();
    m_seekTimeout.stop();
    m_player.setSource(url);
    emit seekableChanged();
}
void QtPlaybackBackend::stop() {
    m_seekTarget.reset();
    m_seekTimeout.stop();
    m_player.stop();
}
void QtPlaybackBackend::seek(qint64 value) {
    if (!seekable()) {
        // Preserve ordinary IPC/UI dispatch while Qt decides whether the unloaded
        // or non-seekable source can accept a position; only confirmation is gated.
        m_player.setPosition(value);
        return;
    }
    m_seekTarget = qBound<qint64>(0, value, m_player.duration());
    m_seekClock.restart();
    m_seekTimeout.start();
    m_player.setPosition(*m_seekTarget);
    confirmSeek(m_player.position());
}
void QtPlaybackBackend::confirmSeek(qint64 position) {
    if (!m_seekTarget)
        return;
    // Qt may coalesce the seek with a subsequent playback tick. Report the observed
    // clock only after it reaches the target, allowing elapsed playback and codec rounding.
    const auto elapsed = m_player.playbackState() == QMediaPlayer::PlayingState
                             ? qint64(m_seekClock.elapsed() * m_player.playbackRate()) : 0;
    if (position < *m_seekTarget - 100 || position > *m_seekTarget + elapsed + 100)
        return;
    m_seekTarget.reset();
    m_seekTimeout.stop();
    emit seeked(position);
}
QList<AudioOutputDevice> QtPlaybackBackend::audioOutputs() const { return m_outputs.outputs(); }
bool QtPlaybackBackend::audioOutputAvailable() const { return m_outputs.available(); }
void QtPlaybackBackend::play() {
    if (audioOutputAvailable()) m_player.play();
    else suspendAudioOutput();
}
void QtPlaybackBackend::suspendAudioOutput() {
    m_outputs.suspend();
    m_player.pause();
    m_player.setAudioOutput(nullptr);
}
void QtPlaybackBackend::setAudioOutput(const QString &id, const QString &port) { m_outputs.select(id, port); }
void QtPlaybackBackend::setAudioOutputPort(const QString &id, const QString &port, OutputCompletion done) {
    m_outputs.selectPort(id, port, std::move(done));
}
// Prefer the contributing performer tag for the display/search artist.
// AlbumArtist is a fallback when files omit that more specific field.
// Empty tags are legitimate and are supplemented by library metadata.
// Do not rewrite persisted metadata just because decoder tags differ.
AudioMetadata QtPlaybackBackend::metadata() const {
    auto data = m_player.metaData();
    auto artist = data.stringValue(QMediaMetaData::ContributingArtist);
    if (artist.isEmpty())
        artist = data.stringValue(QMediaMetaData::AlbumArtist);
    return {data.stringValue(QMediaMetaData::Title), artist, data.stringValue(QMediaMetaData::AlbumTitle)};
}
} // namespace nekotune
