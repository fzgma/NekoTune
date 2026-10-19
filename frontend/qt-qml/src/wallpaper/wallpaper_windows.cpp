#include "wallpaper/wallpaper_platform.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QScreen>
#include <QVariant>
#include <QWindow>
#include <vector>

namespace {
QString windowClass(HWND window) {
    wchar_t name[256]{};
    return GetClassNameW(window, name, 256) ? QString::fromWCharArray(name) : QString();
}
DWORD processId(HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    return pid;
}
bool explorerProcess(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    wchar_t path[32768]{}, directory[MAX_PATH]{};
    DWORD size = 32768;
    const bool queried = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    const UINT directoryLength = GetWindowsDirectoryW(directory, MAX_PATH);
    if (!queried || !directoryLength || directoryLength >= MAX_PATH)
        return false;
    return QString::fromWCharArray(path).compare(QString::fromWCharArray(directory) + "\\explorer.exe",
                                                 Qt::CaseInsensitive) == 0;
}
HWND childClass(HWND parent, const wchar_t *name) { return FindWindowExW(parent, nullptr, name, nullptr); }
bool hasDesktopIcons(HWND host, DWORD pid) {
    HWND view = childClass(host, L"SHELLDLL_DefView");
    HWND icons = view ? childClass(view, L"SysListView32") : nullptr;
    return IsWindowVisible(host) && view && icons && processId(view) == pid && processId(icons) == pid;
}
BOOL CALLBACK enumerate(HWND window, LPARAM data) {
    reinterpret_cast<std::vector<HWND> *>(data)->push_back(window);
    return TRUE;
}
struct MonitorSearch {
    QString name;
    RECT rect{};
    bool found = false;
};
BOOL CALLBACK findMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto &search = *reinterpret_cast<MonitorSearch *>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO *>(&info)) &&
        search.name.compare(QString::fromWCharArray(info.szDevice), Qt::CaseInsensitive) == 0) {
        search.rect = info.rcMonitor;
        search.found = true;
        return FALSE;
    }
    return TRUE;
}
bool contains(const RECT &outer, const RECT &inner) {
    return outer.left <= inner.left && outer.top <= inner.top && outer.right >= inner.right &&
           outer.bottom >= inner.bottom;
}
BOOL CALLBACK checkWorkerChild(HWND child, LPARAM data) {
    auto &safe = *reinterpret_cast<bool *>(data);
    safe = processId(child) == GetCurrentProcessId() &&
           GetPropW(child, L"NekoTuneMusicWallpaper") == reinterpret_cast<HANDLE>(1);
    return safe;
}
bool validWorker(HWND worker, DWORD pid, const RECT &monitor) {
    RECT rect{};
    bool safeChildren = true;
    EnumChildWindows(worker, checkWorkerChild, reinterpret_cast<LPARAM>(&safeChildren));
    return IsWindow(worker) && IsWindowVisible(worker) && processId(worker) == pid &&
           windowClass(worker) == "WorkerW" && !GetParent(worker) && !GetWindow(worker, GW_OWNER) &&
           safeChildren && !childClass(worker, L"SHELLDLL_DefView") && GetWindowRect(worker, &rect) &&
           contains(rect, monitor);
}
HWND findWorker(DWORD pid, const RECT &monitor) {
    std::vector<HWND> windows;
    if (!EnumWindows(enumerate, reinterpret_cast<LPARAM>(&windows)))
        return nullptr;
    // EnumWindows returns top-level Z order. Accept only a separate WorkerW
    // below an Explorer desktop icon host, never the icon host itself.
    bool belowIcons = false;
    for (HWND window : windows) {
        if (processId(window) != pid)
            continue;
        if (hasDesktopIcons(window, pid)) {
            belowIcons = true;
            continue;
        }
        if (belowIcons && validWorker(window, pid, monitor))
            return window;
    }
    return nullptr;
}
bool setLong(HWND window, int index, LONG_PTR value) {
    SetLastError(ERROR_SUCCESS);
    const auto previous = SetWindowLongPtrW(window, index, value);
    return previous != 0 || GetLastError() == ERROR_SUCCESS;
}
bool sameDpiContext(HWND window, HWND parent) {
    // Do not let cross-process SetParent reset the Qt process's DPI awareness.
    using GetContext = HANDLE(WINAPI *)(HWND);
    using Equal = BOOL(WINAPI *)(HANDLE, HANDLE);
    const auto user32 = GetModuleHandleW(L"user32.dll");
    const auto context = reinterpret_cast<GetContext>(GetProcAddress(user32, "GetWindowDpiAwarenessContext"));
    const auto equal = reinterpret_cast<Equal>(GetProcAddress(user32, "AreDpiAwarenessContextsEqual"));
    return context && equal && equal(context(window), context(parent));
}

// Suppress native showing while Qt replaces a handle or Explorer loses its
// parent. A queued manager recovery must never expose a top-level fullscreen
// window between SurfaceCreated and the next health check.
class NativeGuard final : public QObject {
    HWND m_handle = nullptr, m_parent = nullptr;
    static LRESULT CALLBACK procedure(HWND handle, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id,
                                      DWORD_PTR data) {
        auto *guard = reinterpret_cast<NativeGuard *>(data);
        if (message == WM_WINDOWPOSCHANGING) {
            auto *position = reinterpret_cast<WINDOWPOS *>(lparam);
            if (!guard->m_parent || !IsWindow(guard->m_parent) || GetParent(handle) != guard->m_parent) {
                position->flags &= ~SWP_SHOWWINDOW;
                position->flags |= SWP_HIDEWINDOW;
            }
        } else if (message == WM_MOUSEACTIVATE) {
            return MA_NOACTIVATE;
        } else if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(handle, procedure, id);
            guard->m_handle = nullptr;
            guard->m_parent = nullptr;
        }
        return DefSubclassProc(handle, message, wparam, lparam);
    }

  public:
    explicit NativeGuard(QWindow *window) : QObject(window) {
        setObjectName("wallpaperNativeGuard");
        window->installEventFilter(this);
    }
    ~NativeGuard() override {
        if (m_handle && IsWindow(m_handle))
            RemoveWindowSubclass(m_handle, procedure, reinterpret_cast<UINT_PTR>(this));
    }
    bool install(HWND handle) {
        m_handle = handle;
        m_parent = nullptr;
        return SetWindowSubclass(handle, procedure, reinterpret_cast<UINT_PTR>(this),
                                 reinterpret_cast<DWORD_PTR>(this));
    }
    void allow(HWND parent) { m_parent = parent; }
    bool ownsHandle(HWND handle) const { return m_handle && m_handle == handle; }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::PlatformSurface &&
            static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() ==
                QPlatformSurfaceEvent::SurfaceCreated) {
            auto *window = static_cast<QWindow *>(object);
            // The new HWND remains blocked. The manager destroys and re-probes
            // this instance after the old platform surface is lost.
            const HWND handle = reinterpret_cast<HWND>(window->winId());
            if (!install(handle)) {
                ShowWindow(handle, SW_HIDE);
                window->setVisible(false);
            }
        }
        return false;
    }
};
QString place(HWND window, HWND worker, const RECT &monitor) {
    RECT current{};
    if (!GetWindowRect(window, &current))
        return QStringLiteral("wallpaper_windows_geometry");
    if (EqualRect(&current, &monitor))
        return {};
    POINT corners[2]{{monitor.left, monitor.top}, {monitor.right, monitor.bottom}};
    SetLastError(ERROR_SUCCESS);
    if (!MapWindowPoints(nullptr, worker, corners, 2) && GetLastError() != ERROR_SUCCESS)
        return QStringLiteral("wallpaper_windows_geometry");
    // Both endpoints originate from EnumDisplayMonitors in the calling Qt DPI
    // context. Never multiply global logical coordinates by one screen's DPR.
    if (!SetWindowPos(window, HWND_BOTTOM, corners[0].x, corners[0].y, corners[1].x - corners[0].x,
                      corners[1].y - corners[0].y, SWP_NOACTIVATE | SWP_NOOWNERZORDER))
        return QStringLiteral("wallpaper_windows_geometry");
    return {};
}

class WindowsWallpaper final : public WallpaperPlatform {
    QElapsedTimer m_lastRequest;

  public:
    QString unavailableReason() const override {
        return QGuiApplication::platformName() == "windows" ? QString()
                                                            : QStringLiteral("wallpaper_qpa_unavailable");
    }
    bool needsHealthCheck() const override { return true; }
    bool retryable() const override { return true; }
    QString prepare(QWindow *window, QScreen *screen) override {
        if (!screen)
            return QStringLiteral("wallpaper_display_missing");
        const HWND shell = GetShellWindow();
        const DWORD pid = processId(shell);
        if (!shell || !pid || !explorerProcess(pid))
            return QStringLiteral("wallpaper_windows_shell");
        MonitorSearch monitor{screen->name()};
        EnumDisplayMonitors(nullptr, nullptr, findMonitor, reinterpret_cast<LPARAM>(&monitor));
        if (!monitor.found)
            return QStringLiteral("wallpaper_windows_geometry");
        HWND worker = findWorker(pid, monitor.rect);
        if (!worker && (!m_lastRequest.isValid() || m_lastRequest.elapsed() >= 30000)) {
            // Explorer's undocumented request is optional and strictly bounded.
            // No hiding/reparenting of DefView, registry changes or Explorer restart.
            HWND progman = FindWindowW(L"Progman", nullptr);
            if (progman && processId(progman) == pid) {
                DWORD_PTR result = 0;
                m_lastRequest.start();
                if (!SendMessageTimeoutW(progman, 0x052c, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, &result))
                    return QStringLiteral("wallpaper_windows_shell");
                worker = findWorker(pid, monitor.rect);
            }
        }
        if (!worker)
            return QStringLiteral("wallpaper_windows_structure");
        window->setScreen(screen);
        window->setGeometry(screen->geometry());
        window->create();
        const HWND handle = reinterpret_cast<HWND>(window->winId());
        if (!IsWindow(handle))
            return QStringLiteral("wallpaper_surface_lost");
        auto *guard = new NativeGuard(window);
        if (!guard->install(handle))
            return QStringLiteral("wallpaper_windows_embed");
        if (!sameDpiContext(handle, worker))
            return QStringLiteral("wallpaper_windows_dpi");
        const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
        const auto exStyle = GetWindowLongPtrW(handle, GWL_EXSTYLE);
        if (!setLong(handle, GWL_STYLE, (style & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME)) | WS_CHILD) ||
            !setLong(handle, GWL_EXSTYLE,
                     (exStyle & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT |
                         WS_EX_LAYERED))
            return QStringLiteral("wallpaper_windows_embed");
        SetLastError(ERROR_SUCCESS);
        const HWND previous = SetParent(handle, worker);
        const DWORD error = GetLastError();
        if ((!previous && error != ERROR_SUCCESS) || GetParent(handle) != worker)
            return QStringLiteral("wallpaper_windows_embed");
        if (!SetPropW(handle, L"NekoTuneMusicWallpaper", reinterpret_cast<HANDLE>(1)) ||
            !SetWindowPos(handle, HWND_BOTTOM, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED))
            return QStringLiteral("wallpaper_windows_embed");
        guard->allow(worker);
        window->setProperty("wallpaperNativeHandle",
                            QVariant::fromValue<qulonglong>(reinterpret_cast<quintptr>(handle)));
        window->setProperty("wallpaperNativeParent",
                            QVariant::fromValue<qulonglong>(reinterpret_cast<quintptr>(worker)));
        return place(handle, worker, monitor.rect);
    }
    QString check(QWindow *window, QScreen *screen) override {
        // Do not call winId() here: it can create a replacement unembedded HWND.
        const auto handle = reinterpret_cast<HWND>(
            static_cast<quintptr>(window->property("wallpaperNativeHandle").toULongLong()));
        const auto worker = reinterpret_cast<HWND>(
            static_cast<quintptr>(window->property("wallpaperNativeParent").toULongLong()));
        const auto *guard = dynamic_cast<NativeGuard *>(window->findChild<QObject *>("wallpaperNativeGuard"));
        const HWND shell = GetShellWindow();
        const DWORD pid = processId(shell);
        if (!screen || !guard || !guard->ownsHandle(handle) || !IsWindow(handle) ||
            processId(handle) != GetCurrentProcessId() || GetParent(handle) != worker || !pid)
            return QStringLiteral("wallpaper_surface_lost");
        MonitorSearch monitor{screen->name()};
        EnumDisplayMonitors(nullptr, nullptr, findMonitor, reinterpret_cast<LPARAM>(&monitor));
        if (!monitor.found || !validWorker(worker, pid, monitor.rect) ||
            findWorker(pid, monitor.rect) != worker)
            return QStringLiteral("wallpaper_windows_structure");
        const auto required = WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED;
        if (!(GetWindowLongPtrW(handle, GWL_STYLE) & WS_CHILD) ||
            (GetWindowLongPtrW(handle, GWL_EXSTYLE) & required) != required ||
            !sameDpiContext(handle, worker))
            return QStringLiteral("wallpaper_windows_embed");
        return place(handle, worker, monitor.rect);
    }
};
} // namespace
std::unique_ptr<WallpaperPlatform> createWallpaperPlatform() { return std::make_unique<WindowsWallpaper>(); }
