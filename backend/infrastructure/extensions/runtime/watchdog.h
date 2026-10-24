#pragma once
#include <QString>
#include <functional>

class QThread;

namespace nekotune::watchdog {
struct Limits {
    int warnMs = 5'000;
    int hangMs = 30'000;
};
/// Start monitoring extension calls made on thread. onHang runs on the watchdog thread.
void start(QThread *thread, Limits limits, std::function<void(const QString &extensionId)> onHang);
void stop();

/// Times calls; only the outermost call on the monitored thread registers with the watchdog.
class Call {
  public:
    explicit Call(const QString &extensionId);
    ~Call();
    Call(const Call &) = delete;
    Call &operator=(const Call &) = delete;
    /// Stop monitoring before inspecting the completed call; also performed by the destructor.
    void finish();
    qint64 elapsedMs() const { return m_elapsed; }
    bool hung() const { return m_hung; }

  private:
    qint64 m_started;
    qint64 m_elapsed = 0;
    bool m_counted = false;
    bool m_monitored = false;
    bool m_finished = false;
    bool m_hung = false;
};
} // namespace nekotune::watchdog
