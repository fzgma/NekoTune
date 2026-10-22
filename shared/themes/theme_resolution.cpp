#include "themes/theme_resolution.h"

namespace nekotune::themes {
QVariantMap baseTokens(const QVariantMap &catalog, bool dark) {
    const auto rules = catalog.value("tokens").toMap();
    QVariantMap result;
    for (auto it = rules.cbegin(); it != rules.cend(); ++it)
        result.insert(it.key(), it.value().toMap().value("default"));
    if (!dark) {
        const auto light = catalog.value("light").toMap();
        for (auto it = light.cbegin(); it != light.cend(); ++it)
            if (rules.contains(it.key()))
                result.insert(it.key(), it.value());
    }
    return result;
}

QVariantMap resolveThemeTokens(const QVariantMap &catalog, const QVariantMap &theme, bool dark) {
    auto result = baseTokens(catalog, dark);
    // Legacy token-only themes describe a dark palette; light mode keeps the built-in light values.
    if (!dark && !theme.contains("schemaVersion"))
        return result;
    const auto rules = catalog.value("tokens").toMap();
    const auto common = theme.value("tokens").toMap();
    const auto variant =
        theme.value("variants").toMap().value(dark ? "dark" : "light").toMap().value("tokens").toMap();
    for (const auto &values : {common, variant})
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            if (rules.contains(it.key()))
                result.insert(it.key(), it.value());
    if (!common.contains("bgCard") && !variant.contains("bgCard"))
        result.insert("bgCard", result.value("bgSurface"));
    return result;
}
} // namespace nekotune::themes
