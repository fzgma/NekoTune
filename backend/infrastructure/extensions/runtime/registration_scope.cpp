#include "infrastructure/extensions/runtime/registration_scope.h"
#include "infrastructure/extensions/package/extension_manifest.h"
#include "infrastructure/extensions/package/theme_package.h"

namespace nekotune {
namespace {
const QStringList sourceKinds{"pages", "settings", "slots", "toolbars"};
} // namespace

const QStringList &RegistrationScope::uiKinds() {
    static const QStringList kinds{"pages", "settings", "slots", "themes", "menus", "toolbars"};
    return kinds;
}
const QStringList &RegistrationScope::kinds() {
    static const QStringList kinds = QStringList{"services", "commands", "music", "lyrics", "shuffle"} + uiKinds();
    return kinds;
}
Result<void> RegistrationScope::add(const QString &kind, const QString &id, QVariantMap descriptor,
                                    const QString &extensionId, const QString &directory,
                                    const QVariantMap &contributes) {
    if (!kinds().contains(kind) || !validExtensionId(id))
        return failure(QStringLiteral("Invalid registration: %1/%2").arg(kind, id));
    bool contributed = false;
    for (const auto &item : contributes.value(kind).toList())
        contributed = contributed || item.toMap().value("id").toString() == id;
    if (contributed || contains(kind, id))
        return failure(QStringLiteral("Duplicate registration: %1/%2").arg(kind, id));
    if (sourceKinds.contains(kind))
        if (auto source = containedFile(directory, descriptor.value("source").toString()); !source)
            return source.error();
    if (kind == "themes")
        if (auto valid = themes::validate(descriptor, directory); !valid)
            return valid.error();
    descriptor.insert("id", extensionId + "/" + id);
    descriptor.insert("localId", id);
    descriptor.insert("extensionId", extensionId);
    m_items[kind].insert(id, descriptor);
    return {};
}
void RegistrationScope::remove(const QString &kind, const QString &localId) { m_items[kind].remove(localId); }
bool RegistrationScope::contains(const QString &kind, const QString &localId) const {
    return m_items.value(kind).contains(localId);
}
QVariantMap RegistrationScope::descriptor(const QString &kind, const QString &localId) const {
    return m_items.value(kind).value(localId);
}
QMap<QString, QVariantMap> RegistrationScope::list(const QString &kind) const { return m_items.value(kind); }
void RegistrationScope::clear() { m_items.clear(); }
} // namespace nekotune
