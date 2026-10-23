#include "infrastructure/extensions/runtime/watchdog.h"
#include <QDebug>
#include <QThread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace nekotune::watchdog {
namespace {
std::mutex mutex;
std::condition_variable wake;
std::thread worker;
QThread *monitoredThread = nullptr;
Limits limits;
bool stopping = false;
std::atomic<qint64> started{0};
QString currentId;
std::atomic<bool> warned{false}, markedHung{false};
thread_local int depth = 0;

qint64 now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}
} // namespace

void start(QThread *thread, Limits newLimits, std::function<void(const QString &)> onHang) {
    stop();
    {
        std::lock_guard lock(mutex);
        monitoredThread = thread;
        limits = newLimits;
        stopping = false;
    }
    worker = std::thread([onHang = std::move(onHang)] {
        std::unique_lock lock(mutex);
        while (!wake.wait_for(lock, std::chrono::milliseconds(1000), [] { return stopping; })) {
            const auto begin = started.load();
            if (!begin)
                continue;
            const auto elapsed = now() - begin;
            if (elapsed > limits.warnMs && !warned.exchange(true))
                qWarning("Extension %s has blocked the backend for %lld ms", qPrintable(currentId),
                         static_cast<long long>(elapsed));
            if (elapsed > limits.hangMs && !markedHung.exchange(true) && onHang)
                // Serialize the write with finish(): a resolved call must not leave a late sentinel.
                onHang(currentId);
        }
    });
}
void stop() {
    {
        std::lock_guard lock(mutex);
        stopping = true;
    }
    wake.notify_all();
    if (worker.joinable())
        worker.join();
    std::lock_guard lock(mutex);
    monitoredThread = nullptr;
    started.store(0);
    currentId.clear();
}
Call::Call(const QString &extensionId) : m_started(now()) {
    std::lock_guard lock(mutex);
    if (QThread::currentThread() != monitoredThread)
        return;
    m_counted = true;
    if (++depth != 1)
        return;
    m_monitored = true;
    currentId = extensionId;
    warned.store(false);
    markedHung.store(false);
    started.store(m_started);
}
Call::~Call() { finish(); }
void Call::finish() {
    if (m_finished)
        return;
    m_finished = true;
    std::lock_guard lock(mutex);
    m_elapsed = now() - m_started;
    if (m_counted)
        --depth;
    if (!m_monitored)
        return;
    // A call can return between polling ticks, but must still be disabled if it exceeded the limit.
    m_hung = markedHung.load() || m_elapsed > limits.hangMs;
    started.store(0);
    currentId.clear();
}
} // namespace nekotune::watchdog
