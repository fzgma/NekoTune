#pragma once
#include <QStringList>
#include <functional>

class QObject;

namespace nekotune {
class PendingCalls;
/// The runtime services a capability registry needs; implemented by PluginManager.
class CapabilityHost {
  public:
    virtual ~CapabilityHost() = default;
    /// Backend-thread object owning timers and receiving posted work.
    virtual QObject *owner() = 0;
    /// Installed extensions in installation order.
    virtual QStringList extensionOrder() const = 0;
    virtual bool running(const QString &extensionId) const = 0;
    /// Calls of a running extension; null when it is not running.
    virtual PendingCalls *pendingCalls(const QString &extensionId) = 0;
    /// Run extension code with crash attribution; an exception faults the extension.
    virtual bool guarded(const QString &extensionId, const std::function<void()> &call) = 0;
    virtual QString dataDirectory(const QString &extensionId) const = 0;
    virtual void recordLog(const QString &extensionId, const QString &level, const QString &message) = 0;
};
} // namespace nekotune
