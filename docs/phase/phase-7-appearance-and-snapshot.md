# Phase 7：外观服务与新的对外数据模型

## 目标

1. 外观（主题、界面、插槽替换）的选择、校验、导出从 `PluginManager` 移到 `AppearanceService`（R14）。
2. 新增 IPC：`appearance.get`、`appearance.select`、`appearance.export_theme` 与事件 `appearance.changed`；删除 `extensions.select`、`extensions.export_theme`。
3. 选择保存到 `config/extensions/appearance.json`，首次启动从 `registry.json` 的 `selections` 迁移。
4. `extensions.list` / `extensions.changed` 改为按用途组织的结构（R15，方案 §3.8），由 `ContributionRegistry` 统一生成贡献列表。
5. 前端 `ExtensionsController`、`ThemeController` 适配；QML 可见的属性名和界面行为不变。

## 前置条件

- Phase 6 已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `backend/domain/appearance/appearance_backend.h` | 新建：`IAppearanceBackend` |
| `backend/infrastructure/extensions/capabilities/contribution_registry.{h,cpp}` | 新建：贡献与主题目录 |
| `backend/infrastructure/appearance/appearance_service.{h,cpp}` | 新建 |
| `backend/infrastructure/extensions/plugin_manager.{h,cpp}` | 删除 `select`、`validateSelection`、`exportTheme`、`m_selections`、`publicEntry` 中的贡献拼装；新快照 |
| `backend/infrastructure/extensions/package/extension_registry.{h,cpp}` | `RegistryState::selections` 只读迁移，不再写回 |
| `backend/ipc/api/appearance_api.cpp` | 新建 |
| `backend/ipc/api/extensions_api.cpp` | 方法表删除 `extensions.select`、`extensions.export_theme` |
| `backend/ipc/api/api_context.{h,cpp}`、`backend/ipc/ipc_router.cpp`（或注册 API 的位置） | 注册 `registerAppearanceApi` |
| `backend/runtime/backend_session.{h,cpp}` | 组装 `AppearanceService`，转发事件 |
| `backend/infrastructure/extensions/host_gateway.cpp` | 已拒绝 `appearance.*`（Phase 1），确认无需改动 |
| `frontend/qt-qml/src/controllers/extensions_controller.{h,cpp}` | 适配新快照与 `appearance.*` |
| `frontend/qt-qml/src/controllers/theme_controller.cpp` | 主题列表来源、`appearance.select`、`appearance.export_theme` |
| `frontend/qt-qml/qml/components/ExtensionsPanel.qml` | 插槽选择改用 `appearance.select`（一行） |
| `backend/CMakeLists.txt` | 新文件；新目录加入合适目标 |
| `tests/native_extensions_test.cpp`、`tests/extensions_test.cpp`、`tests/wallpaper_test.cpp` | 按 7.8 调整 |

## 新协议

### `extensions.list` / `extensions.changed`

```jsonc
{
  "runtime": {
    "catalogueReady": true,
    "startupComplete": true,
    "ready": true,                 // 原 runtimeReady
    "safeMode": false,
    "safeModeReason": ""
  },
  "extensions": [
    {"id", "name", "version", "description", "directory", "development", "trusted", "enabled",
     "state", "error", "generation", "nativeModules", "type", "hasCode"}
  ],
  // 只含运行中扩展；id 为 "<extensionId>/<localId>"，带 localId、extensionId、generation，source 为绝对路径
  "contributions": {"pages": [], "settings": [], "slots": [], "menus": [], "toolbars": []},
  // 所有已安装扩展的主题（含未运行的），带 extensionId、localId、generation、available（扩展运行中）
  "themes": [],
  // 只含运行中扩展的注册
  "sources": {"music": [], "lyrics": [], "shuffle": []},
  "commands": []
}
```

- `pages` 中没有 `group` 的条目由后端补 `"primary"`（原来由前端 `ExtensionsController::pages()` 补）。
- `themes` 条目保留主题描述的全部字段，与原 `extension.themes` 一致。
- 删除字段：每个扩展条目上的 `contributes`、`registrations`、`themes`；顶层的 `selections`、`runtimeReady`、`catalogueReady`、`startupComplete`、`safeMode`（移到 `runtime`）。

### `appearance.*`

| 方法 | 参数 | data |
| --- | --- | --- |
| `appearance.get` | 无 | `{"selections": {插槽: 贡献 ID}}` |
| `appearance.select` | `{slot, contribution}` 或 `{selections: {...}}` | 规则与原 `extensions.select` 完全相同（全部校验通过才保存，未变化的项不重新校验，持久化失败不改变选择）；成功返回 `{"selections": {...}}` |
| `appearance.export_theme` | `{contribution, path}` | 与原 `extensions.export_theme` 相同：`{path, includesQml}` |

事件 `appearance.changed`：`{"event": "appearance.changed", "selections": {...}}`，在选择改变后发出。

## 步骤

### 7.1 `ContributionRegistry`

文件：`capabilities/contribution_registry.h`

```cpp
#pragma once
#include "domain/result.h"
#include <QVariantMap>

namespace nekotune {
/// Public view of manifest contributions and runtime UI registrations.
class ContributionCatalog {
  public:
    virtual ~ContributionCatalog() = default;
    /// Contributions of running extensions for one UI kind (pages, settings, slots, menus, toolbars).
    virtual QVariantList contributions(const QString &kind) const = 0;
    /// Every theme of every installed extension; "available" marks running ones.
    virtual QVariantList themes() const = 0;
    struct ThemePackage {
        QVariantMap theme;
        QString directory;
        QVariantMap manifest;
    };
    /// Theme descriptor plus the package needed to export it; fails when uninstalled.
    virtual Result<ThemePackage> themePackage(const QString &contributionId) const = 0;
};
} // namespace nekotune
```

`PluginManager` 实现 `ContributionCatalog`（私有继承，提供 `ContributionCatalog &contributions()` 访问器）。实现来自原 `publicEntry()` 中合并清单贡献与运行时注册、改写 `id`/`localId`/`extensionId`/`source` 的代码；`themePackage()` 来自原 `exportTheme()` 中查找主题与包目录的部分（校验 `readManifest` 与 `themes::validate` 也放在这里，错误文本不变）。

> 文件名保留 `contribution_registry`，与方案 §3.1 一致；类名 `ContributionCatalog` 表示它是只读接口。

### 7.2 `IAppearanceBackend` 与 `AppearanceService`

文件：`backend/domain/appearance/appearance_backend.h`

```cpp
#pragma once
#include "domain/result.h"
#include <QObject>
#include <QVariantMap>

namespace nekotune {
class IAppearanceBackend : public QObject {
    Q_OBJECT
  public:
    using Completion = std::function<void(Result<QVariantMap>)>;
    using QObject::QObject;
    virtual void request(const QString &method, const QVariantMap &params, Completion done) = 0;
    virtual QVariantMap selections() const = 0;
  signals:
    void eventReady(const QVariantMap &event);
};
} // namespace nekotune
```

加入 `nekotune_domain` 的头文件列表。

文件：`backend/infrastructure/appearance/appearance_service.h`

```cpp
#pragma once
#include "domain/appearance/appearance_backend.h"

namespace nekotune {
class ContributionCatalog;
/// Theme, layout and slot replacement selections, stored in config/extensions/appearance.json.
class AppearanceService final : public IAppearanceBackend {
    Q_OBJECT
  public:
    AppearanceService(ContributionCatalog &catalog, QString path, QObject *parent = nullptr);
    void request(const QString &method, const QVariantMap &params, Completion done) override;
    QVariantMap selections() const override { return m_selections; }
    /// One-time migration from registry.json; ignored when appearance.json already exists.
    Result<void> migrate(const QVariantMap &legacy);
    /// Extensions owning a selected contribution start first.
    QStringList selectedOwners() const;
    /// Called before a theme package is uninstalled: theme and themeLayout pointing at it reset.
    Result<void> forgetThemePackage(const QString &extensionId);

  private:
    Result<void> validate(const QString &slot, const QString &contribution) const;
    Result<void> save();
    ContributionCatalog &m_catalog;
    QString m_path;
    QVariantMap m_selections;
};
} // namespace nekotune
```

实现：

- 构造时 `readJsonFile(m_path, QVariantMap{})` 读入 `selections` 键。
- `request()`：`appearance.get` 返回 `{selections}`；`appearance.select` 来自原 `PluginManager::select()`（参数解析、错误文本、原子保存、未变化项跳过校验），成功后 `emit eventReady({{"event", "appearance.changed"}, {"selections", m_selections}})` 并返回 `{selections}`；`appearance.export_theme` 来自原 `exportTheme()`：内置主题直接导出，其余通过 `m_catalog.themePackage()` 取得后调用 `themes::exportTheme`。其他方法返回 `failure("Unknown appearance operation: " + method)`。
- `validate()` 来自原 `validateSelection()`，改为读取 `m_catalog.themes()`（只看 `available` 为真的条目）和 `m_catalog.contributions("slots")`。
- `migrate()`：`appearance.json` 已存在则直接返回成功；否则写入 `{selections: legacy}` 并更新内存。
- `forgetThemePackage()` 来自原 `uninstall()` 开头的主题选择重置逻辑。
- `save()` 写 `{selections}`。

### 7.3 `PluginManager`

1. 删除 `select()`、`validateSelection()`、`exportTheme()`、`m_selections` 及 `request()` 中 `extensions.select`、`extensions.export_theme` 分支。
2. 新增 `void setAppearance(AppearanceService *appearance);`，保存指针。
3. `load()`：`state.selections` 保存到临时成员 `m_legacySelections`，不再参与快照。
4. `start()`：`load()` 之后，若 `m_appearance` 非空且 `m_legacySelections` 非空，调用 `m_appearance->migrate(m_legacySelections)`；成功则清空 `m_legacySelections` 并 `save()`（`save()` 写出的 `RegistryState::selections` 为空，`ExtensionRegistry::save()` 不再写 `selections` 键）；失败则 `reportError("Cannot migrate appearance selections: ...")` 并保留在 `registry.json` 中等待下次启动重试。启动顺序中 `selectedOwners` 的计算改为 `m_appearance ? m_appearance->selectedOwners() : QStringList{}`。
5. `uninstall()` 开头的主题选择重置改为：

```cpp
    if (m_entries[id].manifest.isTheme() && m_appearance)
        if (auto forgotten = m_appearance->forgetThemePackage(id); !forgotten) {
            done(forgotten.error());
            return;
        }
```

6. `publicEntry()` 只返回扩展条目字段（见新协议）；`snapshot()` 改为生成新结构：`contributions` 来自 `contributions(kind)`（`pages` 补默认 `group`），`themes` 来自 `themes()`，`sources.music/lyrics` 来自 `m_music.sources()`、`m_lyrics.sources()`，`sources.shuffle` 来自运行扩展的 `shuffle` 注册描述，`commands` 来自运行扩展的 `commands` 注册描述（按扩展顺序、局部 ID 顺序）。
7. `ExtensionRegistry::save()` 删除写 `selections` 的语句；`RegistryState::selections` 注释改为 “Read only for migrating to appearance.json.”。

### 7.4 IPC 与组装

- 新建 `backend/ipc/api/appearance_api.cpp`，模式与 `extensions_api.cpp` 相同：为 `appearance.get`、`appearance.select`、`appearance.export_theme` 注册方法，转给 `api.appearance->request(...)`；`api.appearance` 为空时返回 `Appearance unavailable`。`ApiContext` 新增 `IAppearanceBackend *appearance = nullptr;`，在 API 注册入口调用 `registerAppearanceApi`（按 `registerExtensionsApi` 的注册位置照做）。
- `extensions_api.cpp` 方法表删除 `extensions.select`、`extensions.export_theme`。
- `BackendSession`：新增成员 `AppearanceService m_appearance{m_extensionBackend.contributions(), AppPaths::extensionsDirectory() + "/appearance.json"};`（声明在 `m_extensionBackend` 之后）；构造函数中 `m_extensionBackend.setAppearance(&m_appearance); m_api.appearance = &m_appearance;`；连接 `m_appearance.eventReady` 到 `m_server.broadcastEvent`。路径必须遵循 `NEKOTUNE_HOME`，若 `AppPaths::extensionsDirectory()` 在成员初始化时尚不可靠，改为在 `start()` 前构造并说明。

### 7.5 前端 `ExtensionsController`

- `apply()` 读取新结构：`m_items = extensions`，另存 `m_contributions`、`m_themes`、`m_sources`、`m_commands`、`m_runtime`。
- 选择来自 `appearance.*`：`refresh()` 同时请求 `appearance.get`；收到 `appearance.changed` 事件时更新 `m_selections` 并 `emit changed()`。连接断开时清空。
- `contributions(kind)` 改为返回 `m_contributions[kind]`（安全模式环境变量检查保留）；`kind == "themes"` 时返回 `m_themes` 中 `available` 为真的条目，保持 `ExtensionsPanel.qml` 的 `client.contributions("themes")` 与 `selected("theme")` 可用；`pages()` 不再补 `group`；`commands()` 返回 `m_commands`；`sources()` 返回 `m_sources["music"]`；删除私有 `registrations()`。
- 新增 C++ 访问器 `QVariantList themes() const { return m_themes; }`（不必暴露给 QML）。
- `appearanceReady()` 逻辑不变，读取 `m_runtime` 的 `catalogueReady`、`startupComplete`。
- `request()`：`extensions.export_theme` 的路径规范化改为 `appearance.export_theme`；`data.contains("extensions")` 时 `apply`；`data.contains("selections")` 时更新 `m_selections`。
- `resetInterface()`：改为一次 `appearance.select`，参数 `{selections: {每个已有插槽: ""}}`（原来逐个发送；一次性请求语义更安全，结果一致）。

### 7.6 前端 `ThemeController`

- `rebuild()` 中遍历 `m_extensions.items()` 取 `themes`/`contributes.themes` 的循环，改为遍历 `m_extensions.themes()`；`origin`、`available`、`extensionId`、`version`、`directory`、`generation`、`trusted`、`hasCode` 字段：主题条目自带 `extensionId`、`generation`、`available`，其余按 `extensionId` 在 `m_extensions.items()` 中查找扩展条目补齐（`origin` 仍按扩展 `type` 判断）。
- `applySelection()` 的 `request("extensions.select", ...)` 改为 `request("appearance.select", ...)`；导出的 `request("extensions.export_theme", ...)` 改为 `appearance.export_theme`。
- 其他调用 `extensions.enable/install/reload/uninstall` 的地方不变。

`frontend/qt-qml/qml/components/ExtensionsPanel.qml`：插槽下拉框的

```qml
                        onActivated: root.manage("select", {slot: selector.modelData, contribution: currentValue})
```

改为

```qml
                        onActivated: { root.localError = ""; root.client.request("appearance.select", {slot: selector.modelData, contribution: currentValue}) }
```

其余部分通过 `client.selections`、`client.contributions(...)` 工作，属性名未变，不修改。如果 QML 测试失败，先检查控制器实现。

### 7.7 CMake

- `nekotune_infrastructure` 加入 `infrastructure/appearance/appearance_service.cpp`、`infrastructure/extensions/capabilities/contribution_registry.cpp`。
- `nekotune_domain` 加入 `domain/appearance/appearance_backend.h`。
- `nekotune_ipc` 加入 `ipc/api/appearance_api.cpp`。

### 7.8 测试调整

本阶段允许修改协议相关断言，规则如下（期望的业务结果不变）：

| 原写法 | 新写法 |
| --- | --- |
| `entry(m, id).value("contributes").toMap().value(kind)` | 新辅助函数 `contributions(m, kind)` 中筛选 `extensionId == id` 的条目 |
| `entry(m, id).value("contributes").toMap() == QVariantMap{}`（未运行时无贡献） | `contributions(m, "pages")` 中没有该扩展的条目 |
| `entry(m, id).value("registrations").toMap().value("commands")` | `m.snapshot().value("commands")` 中筛选该扩展 |
| `entry(m, id).value("registrations").toMap().value("music")` | `m.snapshot().value("sources").toMap().value("music")` 中筛选该扩展 |
| `m.snapshot().value("selections")` | 测试中的 `AppearanceService` 的 `selections()` |
| `call(m, "extensions.select", ...)` / `call(m, "extensions.export_theme", ...)` | `callAppearance(appearance, "appearance.select" / "appearance.export_theme", ...)` |
| `snapshot().value("startupComplete")`（`startManager()` 中） | `snapshot().value("runtime").toMap().value("startupComplete")` |

`tests/native_extensions_test.cpp`：

- 新增辅助：`QVariantList contributions(const PluginManager &, const QString &kind)`；`Result<QVariantMap> callAppearance(AppearanceService &, const QString &method, const QVariantMap &params)`（与 `call()` 相同的等待方式）；`std::unique_ptr<AppearanceService> startAppearance(PluginManager &manager)`——**在 `manager->start()` 之前**创建并 `setAppearance`，因此需要把 `startManager()` 拆成“创建 + 设置外观 + 启动”，所有使用外观的测试调用新版本。
- `themeLayoutSelection`、`zipPackagesAndThemes` 等使用选择与导出的测试按上表替换。
- 新增 `appearanceMigratesFromRegistry`：启动前写入只含 `selections: {theme: "builtin/ocean"}` 的 `registry.json`，启动后断言 `appearance.json` 存在且选择为 `builtin/ocean`，`registry.json` 中不再有 `selections`。

`tests/extensions_test.cpp`：

- 构造假快照的代码（约第 140 行起）改为新结构：注册放进顶层 `sources.music`，贡献放进 `contributions`，主题放进 `themes`。
- `peer.call("extensions.select", ...)` 改为 `peer.call("appearance.select", ...)`。
- 预写 `registry.json` 中 `selections` 的用例（约第 782 行）保持不变，它们会经过迁移路径，验证迁移对前端透明。

`tests/wallpaper_test.cpp`：预写 `registry.json` 的 `selections` 保持不变（同上）。如有直接读取快照 `selections` 的断言，改为读取 `appearance.get`。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
grep -rn '"extensions.select"\|"extensions.export_theme"' backend frontend tests   # 应无输出
wc -l backend/infrastructure/extensions/plugin_manager.cpp   # 目标 ≤ 600
```

手动检查（`./build/nekotune`，独立 `NEKOTUNE_HOME`）：

1. 在设置 → 外观切换主题与界面，重启后选择保留。
2. 扩展管理页切换插槽替换，`Ctrl+Shift+E` 的「恢复默认界面」能清除全部替换。
3. 用旧的 `registry.json`（含 `selections`）启动，选择保持，文件被迁移。

## 完成标准

- [ ] `appearance.*` 方法与事件可用，`extensions.select`、`extensions.export_theme` 已删除。
- [ ] `extensions.list` 为新结构；前端属性名与界面行为不变。
- [ ] 迁移测试通过；旧 `registry.json` 的选择对前端透明。
- [ ] 全量测试失败列表不比基线多；手动检查结果已记录。
- [ ] `plugin_manager.cpp` 行数已记录。

## 提交

```sh
git add backend frontend tests
git status --short
git commit -m "refactor(extensions)!: 外观选择移入 AppearanceService 并重整扩展快照

BREAKING CHANGE: extensions.select、extensions.export_theme 改为 appearance.select、appearance.export_theme；extensions.list 改为按用途分组的结构。"
```
