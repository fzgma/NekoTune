#include "wallpaper/wallpaper_platform.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>

#include <QElapsedTimer>
#include <QDebug>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QScreen>
#include <QStringList>
#include <QVariant>
#include <QWindow>
#include <algorithm>
#include <unordered_set>
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
    if (!process) {
        qWarning() << "Wallpaper Windows cannot open shell process" << pid << GetLastError();
        return false;
    }
    wchar_t path[32768]{}, directory[MAX_PATH]{};
    DWORD size = 32768;
    const bool queried = QueryFullProcessImageNameW(process, 0, path, &size);
    const DWORD queryError = queried ? ERROR_SUCCESS : GetLastError();
    CloseHandle(process);
    const UINT directoryLength = GetWindowsDirectoryW(directory, MAX_PATH);
    if (!queried || !directoryLength || directoryLength >= MAX_PATH) {
        qWarning() << "Wallpaper Windows cannot identify shell process image" << pid << queryError
                   << directoryLength;
        return false;
    }
    const QString imagePath = QString::fromWCharArray(path);
    const QString expectedPath = QString::fromWCharArray(directory) + "\\explorer.exe";
    if (imagePath.compare(expectedPath, Qt::CaseInsensitive) != 0) {
        qWarning() << "Wallpaper Windows shell process is not Explorer" << pid << imagePath << expectedPath;
        return false;
    }
    return true;
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
    QString device;
    RECT rect{};
    bool found = false;
};
BOOL CALLBACK findMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto &search = *reinterpret_cast<MonitorSearch *>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO *>(&info)) &&
        search.device.compare(QString::fromWCharArray(info.szDevice), Qt::CaseInsensitive) == 0) {
        search.rect = info.rcMonitor;
        search.found = true;
        return FALSE;
    }
    return TRUE;
}
struct DisplayPath {
    QString device;
    QString friendlyName;
};
bool activeDisplayPaths(std::vector<DisplayPath> &displays) {
    UINT32 pathCount = 0, modeCount = 0;
    LONG status = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
    if (status != ERROR_SUCCESS)
        return false;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
    if (status != ERROR_SUCCESS) {
        if (status != ERROR_INSUFFICIENT_BUFFER)
            return false;
        status = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
        if (status != ERROR_SUCCESS)
            return false;
        paths.resize(pathCount);
        modes.resize(modeCount);
        status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
        if (status != ERROR_SUCCESS)
            return false;
    }
    paths.resize(pathCount);
    displays.clear();
    for (const auto &path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS)
            continue;

        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS)
            continue;
        displays.push_back({QString::fromWCharArray(source.viewGdiDeviceName),
                            QString::fromWCharArray(target.monitorFriendlyDeviceName).trimmed()});
    }
    return !displays.empty();
}
QString monitorDeviceForScreen(QScreen *screen) {
    if (!screen)
        return {};
    const QString screenName = screen->name().trimmed();
    std::vector<DisplayPath> displays;
    if (!activeDisplayPaths(displays)) {
        // Retain the exact native-name path if DisplayConfig is unavailable.
        return screenName.startsWith(QStringLiteral("\\\\.\\"), Qt::CaseInsensitive) ? screenName : QString();
    }
    std::vector<QString> candidates;
    for (const auto &display : displays) {
        if (screenName.compare(display.device, Qt::CaseInsensitive) == 0 ||
            (!display.friendlyName.isEmpty() && screenName.compare(display.friendlyName, Qt::CaseInsensitive) == 0)) {
            if (std::none_of(candidates.cbegin(), candidates.cend(), [&](const QString &candidate) {
                    return candidate.compare(display.device, Qt::CaseInsensitive) == 0;
                }))
                candidates.push_back(display.device);
        }
    }
    return candidates.size() == 1 ? candidates.front() : QString();
}
bool monitorForScreen(QScreen *screen, RECT &rect) {
    const QString screenName = screen ? screen->name().trimmed() : QString();
    QString device = monitorDeviceForScreen(screen);
    MonitorSearch search{device};
    if (!device.isEmpty())
        EnumDisplayMonitors(nullptr, nullptr, findMonitor, reinterpret_cast<LPARAM>(&search));
    if (!search.found && !screenName.isEmpty() && device.compare(screenName, Qt::CaseInsensitive) != 0) {
        // Older display stacks may expose the GDI device name directly from Qt
        // even when DisplayConfig could not provide an active-path mapping.
        search = MonitorSearch{screenName};
        EnumDisplayMonitors(nullptr, nullptr, findMonitor, reinterpret_cast<LPARAM>(&search));
        if (search.found)
            device = screenName;
    }
    if (!search.found) {
        qWarning() << (device.isEmpty() ? "Wallpaper Windows display mapping was missing or ambiguous"
                                        : "Wallpaper Windows display has no monitor rectangle")
                   << screenName << device;
        return false;
    }
    rect = search.rect;
    return true;
}
bool contains(const RECT &outer, const RECT &inner) {
    return outer.left <= inner.left && outer.top <= inner.top && outer.right >= inner.right &&
           outer.bottom >= inner.bottom;
}
struct WorkerChildCheck {
    HWND unsafeChild = nullptr;
};
BOOL CALLBACK checkWorkerChild(HWND child, LPARAM data) {
    auto &check = *reinterpret_cast<WorkerChildCheck *>(data);
    if (processId(child) != GetCurrentProcessId() ||
        GetPropW(child, L"NekoTuneMusicWallpaper") != reinterpret_cast<HANDLE>(1)) {
        check.unsafeChild = child;
        return FALSE;
    }
    return TRUE;
}
bool validWorker(HWND worker, DWORD pid, const RECT &monitor, QString *reason = nullptr) {
    auto reject = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return false;
    };
    if (!IsWindow(worker))
        return reject("invalid handle");
    if (processId(worker) != pid)
        return reject("different process");
    if (windowClass(worker) != "WorkerW")
        return reject("different window class");
    if (!IsWindowVisible(worker))
        return reject("not visible");
    if (GetParent(worker))
        return reject("has parent");
    if (GetWindow(worker, GW_OWNER))
        return reject("has owner");
    if (childClass(worker, L"SHELLDLL_DefView"))
        return reject("contains desktop icon view");
    WorkerChildCheck childCheck;
    EnumChildWindows(worker, checkWorkerChild, reinterpret_cast<LPARAM>(&childCheck));
    if (childCheck.unsafeChild) {
        if (reason)
            *reason = QStringLiteral("contains unrelated child %1 (%2)")
                          .arg(reinterpret_cast<quintptr>(childCheck.unsafeChild), 0, 16)
                          .arg(windowClass(childCheck.unsafeChild));
        return false;
    }
    RECT rect{};
    if (!GetWindowRect(worker, &rect))
        return reject(QStringLiteral("GetWindowRect failed (%1)").arg(GetLastError()));
    if (!contains(rect, monitor))
        return reject(QStringLiteral("does not cover monitor; worker=(%1,%2)-(%3,%4), monitor=(%5,%6)-(%7,%8)")
                           .arg(rect.left).arg(rect.top).arg(rect.right).arg(rect.bottom)
                           .arg(monitor.left).arg(monitor.top).arg(monitor.right).arg(monitor.bottom));
    if (reason)
        reason->clear();
    return true;
}
HWND findWorker(DWORD pid, const RECT &monitor) {
    std::vector<HWND> windows;
    if (!EnumWindows(enumerate, reinterpret_cast<LPARAM>(&windows))) {
        qWarning() << "Wallpaper Windows top-level enumeration failed" << GetLastError();
        return nullptr;
    }
    std::vector<HWND> iconHosts;
    std::unordered_set<HWND> workerCandidates;
    QStringList rejected;
    for (HWND window : windows) {
        const DWORD windowPid = processId(window);
        if (windowPid == pid && hasDesktopIcons(window, pid))
            iconHosts.push_back(window);
        if (windowClass(window) != "WorkerW")
            continue;
        if (windowPid != pid) {
            rejected.append(QStringLiteral("%1 pid=%2: different from shell pid")
                                .arg(reinterpret_cast<quintptr>(window), 0, 16).arg(windowPid));
            continue;
        }
        workerCandidates.insert(window);
    }

    struct OrderedWorker {
        HWND handle;
        size_t distance;
    };
    std::vector<OrderedWorker> orderedWorkers;
    std::unordered_set<HWND> aboveIconHosts;
    const size_t maxSteps = windows.size() + 1;
    for (HWND host : iconHosts) {
        std::unordered_set<HWND> visited;
        HWND window = GetWindow(host, GW_HWNDNEXT);
        for (size_t distance = 1; window && distance <= maxSteps && IsWindow(window) &&
                                    visited.insert(window).second;
             ++distance, window = GetWindow(window, GW_HWNDNEXT)) {
            if (!workerCandidates.contains(window))
                continue;
            auto existing = std::find_if(orderedWorkers.begin(), orderedWorkers.end(),
                                         [window](const OrderedWorker &candidate) {
                                             return candidate.handle == window;
                                         });
            if (existing == orderedWorkers.end())
                orderedWorkers.push_back({window, distance});
            else
                existing->distance = std::min(existing->distance, distance);
        }
        visited.clear();
        window = GetWindow(host, GW_HWNDPREV);
        for (size_t distance = 1; window && distance <= maxSteps && IsWindow(window) &&
                                    visited.insert(window).second;
             ++distance, window = GetWindow(window, GW_HWNDPREV)) {
            if (workerCandidates.contains(window))
                aboveIconHosts.insert(window);
        }
    }
    std::sort(orderedWorkers.begin(), orderedWorkers.end(), [](const OrderedWorker &left, const OrderedWorker &right) {
        return left.distance < right.distance;
    });
    HWND selected = nullptr;
    size_t selectedDistance = 0;
    for (const OrderedWorker &candidate : orderedWorkers) {
        if (selected && candidate.distance > selectedDistance)
            break;
        QString reason;
        if (validWorker(candidate.handle, pid, monitor, &reason)) {
            if (!selected) {
                selected = candidate.handle;
                selectedDistance = candidate.distance;
            } else {
                qWarning() << "Wallpaper Windows found ambiguous verified WorkerW candidates" << selected
                           << candidate.handle << "Z-order distance" << selectedDistance;
                return nullptr;
            }
            continue;
        }
        rejected.append(QStringLiteral("%1: %2 (Z-order distance %3)")
                            .arg(reinterpret_cast<quintptr>(candidate.handle), 0, 16)
                            .arg(reason)
                            .arg(candidate.distance));
    }
    if (selected)
        return selected;
    for (HWND candidate : workerCandidates) {
        const bool reached = std::any_of(orderedWorkers.cbegin(), orderedWorkers.cend(),
                                         [candidate](const OrderedWorker &ordered) {
                                             return ordered.handle == candidate;
                                         });
        if (!reached) {
            QString reason;
            const bool otherwiseValid = validWorker(candidate, pid, monitor, &reason);
            const QString position = aboveIconHosts.contains(candidate)
                                         ? QStringLiteral("above a desktop icon host")
                                         : QStringLiteral("not reachable from a desktop icon host in either Z direction");
            rejected.append(QStringLiteral("%1: %2; %3")
                                .arg(reinterpret_cast<quintptr>(candidate), 0, 16)
                                .arg(position)
                                .arg(otherwiseValid ? QStringLiteral("passes other checks") : reason));
        }
    }
    qWarning() << "Wallpaper Windows found no verified WorkerW; shell pid" << pid << "icon host found"
               << !iconHosts.empty() << "top-level window count" << windows.size() << "WorkerW candidates" << rejected;
    return nullptr;
}
struct DesktopHost {
    HWND parent = nullptr;
    HWND worker = nullptr;
    HWND iconView = nullptr;
    bool raisedDesktop = false;
    explicit operator bool() const { return parent && worker; }
    HWND insertAfter() const { return raisedDesktop ? iconView : HWND_BOTTOM; }
};
// Windows 11's raised desktop keeps the icon view and wallpaper WorkerW as
// children of Progman instead of exposing the WorkerW as a top-level window.
bool validRaisedWorker(HWND worker, HWND progman, DWORD pid, QString *reason = nullptr) {
    auto reject = [reason](const QString &why) {
        if (reason)
            *reason = why;
        return false;
    };
    if (!IsWindow(worker))
        return reject("invalid handle");
    if (processId(worker) != pid)
        return reject("different process");
    if (windowClass(worker) != "WorkerW")
        return reject("different window class");
    if (GetParent(worker) != progman)
        return reject("not a direct Progman child");
    if (childClass(worker, L"SHELLDLL_DefView"))
        return reject("contains desktop icon view");
    if (reason)
        reason->clear();
    return true;
}
DesktopHost findRaisedDesktopHost(HWND progman, DWORD pid, const RECT &monitor) {
    const auto fail = [progman](const QString &reason) {
        qWarning() << "Wallpaper Windows raised desktop host is not verified" << progman << reason;
        return DesktopHost{};
    };
    if (!IsWindow(progman) || processId(progman) != pid || windowClass(progman) != "Progman")
        return fail("invalid Progman");
    if (!hasDesktopIcons(progman, pid))
        return fail("Progman has no verified desktop icon view");
    const HWND iconView = childClass(progman, L"SHELLDLL_DefView");
    RECT rect{};
    if (!GetWindowRect(progman, &rect))
        return fail(QStringLiteral("Progman GetWindowRect failed (%1)").arg(GetLastError()));
    if (!contains(rect, monitor))
        return fail(QStringLiteral("Progman does not cover monitor; progman=(%1,%2)-(%3,%4), monitor=(%5,%6)-(%7,%8)")
                        .arg(rect.left).arg(rect.top).arg(rect.right).arg(rect.bottom)
                        .arg(monitor.left).arg(monitor.top).arg(monitor.right).arg(monitor.bottom));

    HWND worker = nullptr;
    QString rejected;
    bool belowIconView = false;
    std::unordered_set<HWND> visited;
    for (HWND child = GetWindow(progman, GW_CHILD); child && visited.size() < 256 &&
                                              IsWindow(child) && GetParent(child) == progman &&
                                              visited.insert(child).second;
         child = GetWindow(child, GW_HWNDNEXT)) {
        if (child == iconView)
            belowIconView = true;
        if (!belowIconView || windowClass(child) != "WorkerW" || processId(child) != pid)
            continue;
        QString reason;
        if (!validRaisedWorker(child, progman, pid, &reason)) {
            rejected = reason;
            continue;
        }
        if (worker) {
            qWarning() << "Wallpaper Windows found ambiguous raised WorkerW candidates" << worker << child;
            return {};
        }
        worker = child;
    }
    if (!worker)
        return fail(rejected.isEmpty() ? QStringLiteral("no verified WorkerW below the desktop icon view")
                                       : QStringLiteral("WorkerW below the desktop icon view rejected: %1").arg(rejected));
    static HWND loggedProgman = nullptr, loggedIconView = nullptr, loggedWorker = nullptr;
    if (loggedProgman != progman || loggedIconView != iconView || loggedWorker != worker) {
        qInfo() << "Wallpaper Windows verified raised desktop host; Progman" << progman << "icon view" << iconView
                << "WorkerW" << worker;
        loggedProgman = progman;
        loggedIconView = iconView;
        loggedWorker = worker;
    }
    return {progman, worker, iconView, true};
}
bool raisedDesktop(HWND progman, DWORD pid) {
    return IsWindow(progman) && processId(progman) == pid && windowClass(progman) == "Progman" &&
           (GetWindowLongPtrW(progman, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP);
}
DesktopHost findDesktopHost(HWND progman, DWORD pid, const RECT &monitor) {
    if (raisedDesktop(progman, pid))
        return findRaisedDesktopHost(progman, pid, monitor);
    const HWND worker = findWorker(pid, monitor);
    return worker ? DesktopHost{worker, worker, nullptr, false} : DesktopHost{};
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
QString place(HWND window, HWND parent, HWND insertAfter, const RECT &monitor, const QString &screenName) {
    RECT current{};
    if (!GetWindowRect(window, &current)) {
        qWarning() << "Wallpaper Windows GetWindowRect failed" << screenName << GetLastError();
        return QStringLiteral("wallpaper_windows_geometry");
    }
    if (EqualRect(&current, &monitor)) {
        if (!SetWindowPos(window, insertAfter, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
            qWarning() << "Wallpaper Windows Z-order update failed" << screenName << GetLastError();
            return QStringLiteral("wallpaper_windows_embed");
        }
        return {};
    }
    POINT corners[2]{{monitor.left, monitor.top}, {monitor.right, monitor.bottom}};
    SetLastError(ERROR_SUCCESS);
    if (!MapWindowPoints(nullptr, parent, corners, 2) && GetLastError() != ERROR_SUCCESS) {
        qWarning() << "Wallpaper Windows MapWindowPoints failed" << screenName << GetLastError();
        return QStringLiteral("wallpaper_windows_geometry");
    }
    // Both endpoints originate from EnumDisplayMonitors in the calling Qt DPI
    // context. Never multiply global logical coordinates by one screen's DPR.
    if (!SetWindowPos(window, insertAfter, corners[0].x, corners[0].y, corners[1].x - corners[0].x,
                      corners[1].y - corners[0].y, SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {
        qWarning() << "Wallpaper Windows SetWindowPos failed" << screenName << GetLastError()
                   << "mapped rectangle" << corners[0].x << corners[0].y << corners[1].x << corners[1].y;
        return QStringLiteral("wallpaper_windows_geometry");
    }
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
        if (!shell || !pid) {
            qWarning() << "Wallpaper Windows shell window is unavailable" << shell << pid;
            return QStringLiteral("wallpaper_windows_shell");
        }
        if (!explorerProcess(pid))
            return QStringLiteral("wallpaper_windows_shell");
        RECT monitor{};
        if (!monitorForScreen(screen, monitor))
            return QStringLiteral("wallpaper_windows_geometry");
        HWND progman = FindWindowW(L"Progman", nullptr);
        DesktopHost host = findDesktopHost(progman, pid, monitor);
        if (!host && (!m_lastRequest.isValid() || m_lastRequest.elapsed() >= 30000)) {
            // Explorer's undocumented request is optional and strictly bounded.
            // No hiding/reparenting of DefView, registry changes or Explorer restart.
            if (progman && processId(progman) == pid) {
                DWORD_PTR result = 0;
                m_lastRequest.start();
                if (!SendMessageTimeoutW(progman, 0x052c, 0xD, 0x1, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000,
                                         &result)) {
                    qWarning() << "Wallpaper Windows Explorer WorkerW request timed out" << GetLastError();
                    return QStringLiteral("wallpaper_windows_shell");
                }
                host = findDesktopHost(progman, pid, monitor);
            }
        }
        if (!host)
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
        if (!sameDpiContext(handle, host.parent))
            return QStringLiteral("wallpaper_windows_dpi");
        const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
        const auto exStyle = GetWindowLongPtrW(handle, GWL_EXSTYLE);
        if (!setLong(handle, GWL_STYLE, (style & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME)) | WS_CHILD) ||
            !setLong(handle, GWL_EXSTYLE,
                     (exStyle & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT |
                         WS_EX_LAYERED))
            return QStringLiteral("wallpaper_windows_embed");
        SetLastError(ERROR_SUCCESS);
        const HWND previous = SetParent(handle, host.parent);
        const DWORD error = GetLastError();
        if ((!previous && error != ERROR_SUCCESS) || GetParent(handle) != host.parent)
            return QStringLiteral("wallpaper_windows_embed");
        if (!SetPropW(handle, L"NekoTuneMusicWallpaper", reinterpret_cast<HANDLE>(1)) ||
            !SetWindowPos(handle, host.insertAfter(), 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_FRAMECHANGED))
            return QStringLiteral("wallpaper_windows_embed");
        guard->allow(host.parent);
        window->setProperty("wallpaperNativeHandle",
                            QVariant::fromValue<qulonglong>(reinterpret_cast<quintptr>(handle)));
        window->setProperty("wallpaperNativeParent",
                            QVariant::fromValue<qulonglong>(reinterpret_cast<quintptr>(host.parent)));
        window->setProperty("wallpaperNativeWorker",
                            QVariant::fromValue<qulonglong>(reinterpret_cast<quintptr>(host.worker)));
        window->setProperty("wallpaperNativeIconView",
                            QVariant::fromValue<qulonglong>(reinterpret_cast<quintptr>(host.iconView)));
        window->setProperty("wallpaperNativeRaisedDesktop", host.raisedDesktop);
        return place(handle, host.parent, host.insertAfter(), monitor, screen->name());
    }
    QString check(QWindow *window, QScreen *screen) override {
        // Do not call winId() here: it can create a replacement unembedded HWND.
        const auto handle = reinterpret_cast<HWND>(
            static_cast<quintptr>(window->property("wallpaperNativeHandle").toULongLong()));
        const auto parent = reinterpret_cast<HWND>(
            static_cast<quintptr>(window->property("wallpaperNativeParent").toULongLong()));
        const auto worker = reinterpret_cast<HWND>(
            static_cast<quintptr>(window->property("wallpaperNativeWorker").toULongLong()));
        const auto iconView = reinterpret_cast<HWND>(
            static_cast<quintptr>(window->property("wallpaperNativeIconView").toULongLong()));
        const bool raised = window->property("wallpaperNativeRaisedDesktop").toBool();
        const auto *guard = dynamic_cast<NativeGuard *>(window->findChild<QObject *>("wallpaperNativeGuard"));
        const HWND shell = GetShellWindow();
        const DWORD pid = processId(shell);
        if (!screen || !guard || !guard->ownsHandle(handle) || !IsWindow(handle) ||
            processId(handle) != GetCurrentProcessId() || GetParent(handle) != parent || !pid)
            return QStringLiteral("wallpaper_surface_lost");
        RECT monitor{};
        if (!monitorForScreen(screen, monitor))
            return QStringLiteral("wallpaper_windows_structure");
        const DesktopHost current = findDesktopHost(FindWindowW(L"Progman", nullptr), pid, monitor);
        if (!current || current.parent != parent || current.worker != worker || current.iconView != iconView ||
            current.raisedDesktop != raised)
            return QStringLiteral("wallpaper_windows_structure");
        const auto required = WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_LAYERED;
        if (!(GetWindowLongPtrW(handle, GWL_STYLE) & WS_CHILD) ||
            (GetWindowLongPtrW(handle, GWL_EXSTYLE) & required) != required ||
            !sameDpiContext(handle, parent))
            return QStringLiteral("wallpaper_windows_embed");
        return place(handle, parent, current.insertAfter(), monitor, screen->name());
    }
};
} // namespace
std::unique_ptr<WallpaperPlatform> createWallpaperPlatform() { return std::make_unique<WindowsWallpaper>(); }
