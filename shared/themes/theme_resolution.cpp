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

QVariantMap resolveThemeTokens(const QVariantMap &catalog, const QVariantMap &theme, bool dark,
                               const QVariantMap &layout) {
    auto result = baseTokens(catalog, dark);
    const auto rules = catalog.value("tokens").toMap();
    const QString mode = dark ? QStringLiteral("dark") : QStringLiteral("light");
    auto merge = [&](const QVariantMap &source, const auto &accept) {
        const auto common = source.value("tokens").toMap();
        const auto variant = source.value("variants").toMap().value(mode).toMap().value("tokens").toMap();
        for (const auto &values : {common, variant})
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                if (rules.contains(it.key()) && accept(rules.value(it.key()).toMap().value("group").toString()))
                    result.insert(it.key(), it.value());
        return common.contains("bgCard") || variant.contains("bgCard");
    };
    // Legacy token-only themes describe a dark palette; light mode keeps the built-in light values.
    if (dark || theme.contains("schemaVersion")) {
        // Host tokens (reducedMotion) are user preferences and never come from a theme.
        const bool hasCard = merge(theme, [&](const QString &group) {
            return group != QLatin1String("host") && (layout.isEmpty() || group != QLatin1String("layout"));
        });
        if (!hasCard)
            result.insert("bgCard", result.value("bgSurface"));
    }
    if (!layout.isEmpty())
        merge(layout, [](const QString &group) {
            return group == QLatin1String("layout") || group == QLatin1String("shared");
        });
    return result;
}
bool providesLayout(const QVariantMap &theme) {
    const auto ui = theme.value("ui").toMap();
    for (const auto *key : {"shell", "bottomPlayer", "background"})
        if (!ui.value(QLatin1String(key)).toString().isEmpty())
            return true;
    return !ui.value("pages").toMap().isEmpty();
}

bool providesWallpaper(const QVariantMap &theme) {
    return !theme.value("ui").toMap().value("wallpaper").toString().isEmpty();
}
} // namespace nekotune::themes
