#pragma once
#include "infrastructure/extensions/runtime/call_guard.h"
#include "infrastructure/extensions/runtime/extension_state.h"
#include "infrastructure/extensions/runtime/registration_scope.h"
#include <nekotune/sdk/extension_plugin.h>
#include "infrastructure/extensions/runtime/dev_watcher.h"
#include <QSet>
#include <QTimer>
#include <map>
#include <memory>

namespace nekotune {
class HostContext;
struct ExtensionRuntime {
    ~ExtensionRuntime();
    QString id;
    QString directory;
    quint64 generation = 0;
    ExtensionState state = ExtensionState::Starting;
    sdk::IExtensionPlugin *plugin = nullptr;
    std::unique_ptr<HostContext> context;
    RegistrationScope registrations;
    // Function objects and providers below belong to the plugin; stop() drops them all.
    QHash<QString, sdk::Handler> services, commands;
    std::map<quint64, sdk::RegistrationPtr> pcm;
    std::map<quint64, std::pair<QString, sdk::EventHandler>> subscriptions;
    quint64 nextSubscription = 1;
    QVariantMap storage;
    QSet<QString> secretKeys;
    QList<std::function<void(Result<void>)>> waiters;
    PendingCalls calls;
    std::unique_ptr<QTimer> startTimer;
    std::unique_ptr<DevWatcher> watcher;
};

} // namespace nekotune
