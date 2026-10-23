# Phase 2：包管理层

## 目标

1. 把清单、SemVer、ZIP、主题包这些“只处理文件和元数据”的代码移到 `backend/infrastructure/extensions/package/`。
2. `ExtensionManifest` 改为强类型字段，不再让调用方从 `data` 字典取值。
3. 新建 `ExtensionRegistry` 负责 `registry.json` 的读写，新建 `PackageStore` 负责路径、解包与清理旧包。
4. `PluginManager` 改为调用它们。**行为、IPC、`registry.json` 格式全部不变**（README §2.1 第 4 条）。

## 前置条件

- Phase 1 已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `backend/infrastructure/extensions/{extension_manifest,semver,zip_archive,theme_package}.{h,cpp}` | `git mv` 到 `package/` |
| `backend/infrastructure/extensions/package/json_file.{h,cpp}` | 新建：`readJsonFile`、`writeJsonFile` |
| `backend/infrastructure/extensions/package/extension_manifest.{h,cpp}` | 强类型字段 |
| `backend/infrastructure/extensions/package/extension_registry.{h,cpp}` | 新建 |
| `backend/infrastructure/extensions/package/package_store.{h,cpp}` | 新建 |
| `backend/infrastructure/extensions/plugin_manager.{h,cpp}` | 改用以上类型；删除 `readJson`、`writeJson`、`root()`、`dataDirectory()` 的实现细节 |
| `backend/CMakeLists.txt` | 更新源文件路径，加入新文件 |
| `tests/native_extensions_test.cpp`、`tests/extensions_test.cpp` | 更新 include 路径；新增 2 个测试，扩充 `manifestValidation` |

## 步骤

### 2.1 移动文件

```sh
mkdir -p backend/infrastructure/extensions/package
for name in extension_manifest semver zip_archive theme_package; do
    git mv backend/infrastructure/extensions/$name.h backend/infrastructure/extensions/package/$name.h
    git mv backend/infrastructure/extensions/$name.cpp backend/infrastructure/extensions/package/$name.cpp
done
```

把所有 `#include "infrastructure/extensions/<name>.h"`（`<name>` 为以上四个）改为 `#include "infrastructure/extensions/package/<name>.h"`。用下面的命令确认全部改完：

```sh
grep -rnE '#include "infrastructure/extensions/(extension_manifest|semver|zip_archive|theme_package)\.h"' backend tests frontend shared
# 应无输出
```

`backend/CMakeLists.txt` 中对应的 4 个源文件路径改为 `infrastructure/extensions/package/*.cpp`。

### 2.2 `json_file`

`plugin_manager.cpp` 匿名命名空间里的 `readJson`、`writeJson` 移到新文件，函数体**原样搬过去**，只改名字：

文件：`backend/infrastructure/extensions/package/json_file.h`

```cpp
#pragma once
#include "domain/result.h"
#include <QVariant>

namespace nekotune {
/// fallback when the file is missing or is not valid JSON.
QVariant readJsonFile(const QString &path, const QVariant &fallback);
/// Atomic owner-only write; creates the parent directory.
Result<void> writeJsonFile(const QString &path, const QVariant &value);
} // namespace nekotune
```

`json_file.cpp` 包含原 `readJson`、`writeJson` 的实现。`plugin_manager.cpp` 删除这两个函数，所有调用改为新名字。

### 2.3 强类型清单

文件：`backend/infrastructure/extensions/package/extension_manifest.h`，`struct ExtensionManifest` 整体替换为：

```cpp
/// Validated extension.json.
struct ExtensionManifest {
    QString id;
    QString name;
    QString version;
    QString description;
    /// "extension" unless the manifest declares another type; "theme" marks theme packages.
    QString type = "extension";
    int apiVersion = 0;
    bool nativeModules = false;
    QVariantMap defaultConfig;
    /// Extension ID → SemVer range.
    QMap<QString, QString> dependencies;
    /// Validated contributions keyed by kind; items keep their local IDs.
    QVariantMap contributes;
    /// The original JSON, only for re-exporting theme packages.
    QVariantMap raw;
    /// Theme packages that ship QML/JavaScript still need explicit trust.
    bool themeHasCode = false;
    /// Absolute library path for this platform; empty for UI-only and theme packages.
    QString library;
    /// Why the package cannot run here (missing platform build, Qt mismatch); empty when runnable.
    QString unavailable;

    bool isTheme() const { return type == "theme"; }
    bool needsTrust() const { return !isTheme() || themeHasCode; }
    // Theme packages have no backend code; their QML is gated by the frontend until trusted.
    bool needsTrustToRun() const { return !isTheme(); }
    /// Stand-in for an installed package whose manifest can no longer be read.
    static ExtensionManifest placeholder(const QString &id) {
        ExtensionManifest manifest;
        manifest.id = manifest.name = id;
        manifest.version = "0.0.0";
        return manifest;
    }
};
```

文件头补充 `#include <QMap>`。

文件：`extension_manifest.cpp` 的 `readManifest()`：

1. 校验逻辑与顺序**完全不变**，仍先把 JSON 读成 `const auto data = document.object().toVariantMap();`，校验都基于 `data`。
2. 把原来的 `ExtensionManifest manifest{document.object().toVariantMap()};` 改为先声明 `ExtensionManifest manifest;`，在校验通过的位置依次填充：
   - `id`、`name`、`version` 来自 `data`；`description = data.value("description").toString()`；`type = data.value("type", "extension").toString()`；`apiVersion`；`nativeModules = data.value("nativeModules").toBool()`；`defaultConfig = data.value("config").toMap()`；`raw = data`。
   - `dependencies`：在原来逐项校验依赖的循环里，`manifest.dependencies.insert(it.key(), it.value().toString())`。
   - `contributes = data.value("contributes").toMap()`。
3. 原来通过 `manifest.id()`、`manifest.version()`、`manifest.isTheme()`、`manifest.dependencies()`、`manifest.contributes()` 访问的地方改为直接读字段或读 `data`。注意 `themes::inspectPackage(directory, data)` 仍传原始 `data`。

`plugin_manager.cpp` 中按下表替换（逐个搜索，不要批量 sed）：

| 原写法 | 新写法 |
| --- | --- |
| `manifest.id()` / `.manifest.id()` | `manifest.id` |
| `manifest.version()` | `manifest.version` |
| `manifest.dependencies()` | `manifest.dependencies`（迭代时 `it.value()` 已是 `QString`，去掉 `.toString()`） |
| `manifest.contributes()` | `manifest.contributes` |
| `manifest.data.value("name")` | `manifest.name` |
| `manifest.data.value("version")` | `manifest.version` |
| `manifest.data.value("description", QString())` | `manifest.description` |
| `manifest.data.value("nativeModules").toBool()` | `manifest.nativeModules` |
| `manifest.data.value("type", "extension")` | `manifest.type` |
| `manifest.data.value("config", QVariantMap{})` | `manifest.defaultConfig` |
| `entry.manifest.data`（传给 `themes::exportTheme`） | `entry.manifest.raw` |
| `entry.manifest.data = {{"id", id}, {"name", id}, {"version", "0.0.0"}};`（`load()` 中） | `entry.manifest = ExtensionManifest::placeholder(id);` |

替换后执行，应无输出：

```sh
grep -n 'manifest.data\|manifest().\|\.dependencies()\|\.contributes()\|\.id()\b' backend/infrastructure/extensions/plugin_manager.cpp | grep -v 'context\|run->id\|m_id'
```

（`Context::id()` 是 SDK 方法，不属于本次替换。如果过滤后仍有与清单相关的行，逐一处理。）

### 2.4 `ExtensionRegistry`

文件：`backend/infrastructure/extensions/package/extension_registry.h`

```cpp
#pragma once
#include "domain/result.h"
#include <QStringList>
#include <QVariantMap>
#include <optional>

namespace nekotune {
/// The package an update replaced; kept until the new version activates.
struct PreviousPackage {
    QString directory;
    bool development = false;
    bool enabled = false;
    bool trusted = false;
};
/// Persistent part of one installed extension.
struct ExtensionRecord {
    QString id;
    QString directory;
    bool development = false;
    bool enabled = false;
    bool trusted = false;
    std::optional<PreviousPackage> previous;
    /// Last failure, already redacted; empty when the extension did not fail.
    QString failure;
};
struct RegistryState {
    QList<ExtensionRecord> entries;
    QVariantMap selections;
    QStringList developmentDirectories;
    /// Unknown keys (bundledSeen, ...) are kept so switching runtimes does not lose state.
    QVariantMap extra;
};

/// config/extensions/registry.json.
class ExtensionRegistry {
  public:
    explicit ExtensionRegistry(QString path) : m_path(std::move(path)) {}
    const QString &path() const { return m_path; }
    /// Skips invalid and duplicate IDs; a missing or unreadable file yields an empty state.
    RegistryState load() const;
    Result<void> save(const RegistryState &state) const;
    /// Every package directory the file on disk references (current and previous);
    /// nullopt when the file cannot be read, so cleanup keeps everything.
    std::optional<QStringList> storedDirectories() const;

  private:
    QString m_path;
};
} // namespace nekotune
```

实现要求（`extension_registry.cpp`）：

- `load()`：逻辑来自 `PluginManager::load()` 的前半部分——`readJsonFile(m_path, QVariantMap{})`，`take("selections")`、`take("developmentDirectories")`、`take("entries")`，余下的放进 `extra`。每个条目用 `validExtensionId` 过滤并去重；`previous` 存在且为非空对象时读成 `PreviousPackage`；`failure` 读入 `failure`。
- `save()`：逻辑来自 `PluginManager::save()` 的写文件部分。每个条目写 `id,directory,development,enabled,trusted`，`previous` 有值时写 `{directory,development,enabled,trusted}`，`failure` 非空时写 `failure`。顶层写 `extra` 的全部键，再写 `entries`、`selections`、`developmentDirectories`。用 `writeJsonFile`。
- `storedDirectories()`：逻辑来自 `cleanUnusedPackages()` 中读取 `registry.json` 的部分。`entries` 不能转换为列表时返回 `std::nullopt`；否则返回 `developmentDirectories` 加上每个条目的 `directory` 和 `previous.directory`。

`PluginManager` 改动：

1. `Entry` 改为继承 `ExtensionRecord`：

```cpp
    struct Entry : ExtensionRecord {
        ExtensionManifest manifest;
        QString state = "disabled";
        QString error;
    };
```

   删除 `Entry` 中原有的 `directory`、`development`、`enabled`、`trusted`、`previous` 成员。
2. `entry.previous` 由 `QVariantMap` 变为 `std::optional<PreviousPackage>`，按下表替换：

| 原写法 | 新写法 |
| --- | --- |
| `candidate.previous.isEmpty()` | `!candidate.previous` |
| `candidate.previous.value("directory").toString()` | `candidate.previous->directory` |
| `candidate.previous.value("development").toBool()` | `candidate.previous->development` |
| `candidate.previous.clear()` | `candidate.previous.reset()` |
| `install()` 中 `!old.previous.isEmpty() ? old.previous : QVariantMap{{"directory", old.directory}, ...}` | `old.previous ? *old.previous : PreviousPackage{old.directory, old.development, false, old.trusted}` |
| `entry.previous.value("directory").toString()`（`cleanUnusedPackages`） | `entry.previous ? entry.previous->directory : QString()` |

3. 新增成员 `ExtensionRegistry m_registry;`，在构造函数初始化列表中用 `PackageStore` 给出的路径构造（见 2.5）。删除 `m_extraState`，改为 `QVariantMap m_registryExtra;`。
4. `PluginManager::load()` 改为：

```cpp
void PluginManager::load() {
    auto state = m_registry.load();
    m_registryExtra = std::move(state.extra);
    m_selections = std::move(state.selections);
    m_developmentDirectories = std::move(state.developmentDirectories);
    for (auto &record : state.entries) {
        Entry entry;
        static_cast<ExtensionRecord &>(entry) = std::move(record);
        entry.error = entry.failure;
        entry.state = entry.error.isEmpty() ? "disabled" : "failed";
        auto manifest = readManifest(entry.directory);
        if (manifest && manifest.value().id == entry.id)
            entry.manifest = manifest.value();
        else {
            entry.manifest = ExtensionManifest::placeholder(entry.id);
            entry.state = "failed";
            entry.error = manifest ? QStringLiteral("Installed extension ID changed") : manifest.error().message;
        }
        const auto id = entry.id;
        m_entries.insert(id, std::move(entry));
        m_order.append(id);
    }
}
```

5. `PluginManager::save()`：保留把开发目录追加到 `m_developmentDirectories` 的两段逻辑（`previous` 部分改用 `entry.previous->development` / `->directory`），然后构造 `RegistryState`：每个条目复制 `ExtensionRecord` 部分，`failure` 设为 `entry.state == "failed" ? redact(entry.error) : QString()`；`extra = m_registryExtra`；最后 `return m_registry.save(state);`。
6. 所有新建 `Entry` 的地方（`install()` 中的 `Entry entry;`、`startChain()` 中的 `Entry restored;`）补上 `entry.id = id;` / `restored.id = id;`。

### 2.5 `PackageStore`

文件：`backend/infrastructure/extensions/package/package_store.h`

```cpp
#pragma once
#include "domain/result.h"
#include <QStringList>

namespace nekotune {
/// Directory layout below AppPaths::extensionsDirectory(): packages/, data/<id>/, runtime/ and
/// registry.json. Paths follow NEKOTUNE_HOME at call time.
class PackageStore {
  public:
    QString root() const;
    QString registryPath() const;
    QString packagesDirectory() const;
    QString dataDirectory(const QString &id) const;
    QString runtimeDirectory() const;
    /// Canonical path of a development directory.
    Result<QString> developmentDirectory(const QString &path) const;
    /// Unpack a ZIP through a staging directory into packages/<id>-<version>-<uuid>.
    /// themeOnly applies theme archive limits up front.
    Result<QString> unpack(const QString &archive, bool themeOnly) const;
    /// Remove staging and host-named package directories not covered by references; returns
    /// one message per directory that could not be handled.
    QStringList clean(const QStringList &references) const;
};
} // namespace nekotune
```

实现要求（`package_store.cpp`）：

- `root()` 返回 `AppPaths::extensionsDirectory()`；其余路径都基于 `root()`，与 `PluginManager::root()`、`dataDirectory()`、`loadablePath()` 中的 `runtime` 拼法一致。
- `developmentDirectory()`：来自 `install()` 中 `if (development) { ... }` 分支，错误文本不变（`Extension directory not found`）。
- `unpack()`：来自 `install()` 的 `else { ... }` 分支，从 `packages.mkpath(".")` 到 `QDir().rename(stage, directory)` 失败处理为止，包括主题包大小检查。错误文本不变。注意：判断“是否为主题包”的 `themeOnly` 参数由调用方计算（见下文），`unpack` 不再看文件名。
- `clean()`：来自 `cleanUnusedPackages()` 中从 `const QDir packages(...)` 开始的全部逻辑，**除了**读取 `registry.json`、收集 `references` 的部分。`packages` 不是真实目录时返回 `{"Extension package cleanup skipped: packages is not a real directory"}`；删除失败的条目返回 `"Cannot clean extension package " + target`。正则、链接处理、`retained` 判断都原样搬过去。

`PluginManager` 改动：

1. 新增成员 `PackageStore m_packages;`，声明在 `m_registry` **之前**；构造函数初始化 `m_registry(m_packages.registryPath())`。
2. `root()`、`dataDirectory()` 两个成员函数删除，调用处改为 `m_packages.root()`、`m_packages.dataDirectory(id)`。`loadablePath()` 中 `QDir(root()).filePath("runtime")` 改为 `m_packages.runtimeDirectory()`；`start()` 中删除 `runtime` 目录同理。
3. `install()` 开头改为：

```cpp
    const bool development = params.value("development").toBool();
    const bool themeOnly = params.value("themeOnly").toBool() || path.endsWith(".nktheme.zip");
    const auto unpacked = development ? m_packages.developmentDirectory(path) : m_packages.unpack(path, themeOnly);
    if (!unpacked) {
        done(unpacked.error());
        return;
    }
    const auto directory = unpacked.value();
```

   其后的 `// Re-read at the final location` 起的逻辑不变。
4. `cleanUnusedPackages()` 改为只收集引用，再调用 `m_packages.clean()`：

```cpp
void PluginManager::cleanUnusedPackages() {
    // References on disk also protect the last committed version if saving a mutation failed.
    const auto stored = m_registry.storedDirectories();
    if (!stored)
        return;
    QStringList references = m_developmentDirectories + *stored;
    for (const auto &directory : std::as_const(m_retainedDirectories))
        references.append(directory);
    for (const auto &run : std::as_const(m_runs))
        references.append(run->directory);
    for (const auto &entry : std::as_const(m_entries))
        references << entry.directory << (entry.previous ? entry.previous->directory : QString());
    for (const auto &message : m_packages.clean(references))
        reportError(message);
}
```

   原实现在 `packages` 目录不存在时直接返回，这一判断移进 `clean()`（返回空列表）。
5. 删除 `plugin_manager.cpp` 中不再使用的 include（`QRegularExpression`、`QUuid` 等以编译和 `grep` 结果为准，`QUuid` 若仍被使用则保留）。

### 2.6 CMake

`backend/CMakeLists.txt` 的 `nekotune_infrastructure` 加入：

```cmake
    infrastructure/extensions/package/extension_registry.cpp
    infrastructure/extensions/package/json_file.cpp
    infrastructure/extensions/package/package_store.cpp
```

与 2.1 移动的四个文件放在一起，按字母顺序。

### 2.7 测试

文件：`tests/native_extensions_test.cpp`

1. include 改为 `package/` 路径（2.1 已做），并加入：

```cpp
#include "infrastructure/extensions/package/extension_registry.h"
#include "infrastructure/extensions/package/package_store.h"
```

2. `manifestValidation()` 末尾追加，验证强类型字段：

```cpp
        write(R"({"id":"x.typed","name":"Typed","version":"1.2.3","apiVersion":2,"description":"D",)"
              R"("nativeModules":true,"config":{"a":1},"dependencies":{"x.base":"^1.0.0"},)"
              R"("contributes":{"menus":[{"id":"m","title":"M","context":"song","command":"c"}]}})");
        manifest = readManifest(directory.path());
        QVERIFY2(manifest, qPrintable(manifest ? QString() : manifest.error().message));
        QCOMPARE(manifest.value().id, "x.typed");
        QCOMPARE(manifest.value().description, "D");
        QCOMPARE(manifest.value().type, "extension");
        QVERIFY(manifest.value().nativeModules);
        QCOMPARE(manifest.value().defaultConfig.value("a").toInt(), 1);
        QCOMPARE(manifest.value().dependencies.value("x.base"), "^1.0.0");
        QCOMPARE(manifest.value().contributes.value("menus").toList().size(), 1);
        QCOMPARE(manifest.value().raw.value("name").toString(), "Typed");
```

3. 在 `semverRanges()` 之后新增：

```cpp
    void registryKeepsUnknownKeys() {
        const auto path = QDir(m_home->path()).filePath("registry.json");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"bundledSeen":["a"],"selections":{"theme":"x/y"},"developmentDirectories":["/dev/x"],)"
                   R"("entries":[{"id":"x.one","directory":"/p/one","enabled":true,"trusted":true,)"
                   R"("previous":{"directory":"/p/old","development":false,"enabled":false,"trusted":true}},)"
                   R"({"id":"x.one","directory":"/p/duplicate"},{"id":"Bad","directory":"/p/bad"},)"
                   R"({"id":"x.two","directory":"/p/two","failure":"boom"}]})");
        file.close();
        const ExtensionRegistry registry(path);
        auto state = registry.load();
        QCOMPARE(state.entries.size(), 2);
        QCOMPARE(state.entries[0].directory, "/p/one");
        QVERIFY(state.entries[0].previous);
        QCOMPARE(state.entries[0].previous->directory, "/p/old");
        QCOMPARE(state.entries[1].failure, "boom");
        QCOMPARE(state.extra.value("bundledSeen").toStringList(), QStringList{"a"});
        QVERIFY(registry.save(state));
        const auto reloaded = registry.load();
        QCOMPARE(reloaded.extra, state.extra);
        QCOMPARE(reloaded.selections, state.selections);
        QCOMPARE(reloaded.entries.size(), 2);
        QCOMPARE(*registry.storedDirectories(),
                 (QStringList{"/dev/x", "/p/one", "/p/old", "/p/two", QString()}));
    }
    void packageStoreCleansOnlyUnreferenced() {
        const PackageStore store;
        const QDir packages(store.packagesDirectory());
        const QString kept = "x.one-1.0.0-123e4567-e89b-42d3-a456-426614174000";
        const QString unused = "x.one-0.9.0-123e4567-e89b-42d3-a456-426614174001";
        const QString staging = ".staging-123e4567-e89b-42d3-a456-426614174002";
        for (const auto &name : {kept, unused, staging, QStringLiteral("not-a-package")})
            QVERIFY(packages.mkpath(name));
        QVERIFY(store.clean({packages.filePath(kept)}).isEmpty());
        QVERIFY(packages.exists(kept));
        QVERIFY(packages.exists("not-a-package"));
        QVERIFY(!packages.exists(unused));
        QVERIFY(!packages.exists(staging));
    }
```

   `storedDirectories()` 的期望顺序按 2.4 的实现规则：先 `developmentDirectories`，再按条目顺序依次加入 `directory` 和 `previous.directory`（没有 `previous` 时加入空字符串）。如果你的实现选择跳过空字符串，同步修改期望并在报告中说明；`clean()` 会忽略空引用。

文件：`tests/extensions_test.cpp`：只更新 `extension_manifest.h` 的 include 路径。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen ./build/tests/nekotune_native_extensions_test
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
wc -l backend/infrastructure/extensions/plugin_manager.cpp
```

## 完成标准

- [ ] 四个文件已移到 `package/`，旧路径 include 不存在。
- [ ] `plugin_manager.cpp` 中不再出现 `manifest.data`、`readJson(`、`writeJson(`、`m_extraState`。
- [ ] 新增与扩充的测试通过；已有测试断言未修改（include 路径除外）。
- [ ] 全量测试失败列表不比基线多。
- [ ] 记录 `plugin_manager.cpp` 行数。

## 提交

```sh
git add backend/infrastructure/extensions backend/CMakeLists.txt tests/native_extensions_test.cpp tests/extensions_test.cpp
git status --short   # 确认没有带入无关文件
git commit -m "refactor(extensions): 拆出包管理层并强类型化清单"
```
