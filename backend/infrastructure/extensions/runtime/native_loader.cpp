#include "infrastructure/extensions/runtime/native_loader.h"
#include "infrastructure/extensions/package/package_store.h"
#include <nekotune/sdk/extension_plugin.h>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QUuid>
#include <utility>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace nekotune {
Result<QString> NativeLoader::loadablePath(const ExtensionManifest &manifest, bool development) const {
    if (!development)
        return manifest.library;
    // Never map a development build in place: rebuilding it would corrupt the running image (and
    // Windows locks loaded DLLs), and a path that is already loaded cannot be loaded again. The
    // whole library directory is staged so sibling dependencies still resolve ($ORIGIN on Linux,
    // the DLL directory on Windows). Each distinct build gets its own copy.
    const QFileInfo library(manifest.library);
    const QDir source = library.absoluteDir();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QStringList files;
    QDirIterator items(source.path(), QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (items.hasNext())
        files.append(source.relativeFilePath(items.next()));
    files.sort();
    if (files.size() > 256)
        return failure("Extension library directory has too many files");
    for (const auto &relative : std::as_const(files)) {
        QFile file(source.filePath(relative));
        if (!file.open(QIODevice::ReadOnly))
            return failure("Cannot read extension library: " + file.errorString(), ErrorCode::Io);
        hash.addData(relative.toUtf8());
        hash.addData(&file);
    }
    const QDir runtime(m_packages.runtimeDirectory());
    runtime.mkpath(".");
    const auto target = runtime.filePath(manifest.id + "-" + QString::fromLatin1(hash.result().toHex().left(16)));
    const auto staged = QDir(target).filePath(library.fileName());
    if (QFileInfo::exists(staged))
        return staged;
    const auto temporary = target + "." + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".tmp";
    bool copied = QDir().mkpath(temporary);
    for (const auto &relative : std::as_const(files)) {
        const auto destination = QDir(temporary).filePath(relative);
        copied = copied && QDir().mkpath(QFileInfo(destination).absolutePath()) &&
                 QFile::copy(source.filePath(relative), destination);
    }
    if (!copied || !QDir().rename(temporary, target)) {
        QDir(temporary).removeRecursively();
        return failure("Cannot stage development library", ErrorCode::Io);
    }
    return staged;
}
Result<QPluginLoader *> NativeLoader::loader(const QString &path) {
    auto *loader = m_loaders.value(path);
    if (!loader) {
        loader = new QPluginLoader(path, m_owner);
        m_loaders.insert(path, loader);
    }
    // metaData() reads the ELF/PE section without mapping the library or running its code.
    const auto iid = loader->metaData().value("IID").toString();
    if (iid != QStringLiteral(NEKOTUNE_EXTENSION_IID)) {
        return failure(iid.isEmpty() ? QStringLiteral("Library is not a Qt plugin: ") + loader->errorString()
                                     : QStringLiteral("Unsupported extension interface %1").arg(iid));
    }
    return loader;
}
Result<sdk::IExtensionPlugin *> NativeLoader::instance(QPluginLoader *loader, bool loaded) {
    auto *plugin = loaded ? qobject_cast<sdk::IExtensionPlugin *>(loader->instance()) : nullptr;
    if (!plugin)
        return failure(loaded ? QStringLiteral("Library does not implement IExtensionPlugin") : loader->errorString());
    return plugin;
}
NativeLoader::DllDirectoryScope::DllDirectoryScope(const QString &libraryPath) {
#ifdef Q_OS_WIN
    // Windows does not search a DLL's own directory for its dependencies. SetDllDirectory is
    // process-wide, so keep it only for this load on the backend thread.
    SetDllDirectoryW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(QFileInfo(libraryPath).absolutePath()).utf16()));
#else
    Q_UNUSED(libraryPath);
#endif
}
NativeLoader::DllDirectoryScope::~DllDirectoryScope() {
#ifdef Q_OS_WIN
    SetDllDirectoryW(nullptr);
#endif
}
} // namespace nekotune
