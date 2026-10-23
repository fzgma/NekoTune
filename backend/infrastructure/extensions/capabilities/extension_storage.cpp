#include "infrastructure/extensions/capabilities/extension_storage.h"
#include "infrastructure/extensions/package/json_file.h"
#include "infrastructure/extensions/package/package_store.h"
#include <QDir>
#include <QJsonDocument>

namespace nekotune {
void ExtensionStorage::load(const QString &extensionId) {
    m_storage.insert(
        extensionId,
        readJsonFile(QDir(m_packages.dataDirectory(extensionId)).filePath("storage.json"), QVariantMap{})
            .toMap());
}
void ExtensionStorage::unload(const QString &extensionId) { m_storage.remove(extensionId); }
QVariant ExtensionStorage::get(const QString &extensionId, const QString &key) const {
    return m_storage.value(extensionId).value(key);
}
Result<void> ExtensionStorage::set(const QString &extensionId, const QString &key, const QVariant &value) {
    if (key.size() > 256)
        return failure("Invalid storage key");
    auto next = m_storage.value(extensionId);
    next.insert(key, value);
    if (QJsonDocument::fromVariant(next).toJson(QJsonDocument::Compact).size() > 8 * 1024 * 1024)
        return failure("SDK storage limit exceeded; use dataDirectory for larger files");
    const auto saved =
        writeJsonFile(QDir(m_packages.dataDirectory(extensionId)).filePath("storage.json"), next);
    if (saved)
        m_storage.insert(extensionId, std::move(next));
    return saved;
}
QVariantMap ExtensionStorage::readConfig(const QString &extensionId, const QVariantMap &defaults) const {
    return readJsonFile(QDir(m_packages.dataDirectory(extensionId)).filePath("config.json"), defaults)
        .toMap();
}
Result<void> ExtensionStorage::writeConfig(const QString &extensionId, const QVariantMap &config) {
    return writeJsonFile(QDir(m_packages.dataDirectory(extensionId)).filePath("config.json"), config);
}
} // namespace nekotune
