#include "platform/linux/mpris_service.h"
#include <QCoreApplication>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDebug>
#include <QPointer>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {
const QString path = QStringLiteral("/org/mpris/MediaPlayer2");
const QString rootInterface = QStringLiteral("org.mpris.MediaPlayer2");
const QString playerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
void fail(const QDBusMessage &message, const QDBusConnection &connection,
          QDBusError::ErrorType type, const QString &description) {
    connection.send(message.createErrorReply(type, description));
}
}
MprisService::MprisService(SystemMediaBridge &bridge, QObject *parent)
    : QDBusVirtualObject(parent), m_bridge(bridge),
      m_connectionName(QStringLiteral("nekotune-mpris-%1").arg(QUuid::createUuid().toString(QUuid::Id128))),
      m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName)) {
    m_publishTimer.setSingleShot(true);
    connect(&m_publishTimer, &QTimer::timeout, this, &MprisService::publishChanges);
    connect(&bridge, &SystemMediaBridge::readyChanged, this, &MprisService::updateRegistration);
    connect(&bridge, &SystemMediaBridge::snapshotChanged, this, [this] { m_publishTimer.start(0); });
    connect(&bridge, &SystemMediaBridge::seeked, this, [this](qint64 position) {
        if (m_serviceName.isEmpty()) return;
        auto signal = QDBusMessage::createSignal(path, playerInterface, QStringLiteral("Seeked"));
        signal.setArguments({QVariant::fromValue(position)});
        m_bus.send(signal);
    });
    updateRegistration();
}
MprisService::~MprisService() {
    shutdown();
    QDBusConnection::disconnectFromBus(m_connectionName);
}
void MprisService::shutdown() {
    m_publishTimer.stop();
    if (m_serviceName.isEmpty()) return;
    m_bus.unregisterService(m_serviceName);
    m_bus.unregisterObject(path);
    m_serviceName.clear();
    m_published.clear();
}
void MprisService::updateRegistration() {
    if (!m_bridge.ready()) { shutdown(); return; }
    if (!m_serviceName.isEmpty()) return;
    if (!m_bus.isConnected() || !m_bus.registerVirtualObject(path, this, QDBusConnection::SingleNode)) {
        qWarning() << "MPRIS unavailable:" << m_bus.lastError().message();
        return;
    }
    QString name = QStringLiteral("org.mpris.MediaPlayer2.NekoTune");
    if (!m_bus.registerService(name)) {
        name += QStringLiteral(".instance%1").arg(QCoreApplication::applicationPid());
        if (!m_bus.registerService(name)) {
            qWarning() << "MPRIS registration failed:" << m_bus.lastError().message();
            m_bus.unregisterObject(path);
            return;
        }
    }
    m_serviceName = name;
    m_published = properties(playerInterface);
    m_published.remove(QStringLiteral("Position"));
}
QVariantMap MprisService::metadata() const {
    if (m_bridge.queueId() <= 0) return {};
    const auto song = m_bridge.playback().song();
    QVariantMap result{{QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(m_bridge.trackId()))},
                       {QStringLiteral("xesam:title"), song.value("title").toString()}};
    if (m_bridge.durationUs() > 0) result.insert(QStringLiteral("mpris:length"), m_bridge.durationUs());
    if (!song.value("artist").toString().isEmpty())
        result.insert(QStringLiteral("xesam:artist"), QStringList{song.value("artist").toString()});
    if (!song.value("album").toString().isEmpty()) result.insert(QStringLiteral("xesam:album"), song.value("album"));
    if (!song.value("cover_url").toString().isEmpty()) result.insert(QStringLiteral("mpris:artUrl"), song.value("cover_url"));
    return result;
}
QVariantMap MprisService::properties(const QString &interface) const {
    if (interface == rootInterface)
        return {{"CanQuit", true}, {"CanRaise", true}, {"HasTrackList", false}, {"Identity", QStringLiteral("NekoTune")},
                {"SupportedUriSchemes", QStringList{}}, {"SupportedMimeTypes", QStringList{}}};
    if (interface != playerInterface) return {};
    return {{"PlaybackStatus", m_bridge.playbackStatus()}, {"Metadata", metadata()},
            {"Volume", m_bridge.playback().volume()}, {"Position", m_bridge.positionUs()},
            {"Rate", m_bridge.playback().playbackRate()},
            {"MinimumRate", m_bridge.playback().playbackRateSupported() ? .5 : 1.0},
            {"MaximumRate", m_bridge.playback().playbackRateSupported() ? 2.0 : 1.0}, {"CanControl", true},
            {"CanPlay", m_bridge.capability("can_play")}, {"CanPause", m_bridge.capability("can_pause")},
            {"CanSeek", m_bridge.capability("can_seek")}, {"CanGoNext", m_bridge.capability("can_go_next")},
            {"CanGoPrevious", m_bridge.capability("can_go_previous")}};
}
void MprisService::publishChanges() {
    if (m_serviceName.isEmpty()) return;
    auto current = properties(playerInterface);
    current.remove(QStringLiteral("Position"));
    QVariantMap changed;
    for (auto it = current.cbegin(); it != current.cend(); ++it)
        if (m_published.value(it.key()) != it.value()) changed.insert(it.key(), it.value());
    m_published = current;
    if (changed.isEmpty()) return;
    auto signal = QDBusMessage::createSignal(path, propertiesInterface, QStringLiteral("PropertiesChanged"));
    signal.setArguments({playerInterface, changed, QStringList{}});
    m_bus.send(signal);
}
void MprisService::dispatch(const QDBusMessage &message, const QDBusConnection &connection,
                            const QString &method, const QJsonObject &params) {
    message.setDelayedReply(true);
    m_bridge.request(method, params, [guard = QPointer<MprisService>(this), message, connection](const QString &error) {
        if (!guard) return;
        if (error.isEmpty()) connection.send(message.createReply());
        else fail(message, connection, error.contains("timed out") ? QDBusError::Timeout : QDBusError::Failed, error);
    });
}
bool MprisService::handleMessage(const QDBusMessage &message, const QDBusConnection &connection) {
    if (message.path() != path || message.type() != QDBusMessage::MethodCallMessage) return false;
    const auto interface = message.interface();
    const auto member = message.member();
    const auto args = message.arguments();
    auto ok = [&] { connection.send(message.createReply()); };
    auto invalid = [&] { fail(message, connection, QDBusError::InvalidArgs, QStringLiteral("Invalid arguments")); };
    if (interface == propertiesInterface) {
        const auto signature = message.signature();
        if ((member == "Get" && signature != "ss") || (member == "GetAll" && signature != "s") ||
            (member == "Set" && signature != "ssv")) { invalid(); return true; }
        if (member != "Get" && member != "GetAll" && member != "Set") return false;
        const auto target = args.at(0).toString();
        if (target != rootInterface && target != playerInterface) {
            fail(message, connection, QDBusError::UnknownInterface, QStringLiteral("Unknown interface")); return true;
        }
        const auto values = properties(target);
        if (member == "GetAll") { connection.send(message.createReply(QVariantList{values})); return true; }
        const auto property = args.at(1).toString();
        if (!values.contains(property)) {
            fail(message, connection, QDBusError::UnknownProperty, QStringLiteral("Unknown property")); return true;
        }
        if (member == "Get") {
            connection.send(message.createReply(QVariantList{QVariant::fromValue(QDBusVariant(values.value(property)))})); return true;
        }
        if (target != playerInterface || property != "Volume") {
            fail(message, connection, QDBusError::PropertyReadOnly, QStringLiteral("Property is read-only")); return true;
        }
        const auto value = qvariant_cast<QDBusVariant>(args.at(2)).variant();
        if (value.metaType() != QMetaType::fromType<double>() || !std::isfinite(value.toDouble())) {
            invalid(); return true;
        }
        dispatch(message, connection, "player.set_volume", {{"volume", std::clamp(value.toDouble(), 0.0, 1.0)}});
        return true;
    }
    if (interface == rootInterface) {
        if (!message.signature().isEmpty()) { invalid(); return true; }
        if (member == "Raise") { m_bridge.raise(); ok(); return true; }
        if (member == "Quit") {
            ok();
            QTimer::singleShot(0, &m_bridge, &SystemMediaBridge::quit);
            return true;
        }
        return false;
    }
    if (interface != playerInterface) return false;
    if (member == "OpenUri") {
        if (message.signature() != "s") invalid();
        else fail(message, connection, QDBusError::NotSupported, QStringLiteral("OpenUri is not supported"));
        return true;
    }
    if (member == "Seek" || member == "SetPosition") {
        if (message.signature() != (member == "Seek" ? "x" : "ox")) { invalid(); return true; }
        if (!m_bridge.capability("can_seek")) { ok(); return true; }
        const auto value = args.last().toLongLong();
        if (member == "SetPosition") {
            if (qvariant_cast<QDBusObjectPath>(args.first()).path() != m_bridge.trackId() ||
                value < 0 || value > m_bridge.durationUs()) { ok(); return true; }
            dispatch(message, connection, "player.seek", {{"position", value / 1000}, {"queue_id", m_bridge.queueId()}});
        } else {
            dispatch(message, connection, "player.seek", {{"offset", value / 1000}, {"queue_id", m_bridge.queueId()}});
        }
        return true;
    }
    const QHash<QString, QString> controls{{"Play", "player.play"}, {"Pause", "player.pause"},
        {"PlayPause", "player.toggle_play_pause"}, {"Stop", "player.stop"},
        {"Next", "player.next"}, {"Previous", "player.previous"}};
    if (!controls.contains(member)) return false;
    if (!message.signature().isEmpty()) { invalid(); return true; }
    if ((member == "Next" && !m_bridge.capability("can_go_next")) ||
        (member == "Previous" && !m_bridge.capability("can_go_previous")) ||
        (member == "Play" && (!m_bridge.capability("can_play") || m_bridge.playback().state() == "playing")) ||
        (member == "Pause" && (!m_bridge.capability("can_pause") || m_bridge.playback().state() == "paused")) ||
        (member == "Stop" && m_bridge.playback().state() == "stopped")) { ok(); return true; }
    if (member == "PlayPause" && !m_bridge.capability("can_pause")) {
        fail(message, connection, QDBusError::NotSupported, QStringLiteral("Playback cannot be toggled")); return true;
    }
    dispatch(message, connection, controls.value(member), member == "Next" || member == "Previous"
             ? QJsonObject{{"preserve_state", true}} : QJsonObject{});
    return true;
}
QString MprisService::introspect(const QString &objectPath) const {
    if (objectPath != path) return {};
    return QStringLiteral(R"XML(
<interface name="org.mpris.MediaPlayer2">
 <method name="Raise"/><method name="Quit"/>
 <property name="CanQuit" type="b" access="read"/><property name="CanRaise" type="b" access="read"/>
 <property name="HasTrackList" type="b" access="read"/><property name="Identity" type="s" access="read"/>
 <property name="SupportedUriSchemes" type="as" access="read"/><property name="SupportedMimeTypes" type="as" access="read"/>
</interface>
<interface name="org.mpris.MediaPlayer2.Player">
 <method name="Play"/><method name="Pause"/><method name="PlayPause"/><method name="Stop"/>
 <method name="Next"/><method name="Previous"/>
 <method name="Seek"><arg name="Offset" type="x" direction="in"/></method>
 <method name="SetPosition"><arg name="TrackId" type="o" direction="in"/><arg name="Position" type="x" direction="in"/></method>
 <method name="OpenUri"><arg name="Uri" type="s" direction="in"/></method>
 <signal name="Seeked"><arg name="Position" type="x"/></signal>
 <property name="PlaybackStatus" type="s" access="read"/><property name="Metadata" type="a{sv}" access="read"/>
 <property name="Volume" type="d" access="readwrite"/><property name="Position" type="x" access="read"/>
 <property name="Rate" type="d" access="read"/><property name="MinimumRate" type="d" access="read"/>
 <property name="MaximumRate" type="d" access="read"/><property name="CanControl" type="b" access="read"/>
 <property name="CanPlay" type="b" access="read"/><property name="CanPause" type="b" access="read"/>
 <property name="CanSeek" type="b" access="read"/><property name="CanGoNext" type="b" access="read"/>
 <property name="CanGoPrevious" type="b" access="read"/>
</interface>
<interface name="org.freedesktop.DBus.Properties">
 <method name="Get"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/></method>
 <method name="GetAll"><arg type="s" direction="in"/><arg type="a{sv}" direction="out"/></method>
 <method name="Set"><arg type="s" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/></method>
 <signal name="PropertiesChanged"><arg type="s"/><arg type="a{sv}"/><arg type="as"/></signal>
</interface>
)XML");
}
