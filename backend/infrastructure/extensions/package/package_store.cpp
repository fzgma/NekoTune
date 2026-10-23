#include "infrastructure/extensions/package/package_store.h"
#include "app_paths.h"
#include "infrastructure/extensions/package/extension_manifest.h"
#include "infrastructure/extensions/package/semver.h"
#include "infrastructure/extensions/package/theme_package.h"
#include "infrastructure/extensions/package/zip_archive.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <utility>

namespace nekotune {
QString PackageStore::root() const { return AppPaths::extensionsDirectory(); }
QString PackageStore::registryPath() const { return QDir(root()).filePath("registry.json"); }
QString PackageStore::packagesDirectory() const { return QDir(root()).filePath("packages"); }
QString PackageStore::dataDirectory(const QString &id) const { return QDir(root()).filePath("data/" + id); }
QString PackageStore::runtimeDirectory() const { return QDir(root()).filePath("runtime"); }

Result<QString> PackageStore::developmentDirectory(const QString &path) const {
    const auto directory = QFileInfo(path).canonicalFilePath();
    if (directory.isEmpty() || !QFileInfo(directory).isDir())
        return failure("Extension directory not found");
    return directory;
}
Result<QString> PackageStore::unpack(const QString &archive, bool themeOnly) const {
    // Unpack into a staging directory first; only a validated package gets its final name.
    const QDir packages(packagesDirectory());
    packages.mkpath(".");
    const auto stage = packages.filePath(".staging-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    zip::Limits limits;
    if (themeOnly) {
        limits.archiveBytes = themes::archiveLimit;
        limits.unpackedBytes = themes::unpackedLimit;
    }
    const auto extracted = zip::extract(archive, stage, limits);
    const auto staged = extracted ? readManifest(stage) : Result<ExtensionManifest>(extracted.error());
    if (!staged) {
        QDir(stage).removeRecursively();
        return staged.error();
    }
    if (staged.value().isTheme() && QFileInfo(archive).size() > themes::archiveLimit) {
        QDir(stage).removeRecursively();
        return failure("Theme archive exceeds 20 MiB");
    }
    const auto directory = packages.filePath(QStringLiteral("%1-%2-%3").arg(staged.value().id, staged.value().version,
                                                                 QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().rename(stage, directory)) {
        QDir(stage).removeRecursively();
        return failure("Cannot move extension package into place", ErrorCode::Io);
    }
    return directory;
}
QStringList PackageStore::clean(const QStringList &references) const {
    const QDir packages(packagesDirectory());
    const QFileInfo rootInfo(packages.path());
    if (!rootInfo.exists())
        return {};
    if (rootInfo.isSymLink() || !rootInfo.isDir())
        return {"Extension package cleanup skipped: packages is not a real directory"};
    QStringList messages;
    QSet<QString> retained;
    for (const auto &reference : std::as_const(references))
        if (!reference.isEmpty()) {
            // Keep both spellings: a development directory may point into a package via a link.
            retained.insert(QDir::cleanPath(QFileInfo(reference).absoluteFilePath()));
            if (const auto canonical = QFileInfo(reference).canonicalFilePath(); !canonical.isEmpty())
                retained.insert(canonical);
        }
    static const QString uuid = "[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}";
    static const QRegularExpression staging("^\\.staging-" + uuid + "$");
    static const QRegularExpression package("^([a-z][a-z0-9._-]{1,100})-(v?\\d+\\.\\d+\\.\\d+[^/]*)-" + uuid + "$");
    const auto base = rootInfo.canonicalFilePath();
    for (const auto &item : QDir(base).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const auto match = package.match(item.fileName());
        if (!staging.match(item.fileName()).hasMatch() &&
            !(match.hasMatch() && semver::parse(match.captured(2).startsWith('v') ? match.captured(2).mid(1) : match.captured(2))))
            continue;
        const auto target = base + "/" + item.fileName();
        bool keep = item.isSymLink();
        for (const auto &reference : std::as_const(retained))
            keep = keep || reference == target || reference.startsWith(target + "/") || target.startsWith(reference + "/");
        if (keep)
            continue;
        if (!QDir(target).removeRecursively())
            messages.append("Cannot clean extension package " + target);
    }
    return messages;
}
} // namespace nekotune
