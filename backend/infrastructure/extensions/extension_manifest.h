#pragma once
#include "domain/result.h"
#include <QVariantMap>

namespace nekotune {
/// Validated extension.json. data keeps the original JSON for snapshots and contributions.
struct ExtensionManifest {
    QVariantMap data;
    /// Theme packages that ship QML/JavaScript still need explicit trust.
    bool themeHasCode = false;
    /// Absolute library path for this platform; empty for UI-only and theme packages.
    QString library;
    /// Why the package cannot run here (missing platform build, Qt mismatch); empty when runnable.
    QString unavailable;

    QString id() const { return data.value("id").toString(); }
    QString version() const { return data.value("version").toString(); }
    bool isTheme() const { return data.value("type").toString() == "theme"; }
    bool needsTrust() const { return !isTheme() || themeHasCode; }
    // Theme packages have no backend code; their QML is gated by the frontend until trusted.
    bool needsTrustToRun() const { return !isTheme(); }
    QVariantMap dependencies() const { return data.value("dependencies").toMap(); }
    QVariantMap contributes() const { return data.value("contributes").toMap(); }
};

bool validExtensionId(const QString &id);
/// "linux-x86_64" style key matching nekotune_add_extension's library map.
QString extensionPlatform();
/// Resolve a relative file below directory, rejecting absolute paths, escapes and non-files.
Result<QString> containedFile(const QString &directory, const QString &relative);
Result<ExtensionManifest> readManifest(const QString &directory);
} // namespace nekotune
