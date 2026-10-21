#pragma once
#include "domain/result.h"
#include <QStringList>
#include <QVariantMap>

namespace nekotune::themes {
constexpr qint64 archiveLimit = 20LL * 1024 * 1024;
constexpr qint64 unpackedLimit = 64LL * 1024 * 1024;
constexpr qint64 pixelLimit = 16'000'000;

/// shared/themes/catalog.json (token rules, light defaults and built-in themes).
const QVariantMap &catalog();
/// Validate a theme descriptor against the catalog and its resources below directory.
/// strict requires the schemaVersion 1 layout even when schemaVersion is absent.
Result<void> validate(const QVariantMap &theme, const QString &directory, bool strict = false);
struct PackageInfo {
    QStringList files;
    bool hasCode = false;
};
/// Inspect a type "theme" package: allowed file types, no links, size and pixel limits.
Result<PackageInfo> inspectPackage(const QString &directory, const QVariantMap &manifest);
/// Write a theme as an importable package. entry may be empty for built-in themes; full
/// packages are re-exported file by file.
Result<QVariantMap> exportTheme(const QVariantMap &theme, const QString &directory, const QVariantMap &manifest,
                                const QString &destination);
} // namespace nekotune::themes
