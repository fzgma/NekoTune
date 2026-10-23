#include "wallpaper/wallpaper_platform.h"

#include <LayerShellQt/Window>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandshellintegration_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <wayland-client-core.h>

namespace {
bool hasLayerIntegration(QWindow *window) {
    auto *native = dynamic_cast<QtWaylandClient::QWaylandWindow *>(window->handle());
    auto *extension = native ? dynamic_cast<QWaylandClientExtension *>(native->shellIntegration()) : nullptr;
    return extension && extension->isActive() &&
           QByteArray(extension->extensionInterface()->name) == "zwlr_layer_shell_v1";
}

class WaylandWallpaper final : public WallpaperPlatform {
  public:
    bool supportsTransparency() const override { return true; }
    QString unavailableReason() const override {
        if (!QGuiApplication::platformName().startsWith("wayland"))
            return QStringLiteral("wallpaper_qpa_unavailable");
        // A hidden platform window has no mapped shell surface. Verify the actual
        // integration, not an environment variable, before permitting any show().
        QWindow probe;
        LayerShellQt::Window::get(&probe);
        probe.create();
        return hasLayerIntegration(&probe) ? QString() : QStringLiteral("wallpaper_protocol_unavailable");
    }
    QString prepare(QWindow *window, QScreen *screen) override {
        if (!screen || !dynamic_cast<QtWaylandClient::QWaylandScreen *>(screen->handle()))
            return QStringLiteral("wallpaper_display_missing");
        window->setScreen(screen);
        window->resize(screen->size());
        // No Shell::useLayerShell(), no QT_WAYLAND_SHELL_INTEGRATION mutation.
        // These settings precede native creation and the first surface commit.
        auto *layer = LayerShellQt::Window::get(window);
        layer->setScreen(screen);
        layer->setScope(QStringLiteral("nekotune-music-wallpaper"));
        layer->setLayer(LayerShellQt::Window::LayerBottom);
        layer->setAnchors({LayerShellQt::Window::AnchorTop, LayerShellQt::Window::AnchorBottom,
                           LayerShellQt::Window::AnchorLeft, LayerShellQt::Window::AnchorRight});
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        layer->setActivateOnShow(false);
        layer->setCloseOnDismissed(true);
        window->create();
        // Failure must never fall through to an xdg-toplevel wallpaper.
        return check(window, screen);
    }
    QString check(QWindow *window, QScreen *screen) override {
        if (!screen || !hasLayerIntegration(window))
            return QStringLiteral("wallpaper_protocol_unavailable");
        return {};
    }
};
} // namespace
std::unique_ptr<WallpaperPlatform> createWallpaperPlatform() { return std::make_unique<WaylandWallpaper>(); }
