#pragma once
#include "domain/extensions/extension_backend.h"
#include <QHash>
#include <QSet>

namespace nekotune {
/// Keyring access for extension secrets. params carries extensionId, key, operation
/// (get/set/delete) and value; get replies with {value}, null when absent.
/// Kugou's legacy account keys keep their pre-plugin storage identity.
void extensionSecret(const QVariantMap &params, IExtensionBackend::Completion done);

class PackageStore;
/// Per-extension keyring access plus the values to redact from logs.
class SecretVault {
  public:
    using Completion = std::function<void(Result<QVariantMap>)>;
    explicit SecretVault(const PackageStore &packages) : m_packages(packages) {}
    void load(const QString &extensionId);
    void get(const QString &extensionId, const QString &key, Completion done);
    void set(const QString &extensionId, const QString &key, const QString &value, Completion done);
    void remove(const QString &extensionId, const QString &key, Completion done);
    /// Deletes every stored secret of an extension (uninstall with clearData).
    void clear(const QString &extensionId, std::function<void(Result<void>)> done);
    QString redact(QString message) const;

  private:
    void request(const QString &extensionId, const QString &operation, const QString &key,
                 const QString &value, Completion done);
    void clearKeys(const QString &extensionId, QStringList keys, std::function<void(Result<void>)> done);
    const PackageStore &m_packages;
    QHash<QString, QSet<QString>> m_keys;
    QSet<QString> m_values;
};
} // namespace nekotune
