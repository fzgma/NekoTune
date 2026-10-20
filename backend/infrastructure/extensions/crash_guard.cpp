#include "infrastructure/extensions/crash_guard.h"
#include <QDir>
#include <QFile>
#include <atomic>
#include <cstring>
#ifdef Q_OS_UNIX
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace nekotune::crashguard {
namespace {
// Fixed buffers: the signal handler may not allocate. Only the backend thread writes them.
#ifdef Q_OS_WIN
wchar_t sentinel[4096];
#else
char sentinel[4096];
#endif
char active[128];
std::atomic<int> activeLength{0};
#ifdef Q_OS_UNIX
constexpr int fatalSignals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
struct sigaction previous[std::size(fatalSignals)];
std::atomic<bool> installed{false};

void handler(int signal) {
    const int length = activeLength.load();
    if (length > 0 && sentinel[0]) {
        const int fd = ::open(sentinel, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
        if (fd >= 0) {
            [[maybe_unused]] const auto written = ::write(fd, active, length);
            ::close(fd);
        }
    }
    for (std::size_t i = 0; i < std::size(fatalSignals); ++i)
        if (fatalSignals[i] == signal)
            ::sigaction(signal, &previous[i], nullptr);
    ::raise(signal);
}
#endif
#ifdef Q_OS_WIN
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
std::atomic<bool> installed{false};

LONG WINAPI filter(EXCEPTION_POINTERS *exception) {
    const int length = activeLength.load();
    if (length > 0 && sentinel[0]) {
        const HANDLE file = CreateFileW(sentinel, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(file, active, DWORD(length), &written, nullptr);
            CloseHandle(file);
        }
    }
    return previousFilter ? previousFilter(exception) : EXCEPTION_CONTINUE_SEARCH;
}
#endif
void setActive(const QByteArray &id) {
    activeLength.store(0);
    const auto length = qMin<qsizetype>(id.size(), sizeof(active));
    std::memcpy(active, id.constData(), length);
    activeLength.store(int(length));
}
} // namespace

void install(const QString &sentinelPath) {
#ifdef Q_OS_WIN
    const auto path = QDir::toNativeSeparators(sentinelPath);
    if (path.size() >= qsizetype(std::size(sentinel)))
        return;
    std::memcpy(sentinel, path.utf16(), (path.size() + 1) * sizeof(wchar_t));
    if (!installed.exchange(true))
        previousFilter = SetUnhandledExceptionFilter(filter);
#else
    const auto path = QFile::encodeName(sentinelPath);
    if (path.size() >= qsizetype(sizeof(sentinel)))
        return;
    std::memcpy(sentinel, path.constData(), path.size() + 1);
#endif
#ifdef Q_OS_UNIX
    if (installed.exchange(true))
        return;
    struct sigaction action {};
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_ONSTACK;
    for (std::size_t i = 0; i < std::size(fatalSignals); ++i)
        ::sigaction(fatalSignals[i], &action, &previous[i]);
#endif
}
QString takeSentinel(const QString &sentinelPath) {
    QFile file(sentinelPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto id = QString::fromUtf8(file.read(sizeof(active))).trimmed();
    file.close();
    file.remove();
    return id;
}
Scope::Scope(const QString &extensionId) : m_previous(active, activeLength.load()) {
    setActive(extensionId.toUtf8());
}
Scope::~Scope() { setActive(m_previous); }
} // namespace nekotune::crashguard
