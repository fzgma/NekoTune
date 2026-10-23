#pragma once
#include "domain/extensions/extension_backend.h"
#include "infrastructure/extensions/runtime/crash_guard.h"
#include <QPointer>
#include <exception>
#include <memory>
#include <optional>

namespace nekotune {
namespace timeouts {
constexpr int activation = 30'000;
constexpr int call = 30'000;
constexpr int download = 610'000;
constexpr int downloaded = 120'000;
} // namespace timeouts

/// Run extension code with crash attribution. Returns the error message when it threw.
template <class F> std::optional<QString> runAttributed(const QString &extensionId, F &&call) {
    crashguard::Scope scope(extensionId);
    try {
        call();
        return std::nullopt;
    } catch (const std::exception &exception) {
        return QStringLiteral("Unhandled exception: %1").arg(QString::fromLocal8Bit(exception.what()));
    } catch (...) {
        return QStringLiteral("Unhandled exception");
    }
}

/// Completions handed to one run's extension code. Each completes at most once, on the owner's
/// thread, fails after its timeout and fails when the run stops first.
class PendingCalls {
  public:
    using Completion = IExtensionBackend::Completion;
    Completion add(QObject *owner, Completion done, int timeoutMs);
    void failAll(const QString &message);

  private:
    struct Call;
    QList<std::shared_ptr<Call>> m_calls;
};
} // namespace nekotune
