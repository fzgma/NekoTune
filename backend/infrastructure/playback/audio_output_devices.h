#pragma once
#include "domain/playback/playback_backend.h"
#include <QAudioDevice>
#include <QMediaDevices>
#ifdef NEKOTUNE_PULSE_AUDIO_PORTS
#include "infrastructure/playback/pulse_audio_ports.h"
#endif
namespace nekotune {
// Device/port ownership is shared by both playback adapters. Empty means detached.
class AudioOutputDevices final : public QObject {
    Q_OBJECT
  public:
    AudioOutputDevices();
    QList<AudioOutputDevice> outputs() const;
    QString activeId() const { return m_id; }
    bool available() const;
    QAudioDevice device() const;
    void select(const QString &id, const QString &port = {});
    void selectPort(const QString &id, const QString &port, IPlaybackBackend::OutputCompletion done);
    void suspend();
  signals:
    void changed();
    void availabilityChanged(bool available);
    void deviceChanged();

  private:
    QMediaDevices m_devices;
    QString m_id, m_port, m_lastPort;
    bool m_switching = false;
#ifdef NEKOTUNE_PULSE_AUDIO_PORTS
    PulseAudioPorts m_ports;
#endif
};
} // namespace nekotune
