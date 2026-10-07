#pragma once
#include "controllers/playback_controller.h"
#include <QObject>
#include <functional>

/// Platform-independent desktop media state and commands. All playback stays behind IPC.
class SystemMediaBridge final : public QObject {
    Q_OBJECT
  public:
    using Completion = std::function<void(const QString &)>;
    SystemMediaBridge(IpcClient &client, PlaybackController &playback, QObject *parent = nullptr);
    bool ready() const { return m_ready; }
    const PlaybackController &playback() const { return m_playback; }
    QString trackId() const;
    QString playbackStatus() const;
    qint64 positionUs() const;
    qint64 durationUs() const;
    int queueId() const { return m_playback.song().value("queue_id").toInt(); }
    bool capability(const QString &name) const { return m_playback.capabilities().value(name).toBool(); }
    void request(const QString &method, const QJsonObject &params, Completion done);
    void raise() { emit raiseRequested(); }
    void quit() { emit quitRequested(); }
    void shutdown();
  signals:
    void readyChanged();
    void snapshotChanged();
    void seeked(qint64 positionUs);
    void raiseRequested();
    void quitRequested();
  private:
    IpcClient &m_client;
    PlaybackController &m_playback;
    quint64 m_generation = 0;
    bool m_ready = false;
    bool m_stopping = false;
    void setReady(bool ready);
};
