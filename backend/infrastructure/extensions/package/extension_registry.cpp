#include "infrastructure/extensions/package/extension_registry.h"
#include "infrastructure/extensions/package/extension_manifest.h"
#include "infrastructure/extensions/package/json_file.h"
#include <QSet>

namespace nekotune {
RegistryState ExtensionRegistry::load() const {
    RegistryState state;
    // Unknown keys (bundledSeen, ...) are kept so switching runtimes does not lose state.
    auto stored = readJsonFile(m_path, QVariantMap{}).toMap();
    state.selections = stored.take("selections").toMap();
    state.developmentDirectories = stored.take("developmentDirectories").toStringList();
    QSet<QString> ids;
    for (const auto &value : stored.take("entries").toList()) {
        const auto item = value.toMap();
        const auto id = item.value("id").toString();
        if (!validExtensionId(id) || ids.contains(id))
            continue;
        ids.insert(id);
        ExtensionRecord record;
        record.id = id;
        record.directory = item.value("directory").toString();
        record.development = item.value("development").toBool();
        record.enabled = item.value("enabled").toBool();
        record.trusted = item.value("trusted").toBool();
        const auto previous = item.value("previous").toMap();
        if (!previous.isEmpty())
            record.previous = PreviousPackage{previous.value("directory").toString(),
                                              previous.value("development").toBool(),
                                              previous.value("enabled").toBool(),
                                              previous.value("trusted").toBool()};
        record.failure = item.value("failure").toString();
        state.entries.append(std::move(record));
    }
    state.extra = std::move(stored);
    return state;
}
Result<void> ExtensionRegistry::save(const RegistryState &state) const {
    QVariantList entries;
    for (const auto &record : state.entries) {
        QVariantMap stored{{"id", record.id},
                           {"directory", record.directory},
                           {"development", record.development},
                           {"enabled", record.enabled},
                           {"trusted", record.trusted}};
        if (record.previous)
            stored.insert("previous", QVariantMap{{"directory", record.previous->directory},
                                                   {"development", record.previous->development},
                                                   {"enabled", record.previous->enabled},
                                                   {"trusted", record.previous->trusted}});
        if (!record.failure.isEmpty())
            stored.insert("failure", record.failure);
        entries.append(stored);
    }
    auto stored = state.extra;
    stored.insert("entries", entries);
    stored.insert("developmentDirectories", state.developmentDirectories);
    return writeJsonFile(m_path, stored);
}
std::optional<QStringList> ExtensionRegistry::storedDirectories() const {
    const auto stored = readJsonFile(m_path, QVariant()).toMap();
    if (!stored.value("entries").canConvert<QVariantList>())
        return std::nullopt;
    QStringList directories = stored.value("developmentDirectories").toStringList();
    for (const auto &value : stored.value("entries").toList())
        directories << value.toMap().value("directory").toString()
                    << value.toMap().value("previous").toMap().value("directory").toString();
    return directories;
}
} // namespace nekotune
