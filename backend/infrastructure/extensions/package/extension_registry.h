#pragma once
#include "domain/result.h"
#include <QStringList>
#include <QVariantMap>
#include <optional>

namespace nekotune {
/// The package an update replaced; kept until the new version activates.
struct PreviousPackage {
    QString directory;
    bool development = false;
    bool enabled = false;
    bool trusted = false;
};
/// Persistent part of one installed extension.
struct ExtensionRecord {
    QString id;
    QString directory;
    bool development = false;
    bool enabled = false;
    bool trusted = false;
    std::optional<PreviousPackage> previous;
    /// Last failure, already redacted; empty when the extension did not fail.
    QString failure;
};
struct RegistryState {
    QList<ExtensionRecord> entries;
    /// Read only for migrating to appearance.json.
    QVariantMap selections;
    QStringList developmentDirectories;
    /// Unknown keys (bundledSeen, ...) are kept so switching runtimes does not lose state.
    QVariantMap extra;
};

/// config/extensions/registry.json.
class ExtensionRegistry {
  public:
    explicit ExtensionRegistry(QString path) : m_path(std::move(path)) {}
    const QString &path() const { return m_path; }
    /// Skips invalid and duplicate IDs; a missing or unreadable file yields an empty state.
    RegistryState load() const;
    Result<void> save(const RegistryState &state) const;
    /// Every package directory the file on disk references (current and previous);
    /// nullopt when the file cannot be read, so cleanup keeps everything.
    std::optional<QStringList> storedDirectories() const;

  private:
    QString m_path;
};
} // namespace nekotune
