#pragma once
#include <QVariantMap>

// Token resolution shared by the backend (theme export) and the frontend (display).
// The catalog is passed in because each side loads shared/themes/catalog.json from its own resources.
namespace nekotune::themes {
/// Catalog defaults for one color scheme: every token's default, plus the "light" overrides when !dark.
QVariantMap baseTokens(const QVariantMap &catalog, bool dark);
/// Final token values for one theme descriptor. Order: base values, common "tokens", then the
/// variant "tokens" of the requested scheme. Unknown token names are ignored.
QVariantMap resolveThemeTokens(const QVariantMap &catalog, const QVariantMap &theme, bool dark);
/// True when the theme replaces any part of the main window: shell, bottomPlayer, background or pages.
/// Wallpaper alone does not count as a layout.
bool providesLayout(const QVariantMap &theme);
/// True when the theme declares ui.wallpaper.
bool providesWallpaper(const QVariantMap &theme);
} // namespace nekotune::themes
