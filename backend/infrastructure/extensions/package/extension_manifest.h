#pragma once
#include "domain/result.h"
#include <QMap>
#include <QVariantMap>

namespace nekotune {
/// Validated extension.json.
struct ExtensionManifest {
    QString id;
    QString name;
    QString version;
    QString description;
    /// "extension" unless the manifest declares another type; "theme" marks theme packages.
    QString type = "extension";
    int apiVersion = 0;
    bool nativeModules = false;
    QVariantMap defaultConfig;
    /// Extension ID → SemVer range.
    QMap<QString, QString> dependencies;
    /// Validated contributions keyed by kind; items keep their local IDs.
    QVariantMap contributes;
    /// The original JSON, only for re-exporting theme packages.
    QVariantMap raw;
    /// Theme packages that ship QML/JavaScript still need explicit trust.
    bool themeHasCode = false;
    /// Absolute library path for this platform; empty for UI-only and theme packages.
    QString library;
    /// Why the package cannot run here (missing platform build, Qt mismatch); empty when runnable.
    QString unavailable;

    bool isTheme() const { return type == "theme"; }
    bool needsTrust() const { return !isTheme() || themeHasCode; }
    // Theme packages have no backend code; their QML is gated by the frontend until trusted.
    bool needsTrustToRun() const { return !isTheme(); }
    /// Stand-in for an installed package whose manifest can no longer be read.
    static ExtensionManifest placeholder(const QString &id) {
        ExtensionManifest manifest;
        manifest.id = manifest.name = id;
        manifest.version = "0.0.0";
        return manifest;
    }
};

bool validExtensionId(const QString &id);
/// "linux-x86_64" style key matching nekotune_add_extension's library map.
QString extensionPlatform();
/// Resolve a relative file below directory, rejecting absolute paths, escapes and non-files.
Result<QString> containedFile(const QString &directory, const QString &relative);
Result<ExtensionManifest> readManifest(const QString &directory);
} // namespace nekotune
