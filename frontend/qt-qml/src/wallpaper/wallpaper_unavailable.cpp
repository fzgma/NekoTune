#include "wallpaper/wallpaper_platform.h"

namespace {
class Unavailable final : public WallpaperPlatform {
  public:
    QString unavailableReason() const override { return QStringLiteral("wallpaper_build_unavailable"); }
    QString prepare(QWindow *, QScreen *) override { return unavailableReason(); }
    QString check(QWindow *, QScreen *) override { return unavailableReason(); }
};
} // namespace
std::unique_ptr<WallpaperPlatform> createWallpaperPlatform() { return std::make_unique<Unavailable>(); }
