#pragma once
#include <QHash>
#include <QVariantMap>

namespace nekotune {
class SecretVault;
class ExtensionLog {
  public:
    explicit ExtensionLog(const SecretVault &secrets) : m_secrets(secrets) {}
    void record(const QString &extensionId, const QString &level, const QString &message);
    QVariantList entries(const QString &extensionId) const;
    void remove(const QString &extensionId);

  private:
    const SecretVault &m_secrets;
    QHash<QString, QList<QVariantMap>> m_logs;
};
} // namespace nekotune
