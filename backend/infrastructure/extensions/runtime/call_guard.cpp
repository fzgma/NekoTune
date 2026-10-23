#include "infrastructure/extensions/runtime/call_guard.h"
#include <QTimer>
#include <atomic>
#include <utility>

namespace nekotune {
struct PendingCalls::Call {
    std::atomic<bool> fired{false};
    IExtensionBackend::Completion done;
    QTimer *timer = nullptr;
};
namespace {
template <class Call> void finishCall(const std::shared_ptr<Call> &call, Result<QVariantMap> result) {
    if (call->fired.exchange(true))
        return;
    if (call->timer) {
        call->timer->stop();
        call->timer->deleteLater();
    }
    auto done = std::move(call->done);
    if (done)
        done(std::move(result));
}
} // namespace

PendingCalls::Completion PendingCalls::add(QObject *owner, Completion done, int timeoutMs) {
    m_calls.removeIf([](const auto &call) { return call->fired.load(); });
    auto call = std::make_shared<Call>();
    call->done = std::move(done);
    call->timer = new QTimer(owner);
    call->timer->setSingleShot(true);
    QObject::connect(call->timer, &QTimer::timeout, owner, [call] { finishCall(call, failure("Extension request timed out")); });
    call->timer->start(timeoutMs);
    m_calls.append(call);
    QPointer<QObject> guard(owner);
    return [guard, call](Result<QVariantMap> result) {
        if (!guard || call->fired.load())
            return;
        QMetaObject::invokeMethod(guard, [call, result] { finishCall(call, result); }, Qt::AutoConnection);
    };
}
void PendingCalls::failAll(const QString &message) {
    for (const auto &call : std::exchange(m_calls, {}))
        finishCall(call, failure(message));
}
} // namespace nekotune
