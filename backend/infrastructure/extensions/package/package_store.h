#pragma once
#include "domain/result.h"
#include <QStringList>

namespace nekotune {
/// Directory layout below AppPaths::extensionsDirectory(): packages/, data/<id>/, runtime/ and
/// registry.json. Paths follow NEKOTUNE_HOME at call time.
class PackageStore {
  public:
    QString root() const;
    QString registryPath() const;
    QString packagesDirectory() const;
    QString dataDirectory(const QString &id) const;
    QString runtimeDirectory() const;
    /// Canonical path of a development directory.
    Result<QString> developmentDirectory(const QString &path) const;
    /// Unpack a ZIP through a staging directory into packages/<id>-<version>-<uuid>.
    /// themeOnly applies theme archive limits up front.
    Result<QString> unpack(const QString &archive, bool themeOnly) const;
    /// Remove staging and host-named package directories not covered by references; returns
    /// one message per directory that could not be handled.
    QStringList clean(const QStringList &references) const;
};
} // namespace nekotune
