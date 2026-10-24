#include "infrastructure/extensions/capabilities/contribution_registry.h"
#include "infrastructure/extensions/plugin_manager.h"
#include "infrastructure/extensions/runtime/extension_runtime.h"
#include "infrastructure/extensions/package/theme_package.h"
#include <QDir>

namespace nekotune {
QVariantList PluginManager::contributionItems(const QString &id, const QString &kind) const {
    const auto &entry = m_entries[id];
    const auto run = m_runs.value(id);
    auto items = entry.manifest.contributes.value(kind).toList();
    if (run)
        for (const auto &item : run->registrations.list(kind))
            items.append(item);
    for (auto &value : items) {
        auto item = value.toMap();
        if (!item.contains("localId"))
            item.insert("localId", item.value("id"));
        item.insert("id", id + "/" + item.value("localId").toString());
        item.insert("extensionId", id);
        item.insert("generation", run ? run->generation : 0);
        if (item.contains("source"))
            item.insert("source", QDir::cleanPath(QDir(entry.directory).filePath(item.value("source").toString())));
        if (kind == "pages" && !item.contains("group"))
            item.insert("group", "primary");
        if (kind == "themes")
            item.insert("available", running(id));
        value = item;
    }
    return items;
}
QVariantList PluginManager::contributions(const QString &kind) const {
    QVariantList items;
    for (const auto &id : m_order)
        if (running(id))
            items.append(contributionItems(id, kind));
    return items;
}
QVariantList PluginManager::themes() const {
    QVariantList items;
    for (const auto &id : m_order)
        items.append(contributionItems(id, "themes"));
    return items;
}
Result<ContributionCatalog::ThemePackage> PluginManager::themePackage(const QString &id) const {
    const auto owner = id.section('/', 0, 0), local = id.section('/', 1);
    if (!m_entries.contains(owner))
        return failure("Theme is unavailable for export");
    const auto &entry = m_entries[owner];
    QVariantMap theme;
    for (const auto &value : entry.manifest.contributes.value("themes").toList())
        if (value.toMap().value("id") == local)
            theme = value.toMap();
    if (theme.isEmpty())
        if (const auto run = m_runs.value(owner))
            theme = run->registrations.descriptor("themes", local);
    if (theme.isEmpty())
        return failure("Theme is unavailable for export");
    if (auto manifest = readManifest(entry.directory); !manifest)
        return manifest.error();
    if (auto valid = themes::validate(theme, entry.directory); !valid)
        return valid.error();
    return ThemePackage{theme, entry.directory, entry.manifest.raw};
}
QVariantMap PluginManager::publicEntry(const QString &id) const {
    const auto &entry = m_entries[id];
    const auto run = m_runs.value(id);
    return {{"id", id},
            {"name", entry.manifest.name},
            {"version", entry.manifest.version},
            {"description", entry.manifest.description},
            {"directory", entry.directory},
            {"development", entry.development},
            {"trusted", entry.trusted},
            {"enabled", entry.enabled},
            {"state", toString(run ? run->state : entry.state)},
            {"error", m_vault.redact(entry.error)},
            {"generation", run ? run->generation : 0},
            {"nativeModules", entry.manifest.nativeModules},
            {"type", entry.manifest.type},
            {"hasCode", entry.manifest.needsTrust()}};
}
QVariantMap PluginManager::snapshot() const {
    QVariantList extensions, commands, shuffle;
    for (const auto &id : m_order) {
        extensions.append(publicEntry(id));
        if (!running(id))
            continue;
        const auto run = m_runs.value(id);
        for (const auto &item : run->registrations.list("commands"))
            commands.append(item);
        for (const auto &item : run->registrations.list("shuffle"))
            shuffle.append(item);
    }
    QVariantMap ui;
    for (const auto &kind : RegistrationScope::uiKinds())
        if (kind != "themes")
            ui.insert(kind, contributions(kind));
    return {{"runtime", QVariantMap{{"catalogueReady", m_catalogueReady},
                                    {"startupComplete", m_startupComplete},
                                    {"ready", m_catalogueReady && !m_closing},
                                    {"safeMode", m_safeMode}, {"safeModeReason", m_safeModeReason}}},
            {"extensions", extensions}, {"contributions", ui}, {"themes", themes()},
            {"sources", QVariantMap{{"music", m_music.sources()}, {"lyrics", m_lyrics.sources()},
                                     {"shuffle", shuffle}}},
            {"commands", commands}};
}
} // namespace nekotune
