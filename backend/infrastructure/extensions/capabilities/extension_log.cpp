#include "infrastructure/extensions/capabilities/extension_log.h"
#include "infrastructure/extensions/capabilities/extension_secrets.h"
#include <QDateTime>

namespace nekotune {
void ExtensionLog::record(const QString &extensionId, const QString &level, const QString &message) {
    auto &list = m_logs[extensionId];
    list.append({{"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                 {"level", level},
                 {"message", m_secrets.redact(message).left(16'384)}});
    if (list.size() > 500)
        list.remove(0, list.size() - 500);
}
QVariantList ExtensionLog::entries(const QString &extensionId) const {
    QVariantList logs;
    for (auto item : m_logs.value(extensionId)) {
        item.insert("message", m_secrets.redact(item.value("message").toString()));
        logs.append(item);
    }
    return logs;
}
void ExtensionLog::remove(const QString &extensionId) { m_logs.remove(extensionId); }
} // namespace nekotune
