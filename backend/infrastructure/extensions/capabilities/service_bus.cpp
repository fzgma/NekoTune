#include "infrastructure/extensions/capabilities/service_bus.h"
#include "infrastructure/extensions/capabilities/capability_host.h"
#include "infrastructure/extensions/runtime/call_guard.h"

namespace nekotune {
void ServiceBus::addService(const QString &extensionId, const QString &id, sdk::Handler handler) {
    m_extensions[extensionId].services.insert(id, std::move(handler));
}
void ServiceBus::addCommand(const QString &extensionId, const QString &id, sdk::Handler handler) {
    m_extensions[extensionId].commands.insert(id, std::move(handler));
}
void ServiceBus::removeService(const QString &extensionId, const QString &id) {
    const auto found = m_extensions.find(extensionId);
    if (found != m_extensions.end())
        found->services.remove(id);
}
void ServiceBus::removeCommand(const QString &extensionId, const QString &id) {
    const auto found = m_extensions.find(extensionId);
    if (found != m_extensions.end())
        found->commands.remove(id);
}
void ServiceBus::subscribe(const QString &extensionId, quint64 key, QString event,
                           sdk::EventHandler handler) {
    m_extensions[extensionId].subscriptions.emplace(key, std::pair{std::move(event), std::move(handler)});
}
void ServiceBus::unsubscribe(const QString &extensionId, quint64 key) {
    const auto found = m_extensions.find(extensionId);
    if (found != m_extensions.end())
        found->subscriptions.erase(key);
}
void ServiceBus::removeExtension(const QString &extensionId) { m_extensions.remove(extensionId); }
void ServiceBus::invoke(const QString &extensionId, QString service, const QVariantMap &params,
                        Completion done) {
    if (!m_host.running(extensionId)) {
        done(failure("Extension is not running"));
        return;
    }
    if (service.isEmpty()) {
        done(failure("Service name required"));
        return;
    }
    if (service.startsWith(extensionId + "/"))
        service = service.mid(extensionId.size() + 1);
    const auto extension = m_extensions.constFind(extensionId);
    const auto handler = extension == m_extensions.cend()
                             ? sdk::Handler{}
                             : extension->services.value(service, extension->commands.value(service));
    if (!handler) {
        done(failure("Unknown service/command: " + service));
        return;
    }
    const auto reply = m_host.pendingCalls(extensionId)->add(m_host.owner(), std::move(done), timeouts::call);
    m_host.guarded(extensionId, [&] { handler(params, reply); });
}
void ServiceBus::deliver(const QString &extensionId, const QVariantMap &event) {
    const auto name = event.value("event").toString();
    // Copy first: handlers may subscribe or dispose while running.
    const auto subscriptions = m_extensions.value(extensionId).subscriptions;
    for (const auto &[key, subscription] : subscriptions)
        if (subscription.first == "*" || subscription.first == name) {
            const auto live = m_extensions.constFind(extensionId);
            if (live != m_extensions.cend() && live->subscriptions.contains(key))
                m_host.guarded(extensionId, [&] { subscription.second(event); });
        }
}
void ServiceBus::broadcast(const QVariantMap &event) {
    for (const auto &extensionId : m_host.extensionOrder())
        if (m_host.running(extensionId))
            deliver(extensionId, event);
}
} // namespace nekotune
