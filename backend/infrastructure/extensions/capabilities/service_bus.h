#pragma once
#include <nekotune/sdk/extension_context.h>
#include <QHash>
#include <map>

namespace nekotune {
class CapabilityHost;
/// Services, commands and event subscriptions of running extensions.
class ServiceBus {
  public:
    using Completion = std::function<void(Result<QVariantMap>)>;
    explicit ServiceBus(CapabilityHost &host) : m_host(host) {}
    void addService(const QString &extensionId, const QString &id, sdk::Handler handler);
    void addCommand(const QString &extensionId, const QString &id, sdk::Handler handler);
    void removeService(const QString &extensionId, const QString &id);
    void removeCommand(const QString &extensionId, const QString &id);
    /// key is reserved by the caller so a handle can remove a subscription installed later.
    void subscribe(const QString &extensionId, quint64 key, QString event, sdk::EventHandler handler);
    void unsubscribe(const QString &extensionId, quint64 key);
    void removeExtension(const QString &extensionId);
    /// Service or command of a running extension; "<id>/" prefixes are accepted.
    void invoke(const QString &extensionId, QString service, const QVariantMap &params, Completion done);
    void deliver(const QString &extensionId, const QVariantMap &event);
    void broadcast(const QVariantMap &event);

  private:
    struct Extension {
        QHash<QString, sdk::Handler> services, commands;
        std::map<quint64, std::pair<QString, sdk::EventHandler>> subscriptions;
    };
    CapabilityHost &m_host;
    QHash<QString, Extension> m_extensions;
};
} // namespace nekotune
