#pragma once
#include "domain/result.h"
#include <QHash>
#include <QVariantMap>

namespace nekotune {
class PackageStore;
class ExtensionStorage {
  public:
    explicit ExtensionStorage(const PackageStore &packages) : m_packages(packages) {}
    void load(const QString &extensionId);
    void unload(const QString &extensionId);
    QVariant get(const QString &extensionId, const QString &key) const;
    Result<void> set(const QString &extensionId, const QString &key, const QVariant &value);
    QVariantMap readConfig(const QString &extensionId, const QVariantMap &defaults) const;
    Result<void> writeConfig(const QString &extensionId, const QVariantMap &config);

  private:
    const PackageStore &m_packages;
    QHash<QString, QVariantMap> m_storage;
};
} // namespace nekotune
