#pragma once

#include <QString>
#include <memory>

class QScreen;
class QWindow;

// GUI-thread-only platform boundary. prepare() must succeed before show().
// An empty error means success; a failed instance is always destroyed by the
// manager.
class WallpaperPlatform {
  public:
    virtual ~WallpaperPlatform() = default;
    virtual QString unavailableReason() const = 0;
    virtual QString prepare(QWindow *window, QScreen *screen) = 0;
    virtual QString check(QWindow *window, QScreen *screen) = 0;
    virtual bool needsHealthCheck() const { return false; }
    virtual bool retryable() const { return false; }
};

std::unique_ptr<WallpaperPlatform> createWallpaperPlatform();
