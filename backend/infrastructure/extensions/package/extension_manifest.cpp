#include "infrastructure/extensions/package/extension_manifest.h"
#include "infrastructure/extensions/package/semver.h"
#include "infrastructure/extensions/package/theme_package.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSysInfo>

namespace nekotune {
namespace {
const QStringList uiKinds{"pages", "settings", "slots", "themes", "menus", "toolbars"};
const QStringList sourceKinds{"pages", "settings", "slots", "toolbars"};
} // namespace

bool validExtensionId(const QString &id) {
    static const QRegularExpression pattern("^[a-z][a-z0-9._-]{1,100}$");
    return pattern.match(id).hasMatch();
}
QString extensionPlatform() {
    // Linux and Windows are the only supported hosts; QSysInfo reports x86_64 on both.
#if defined(Q_OS_WIN)
    const QString system = "windows";
#else
    const QString system = "linux";
#endif
    return system + "-" + QSysInfo::currentCpuArchitecture();
}
Result<QString> containedFile(const QString &directory, const QString &relative) {
    if (relative.isEmpty() || QDir::isAbsolutePath(relative))
        return failure("Entry must be a relative file");
    const auto root = QFileInfo(directory).canonicalFilePath();
    const QFileInfo target(QDir(root).filePath(relative));
    const auto file = target.canonicalFilePath();
    if (root.isEmpty() || file.isEmpty() || !file.startsWith(root + "/") || !QFileInfo(file).isFile())
        return failure("Entry escapes extension directory");
    return file;
}
Result<ExtensionManifest> readManifest(const QString &directory) {
    QFile file(QDir(directory).filePath("extension.json"));
    if (!file.open(QIODevice::ReadOnly))
        return failure("Cannot read extension.json: " + file.errorString(), ErrorCode::Io);
    if (file.size() > 256 * 1024)
        return failure("Manifest is too large");
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (!document.isObject())
        return failure("Invalid extension.json: " + error.errorString());
    const auto data = document.object().toVariantMap();
    ExtensionManifest manifest;
    const int apiVersion = data.value("apiVersion").toInt();
    if (!validExtensionId(data.value("id").toString()) || !semver::parse(data.value("version").toString()) ||
        data.value("name").typeId() != QMetaType::QString || (apiVersion != 1 && apiVersion != 2))
        return failure("Invalid extension identity, version or apiVersion (expected 2)");
    if (data.value("id").toString() == "builtin")
        return failure("Reserved extension ID");
    if (data.contains("main"))
        return failure("Node.js extensions are no longer supported; update the extension to apiVersion 2");
    const bool hasLibrary = data.contains("library");
    if (hasLibrary && apiVersion != 2)
        return failure("Native libraries require apiVersion 2");
    if (!hasLibrary && !data.contains("contributes"))
        return failure("Extension needs a library or UI contributions");
    if (hasLibrary) {
        const auto libraries = data.value("library");
        if (libraries.typeId() != QMetaType::QVariantMap)
            return failure("library must map platforms to relative paths");
        const auto relative = libraries.toMap().value(extensionPlatform()).toString();
        const auto qt = data.value("qt").toString();
        const auto host = QStringLiteral("%1.%2").arg(QT_VERSION_MAJOR).arg(QT_VERSION_MINOR);
        if (relative.isEmpty())
            manifest.unavailable = "No library for platform " + extensionPlatform();
        else if (qt != host)
            manifest.unavailable = QStringLiteral("Built for Qt %1; this host uses Qt %2").arg(qt, host);
        else {
            auto path = containedFile(directory, relative);
            if (!path)
                return path.error();
            manifest.library = path.value();
        }
    }
    const auto dependencies = data.value("dependencies");
    if (dependencies.isValid() && dependencies.typeId() != QMetaType::QVariantMap)
        return failure("dependencies must be an object");
    const auto dependencyMap = dependencies.toMap();
    for (auto it = dependencyMap.cbegin(); it != dependencyMap.cend(); ++it) {
        if (!validExtensionId(it.key()) || !semver::validRange(it.value().toString()))
            return failure("Invalid dependency: " + it.key());
        manifest.dependencies.insert(it.key(), it.value().toString());
    }
    const auto contributes = data.value("contributes").toMap();
    for (const auto &kind : uiKinds) {
        if (!contributes.contains(kind))
            continue;
        if (contributes.value(kind).typeId() != QMetaType::QVariantList)
            return failure(QStringLiteral("contributes.%1 must be an array").arg(kind));
        QSet<QString> ids;
        for (const auto &value : contributes.value(kind).toList()) {
            const auto item = value.toMap();
            const auto id = item.value("id").toString();
            if (!validExtensionId(id) || ids.contains(id))
                return failure(QStringLiteral("Invalid/duplicate contribution: %1/%2").arg(kind, id));
            ids.insert(id);
            if (sourceKinds.contains(kind))
                if (auto source = containedFile(directory, item.value("source").toString()); !source)
                    return source.error();
        }
    }
    for (const auto &theme : contributes.value("themes").toList())
        if (auto valid = themes::validate(theme.toMap(), directory, data.value("type").toString() == "theme"); !valid)
            return valid.error();
    if (data.value("type").toString() == "theme") {
        const auto info = themes::inspectPackage(directory, data);
        if (!info)
            return info.error();
        manifest.themeHasCode = info.value().hasCode;
    }
    manifest.id = data.value("id").toString();
    manifest.name = data.value("name").toString();
    manifest.version = data.value("version").toString();
    manifest.description = data.value("description").toString();
    manifest.type = data.value("type", "extension").toString();
    manifest.apiVersion = apiVersion;
    manifest.nativeModules = data.value("nativeModules").toBool();
    manifest.defaultConfig = data.value("config").toMap();
    manifest.contributes = contributes;
    manifest.raw = data;
    return manifest;
}
} // namespace nekotune
