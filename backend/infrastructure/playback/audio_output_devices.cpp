#include "infrastructure/playback/audio_output_devices.h"
#include <QPointer>
namespace nekotune {
AudioOutputDevices::AudioOutputDevices() {
    auto reconcile = [this] {
        bool present = m_id.isEmpty();
        for (const auto &output : outputs()) {
            if (output.id != m_id)
                continue;
            present = true;
            bool oldPortPresent = m_lastPort.isEmpty();
            for (const auto &port : output.ports)
                if (port.id == m_lastPort)
                    oldPortPresent = port.available;
            if (!oldPortPresent || (!m_switching && !available()))
                suspend();
            else if (!m_switching)
                m_lastPort = output.activePortId;
            break;
        }
        if (!present)
            suspend();
        emit changed();
    };
    connect(&m_devices, &QMediaDevices::audioOutputsChanged, this, reconcile);
#ifdef NEKOTUNE_PULSE_AUDIO_PORTS
    connect(&m_ports, &PulseAudioPorts::changed, this, reconcile);
#endif
}
QList<AudioOutputDevice> AudioOutputDevices::outputs() const {
    QList<AudioOutputDevice> result;
    for (const auto &device : QMediaDevices::audioOutputs()) {
        AudioOutputDevice output{QString::fromLatin1(device.id().toBase64()), device.description(), device.isDefault()};
#ifdef NEKOTUNE_PULSE_AUDIO_PORTS
        for (const auto &sink : m_ports.outputs())
            if (sink.name.toUtf8() == device.id()) {
                output.ports = sink.ports;
                output.activePortId = sink.activePortId;
                break;
            }
#endif
        result.append(output);
    }
    return result;
}
QAudioDevice AudioOutputDevices::device() const {
    for (const auto &device : QMediaDevices::audioOutputs())
        if (QString::fromLatin1(device.id().toBase64()) == m_id)
            return device;
    return {};
}
bool AudioOutputDevices::available() const {
    if (m_id.isEmpty())
        return false;
    for (const auto &output : outputs()) {
        if (output.id != m_id)
            continue;
        if (m_port.isEmpty())
            return true;
        for (const auto &port : output.ports)
            if (port.id == m_port)
                return port.available && output.activePortId == m_port;
    }
    return false;
}
void AudioOutputDevices::suspend() {
    const bool attached = !m_id.isEmpty();
    m_id.clear();
    m_port.clear();
    m_lastPort.clear();
    if (attached) {
        emit availabilityChanged(false);
        emit deviceChanged();
    }
}
void AudioOutputDevices::select(const QString &id, const QString &port) {
    for (const auto &output : outputs()) {
        if (id.isEmpty() || output.id != id)
            continue;
        bool usable = port.isEmpty();
        for (const auto &entry : output.ports)
            if (entry.id == port && entry.available && output.activePortId == port)
                usable = true;
        if (!usable)
            break;
        const bool attached = !m_id.isEmpty();
        const bool different = m_id != id || m_port != port;
        m_id = id;
        m_port = port;
        m_lastPort = output.activePortId;
        if (different)
            emit deviceChanged();
        if (!attached)
            emit availabilityChanged(true);
        return;
    }
    suspend();
}
void AudioOutputDevices::selectPort(const QString &id, const QString &port, IPlaybackBackend::OutputCompletion done) {
#ifdef NEKOTUNE_PULSE_AUDIO_PORTS
    for (const auto &output : outputs()) {
        if (output.id != id)
            continue;
        for (const auto &entry : output.ports) {
            if (entry.id != port || !entry.available)
                continue;
            if (output.activePortId == port) {
                done({});
                return;
            }
            if (m_switching) {
                done(failure("audio_output_port_switch_failed", ErrorCode::Unavailable));
                return;
            }
            m_switching = true;
            QPointer<AudioOutputDevices> guard(this);
            m_ports.select(QString::fromUtf8(QByteArray::fromBase64(id.toLatin1())), port,
                           [guard, done = std::move(done)](Result<void> result) {
                               if (!guard)
                                   return;
                               guard->m_switching = false;
                               done(std::move(result));
                           });
            return;
        }
    }
#else
    Q_UNUSED(id)
    Q_UNUSED(port)
#endif
    done(failure("audio_output_port_unavailable", ErrorCode::Unavailable));
}
} // namespace nekotune
