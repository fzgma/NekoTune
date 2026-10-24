#include "infrastructure/extensions/runtime/call_guard.h"
#include <QTimer>
#include <utility>

namespace nekotune {
std::shared_ptr<PendingCalls::Call> PendingCalls::track(QObject *owner, int timeoutMs) {
    m_calls.removeIf([](const auto &call) { return call->fired.load(); });
    auto call = std::make_shared<Call>();
    call->timer = new QTimer(owner);
    call->timer->setSingleShot(true);
    QObject::connect(call->timer, &QTimer::timeout, owner, [call] {
        call->fail(failure("Extension request timed out"));
    });
    call->timer->start(timeoutMs);
    m_calls.append(call);
    return call;
}
bool PendingCalls::claim(const std::shared_ptr<Call> &call) {
    if (call->fired.exchange(true))
        return false;
    if (call->timer) {
        call->timer->stop();
        call->timer->deleteLater();
    }
    return true;
}
void PendingCalls::failAll(const QString &message) {
    for (const auto &call : std::exchange(m_calls, {}))
        call->fail(failure(message));
}
} // namespace nekotune
