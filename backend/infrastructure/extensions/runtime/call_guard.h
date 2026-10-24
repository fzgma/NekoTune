#pragma once
#include "domain/extensions/extension_backend.h"
#include "infrastructure/extensions/runtime/crash_guard.h"
#include "infrastructure/extensions/runtime/watchdog.h"
#include <QPointer>
#include <exception>
#include <atomic>
#include <memory>
#include <optional>

class QTimer;

namespace nekotune {
namespace timeouts {
constexpr int activation = 30'000;
constexpr int call = 30'000;
constexpr int download = 610'000;
constexpr int downloaded = 120'000;
} // namespace timeouts

struct AttributedOutcome {
    std::optional<QString> error;
    qint64 elapsedMs = 0;
    bool hung = false;
};
/// Run extension code with crash attribution, exception capture and backend blocking detection.
template <class F> AttributedOutcome runAttributed(const QString &extensionId, F &&call) {
    watchdog::Call monitor(extensionId);
    crashguard::Scope scope(extensionId);
    AttributedOutcome outcome;
    try {
        call();
    } catch (const std::exception &exception) {
        outcome.error = QStringLiteral("Unhandled exception: %1").arg(QString::fromLocal8Bit(exception.what()));
    } catch (...) {
        outcome.error = QStringLiteral("Unhandled exception");
    }
    monitor.finish();
    outcome.elapsedMs = monitor.elapsedMs();
    outcome.hung = monitor.hung();
    return outcome;
}

/// Completions handed to one run's extension code. Each completes at most once, on the owner's
/// thread, fails after its timeout and fails when the run stops first.
class PendingCalls {
  public:
    using Completion = IExtensionBackend::Completion;
    template <class T>
    std::function<void(Result<T>)> add(QObject *owner, std::function<void(Result<T>)> done, int timeoutMs);
    /// Keep map callbacks accepting lambdas without an explicit template argument.
    Completion add(QObject *owner, Completion done, int timeoutMs) {
        return add<QVariantMap>(owner, std::move(done), timeoutMs);
    }
    void failAll(const QString &message);

  private:
    struct Call {
        std::atomic<bool> fired{false};
        QTimer *timer = nullptr;
        /// Fails the typed completion; used by the timeout and failAll().
        std::function<void(const AppError &)> fail;
    };
    std::shared_ptr<Call> track(QObject *owner, int timeoutMs);
    static bool claim(const std::shared_ptr<Call> &call);
    QList<std::shared_ptr<Call>> m_calls;
};
template <class T>
std::function<void(Result<T>)> PendingCalls::add(QObject *owner, std::function<void(Result<T>)> done, int timeoutMs) {
    auto call = track(owner, timeoutMs);
    // The call owns its failure callback, so retain only a weak reference back to the call.
    call->fail = [weak = std::weak_ptr<Call>(call), done](const AppError &error) {
        if (const auto alive = weak.lock(); alive && claim(alive) && done)
            done(error);
    };
    return [guard = QPointer<QObject>(owner), call, done](Result<T> result) {
        if (!guard || call->fired.load())
            return;
        QMetaObject::invokeMethod(guard, [call, done, result] {
            if (claim(call) && done)
                done(result);
        }, Qt::AutoConnection);
    };
}
} // namespace nekotune
