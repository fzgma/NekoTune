#include "infrastructure/appearance/appearance_service.h"
#include "infrastructure/extensions/capabilities/contribution_registry.h"
#include "infrastructure/extensions/package/json_file.h"
#include "infrastructure/extensions/package/theme_package.h"
#include "themes/theme_resolution.h"
#include <QFileInfo>

namespace nekotune {
AppearanceService::AppearanceService(ContributionCatalog &catalog, QString path, QObject *parent)
    : IAppearanceBackend(parent), m_catalog(catalog), m_path(std::move(path)),
      m_selections(readJsonFile(m_path, QVariantMap{}).toMap().value("selections").toMap()) {}

void AppearanceService::request(const QString &method, const QVariantMap &params, Completion done) {
    if (method == "appearance.get")
        done(QVariantMap{{"selections", m_selections}});
    else if (method == "appearance.select")
        done(select(params));
    else if (method == "appearance.export_theme")
        done(exportTheme(params));
    else
        done(failure("Unknown appearance operation: " + method));
}
Result<void> AppearanceService::save() {
    return writeJsonFile(m_path, QVariantMap{{"selections", m_selections}});
}
Result<void> AppearanceService::migrate(const QVariantMap &legacy) {
    if (QFileInfo(m_path).isFile())
        return {};
    const auto previous = m_selections;
    m_selections = legacy;
    if (auto saved = save(); !saved) {
        m_selections = previous;
        return saved;
    }
    if (m_selections != previous)
        emit eventReady({{"event", "appearance.changed"}, {"selections", m_selections}});
    return {};
}
QStringList AppearanceService::selectedOwners() const {
    QStringList owners;
    for (const auto &value : m_selections) {
        const auto owner = value.toString().section('/', 0, 0);
        if (!owner.isEmpty() && !owners.contains(owner))
            owners.append(owner);
    }
    return owners;
}
Result<void> AppearanceService::forgetThemePackage(const QString &extensionId) {
    QVariantMap changes;
    for (const auto *key : {"theme", "themeLayout"})
        if (m_selections.value(QLatin1String(key)).toString().startsWith(extensionId + "/"))
            changes.insert(QLatin1String(key), QString());
    if (changes.isEmpty())
        return {};
    const auto selected = select({{"selections", changes}});
    return selected ? Result<void>{} : Result<void>(selected.error());
}
Result<void> AppearanceService::validate(const QString &slot, const QString &contribution) const {
    if (contribution.isEmpty())
        return {};
    if (slot == "theme")
        for (const auto &theme : themes::catalog().value("themes").toList())
            if (theme.toMap().value("id") == contribution)
                return {};
    const bool themeSlot = slot == "theme" || slot == "themeLayout";
    const auto items = themeSlot ? m_catalog.themes() : m_catalog.contributions("slots");
    for (const auto &item : items) {
        const auto map = item.toMap();
        if (themeSlot && !map.value("available").toBool())
            continue;
        if (map.value("id") != contribution)
            continue;
        if (slot == "theme" || (slot == "themeLayout" && themes::providesLayout(map)) ||
            (!themeSlot && map.value("slot") == slot))
            return {};
    }
    return failure("Contribution is unavailable for this slot");
}
Result<QVariantMap> AppearanceService::select(const QVariantMap &params) {
    // {selections: {slot: contribution}} updates several slots atomically; {slot, contribution} is the legacy form.
    QVariantMap changes;
    if (params.contains("selections")) {
        if (params.value("selections").typeId() != QMetaType::QVariantMap)
            return failure("Invalid selection");
        changes = params.value("selections").toMap();
    } else if (params.value("slot").typeId() == QMetaType::QString) {
        changes.insert(params.value("slot").toString(), params.value("contribution"));
    }
    if (changes.isEmpty())
        return failure("Invalid selection");
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        if (it.key().isEmpty() || it.value().typeId() != QMetaType::QString)
            return failure("Invalid selection");
        // Unchanged entries keep a selection whose package is temporarily disabled.
        if (m_selections.value(it.key()) == it.value())
            continue;
        if (auto valid = validate(it.key(), it.value().toString()); !valid)
            return valid.error();
    }
    const auto previous = m_selections;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it)
        m_selections.insert(it.key(), it.value());
    if (auto saved = save(); !saved) {
        m_selections = previous;
        return saved.error();
    }
    if (m_selections != previous)
        emit eventReady({{"event", "appearance.changed"}, {"selections", m_selections}});
    return QVariantMap{{"selections", m_selections}};
}
Result<QVariantMap> AppearanceService::exportTheme(const QVariantMap &params) const {
    const auto id = params.value("contribution").toString().isEmpty() ? QStringLiteral("builtin/yozakura")
                                                                       : params.value("contribution").toString();
    const auto destination = params.value("path").toString();
    for (const auto &theme : themes::catalog().value("themes").toList())
        if (theme.toMap().value("id") == id)
            return themes::exportTheme(theme.toMap(), {}, {}, destination);
    const auto package = m_catalog.themePackage(id);
    if (!package)
        return package.error();
    return themes::exportTheme(package.value().theme, package.value().directory, package.value().manifest, destination);
}
} // namespace nekotune
