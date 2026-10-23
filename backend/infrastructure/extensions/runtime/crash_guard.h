#pragma once
#include <QString>

/// Records which extension was executing when the backend crashed. Recovering inside a crash
/// handler is unsafe, so the handler (POSIX signals, or the unhandled exception filter on Windows)
/// only writes the active extension ID to a sentinel file and lets the crash continue; the next
/// start disables that extension.
namespace nekotune::crashguard {
/// Install handlers for fatal signals once; later calls only change the sentinel path.
void install(const QString &sentinelPath);
/// Read and delete the sentinel left by a previous crash; empty when the last run did not crash.
QString takeSentinel(const QString &sentinelPath);

/// Marks extension code running on the current thread. Scopes nest.
class Scope {
  public:
    explicit Scope(const QString &extensionId);
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

  private:
    QByteArray m_previous;
};
} // namespace nekotune::crashguard
