#include "platform/system_media_bridge.h"
#include <QDebug>
#include <QPointer>
#include <cmath>
#include <limits>

namespace {
qint64 microseconds(double milliseconds) {
    if (!std::isfinite(milliseconds) || milliseconds <= 0)
        return 0;
    if (milliseconds >= double(std::numeric_limits<qint64>::max() / 1000))
        return std::numeric_limits<qint64>::max();
    return qint64(milliseconds) * 1000;
}
}
SystemMediaBridge::SystemMediaBridge(IpcClient &client, PlaybackController &playback, QObject *parent)
    : QObject(parent), m_client(client), m_playback(playback) {
    connect(&client, &IpcClient::connectedChanged, this, [this] {
        if (!m_client.connected())
            setReady(false);
    });
    connect(&client, &IpcClient::eventReceived, this, [this](const QJsonObject &event) {
        if (m_stopping || event.value("event") != "server.connected")
            return;
        const auto data = event.value("data").toObject();
        if (!m_client.connected() || !data.contains("state") || !data.contains("volume") ||
            !data.contains("song") || !data.contains("capabilities"))
            return;
        ++m_generation;
        setReady(true);
        emit snapshotChanged();
    });
    for (auto signal : {&PlaybackController::stateChanged, &PlaybackController::songChanged,
                        &PlaybackController::durationChanged, &PlaybackController::volumeChanged,
                        &PlaybackController::positionChanged, &PlaybackController::capabilitiesChanged,
                        &PlaybackController::playbackRateChanged, &PlaybackController::playbackRateSupportedChanged})
        connect(&playback, signal, this, &SystemMediaBridge::snapshotChanged);
    connect(&playback, &PlaybackController::seeked, this, [this](qint64 position, int id) {
        if (m_ready && id == queueId())
            emit seeked(microseconds(double(position)));
    });
}
void SystemMediaBridge::setReady(bool ready) {
    if (m_ready == ready)
        return;
    m_ready = ready;
    emit readyChanged();
}
QString SystemMediaBridge::trackId() const {
    return queueId() > 0 ? QStringLiteral("/com/nekotune/track/session%1/queue%2").arg(m_generation).arg(queueId())
                         : QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack");
}
QString SystemMediaBridge::playbackStatus() const {
    const auto state = m_playback.state();
    if (state == "playing") return QStringLiteral("Playing");
    if (state == "paused" || state == "loading") return QStringLiteral("Paused");
    return QStringLiteral("Stopped");
}
qint64 SystemMediaBridge::positionUs() const {
    if (queueId() <= 0) return 0;
    const auto position = microseconds(m_playback.position());
    return durationUs() > 0 ? qMin(position, durationUs()) : position;
}
qint64 SystemMediaBridge::durationUs() const { return microseconds(m_playback.duration()); }
void SystemMediaBridge::request(const QString &method, const QJsonObject &params, Completion done) {
    if (!m_ready || m_stopping) {
        done(QStringLiteral("Backend is not ready"));
        return;
    }
    m_client.request(method, params,
        [guard = QPointer<SystemMediaBridge>(this), done = std::move(done), method]
        (const QJsonObject &, const QString &error) {
        if (!guard) return;
        if (!error.isEmpty()) qWarning() << "System media command failed:" << method << error;
        done(error);
    }, false, 5000);
}
void SystemMediaBridge::shutdown() {
    m_stopping = true;
    setReady(false);
}
