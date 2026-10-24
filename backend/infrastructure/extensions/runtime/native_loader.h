#pragma once
#include "domain/result.h"
#include "infrastructure/extensions/package/extension_manifest.h"
#include <QHash>

class QObject;
class QPluginLoader;
namespace nekotune::sdk { class IExtensionPlugin; }

namespace nekotune {
class PackageStore;

/// Maps extension libraries. Libraries stay mapped until the process exits; loaders are cached by
/// path so re-enabling reuses the loaded instance.
class NativeLoader {
  public:
    NativeLoader(QObject *owner, const PackageStore &packages) : m_owner(owner), m_packages(packages) {}
    /// Installed libraries load in place; development builds are staged by content hash.
    Result<QString> loadablePath(const ExtensionManifest &manifest, bool development) const;
    /// Cached loader for path after checking the plugin IID from metadata, without running code.
    Result<QPluginLoader *> loader(const QString &path);
    /// The plugin instance once loader->load() returned loaded.
    static Result<sdk::IExtensionPlugin *> instance(QPluginLoader *loader, bool loaded);

    /// Windows does not search a DLL's own directory for its dependencies; SetDllDirectory is
    /// process-wide, so it is set only for the lifetime of this object. No-op elsewhere.
    class SearchPath {
      public:
        explicit SearchPath(const QString &libraryPath);
        ~SearchPath();
        SearchPath(const SearchPath &) = delete;
        SearchPath &operator=(const SearchPath &) = delete;
    };

  private:
    QObject *m_owner;
    const PackageStore &m_packages;
    QHash<QString, QPluginLoader *> m_loaders;
};
} // namespace nekotune
