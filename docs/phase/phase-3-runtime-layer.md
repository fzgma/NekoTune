# Phase 3：运行时层

## 目标

把“加载、启停、隔离故障”的通用部件从 `plugin_manager.cpp` 拆到 `backend/infrastructure/extensions/runtime/`：

| 部件 | 来源 |
| --- | --- |
| `ExtensionState` 枚举 | `Entry::state`、`Run::state` 的字符串 |
| `dependency_graph` | `PluginManager::dependencyOrder`、`dependents` |
| `crash_guard`（移动） | 原文件 |
| `call_guard` | `guarded()` 的异常处理、`pending()`、`PendingCall`、`finishCall`、超时常量 |
| `native_loader` | `loadablePath()`、`m_loaders`、IID 检查、Windows DLL 搜索路径 |
| `registration_scope` | `Run::registrations`、`addRegistration()` 的校验 |
| `extension_runtime.h` | `struct PluginManager::Run` |
| `host_context` | `PluginManager::Context`、`PluginManager::Handle` |
| `dev_watcher` | `watchDevelopment()` |

**行为、IPC、日志与错误文本全部不变**（README §2.1 第 4 条）。本阶段只搬代码、换类型，不新增功能。

## 前置条件

- Phase 2 已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `backend/infrastructure/extensions/runtime/extension_state.{h,cpp}` | 新建 |
| `backend/infrastructure/extensions/runtime/dependency_graph.{h,cpp}` | 新建 |
| `backend/infrastructure/extensions/runtime/crash_guard.{h,cpp}` | 从上级目录 `git mv` |
| `backend/infrastructure/extensions/runtime/call_guard.{h,cpp}` | 新建 |
| `backend/infrastructure/extensions/runtime/native_loader.{h,cpp}` | 新建 |
| `backend/infrastructure/extensions/runtime/registration_scope.{h,cpp}` | 新建 |
| `backend/infrastructure/extensions/runtime/extension_runtime.h` | 新建：`ExtensionRuntime`（原 `Run`） |
| `backend/infrastructure/extensions/runtime/host_context.{h,cpp}` | 新建：`HostContext`（原 `Context`）、`HostRegistration`（原 `Handle`） |
| `backend/infrastructure/extensions/runtime/dev_watcher.{h,cpp}` | 新建 |
| `backend/infrastructure/extensions/plugin_manager.{h,cpp}` | 删除被搬走的代码，改为调用新部件 |
| `backend/CMakeLists.txt` | 加入新文件，更新 `crash_guard.cpp` 路径 |
| `tests/native_extensions_test.cpp` | 更新 include；新增 3 个测试 |

## 执行顺序

按 3.1 → 3.9 顺序进行，**每完成一步就构建并运行 `nekotune_native_extensions_test`**，通过后再做下一步。某一步失败时只回退这一步，不要在失败状态下继续。

## 步骤

### 3.1 `ExtensionState`

文件：`runtime/extension_state.h`

```cpp
#pragma once
#include <QString>

namespace nekotune {
/// Runtime state of an installed extension; the strings are part of the extensions.list protocol.
enum class ExtensionState { Disabled, Blocked, Starting, Running, Stopping, Failed };
QString toString(ExtensionState state);
} // namespace nekotune
```

`extension_state.cpp` 用 `switch` 返回 `disabled`、`blocked`、`starting`、`running`、`stopping`、`failed`，不写 `default`，让编译器检查遗漏。

`PluginManager` 中：

- `Entry::state` 类型改为 `ExtensionState`，默认 `ExtensionState::Disabled`；`Run::state` 改为 `ExtensionState`，默认 `ExtensionState::Starting`。
- 所有 `"disabled"`、`"blocked"`、`"starting"`、`"running"`、`"stopping"`、`"failed"` 状态比较和赋值改为枚举。
- `publicEntry()` 的 `{"state", run ? run->state : entry.state}` 改为 `{"state", toString(run ? run->state : entry.state)}`。
- `HostContext` 中对 `run->state` 的比较同样改为枚举。

完成后检查，应无输出：

```sh
grep -nE '"(disabled|blocked|starting|running|stopping|failed)"' backend/infrastructure/extensions/plugin_manager.cpp
```

### 3.2 依赖图

文件：`runtime/dependency_graph.h`

```cpp
#pragma once
#include "domain/result.h"
#include <QHash>
#include <QMap>
#include <QStringList>

namespace nekotune {
struct DependencyNode {
    QString version;
    /// Extension ID → SemVer range.
    QMap<QString, QString> dependencies;
};
using DependencyNodes = QHash<QString, DependencyNode>;

/// id and everything it needs, dependencies first. Fails on missing extensions, unsatisfied
/// ranges and cycles.
Result<QStringList> dependencyOrder(const QString &id, const DependencyNodes &nodes);
/// id followed by every extension that depends on it, directly or transitively, in the
/// installation order given by order.
QStringList dependentsOf(const QString &id, const QStringList &order, const DependencyNodes &nodes);
} // namespace nekotune
```

实现：把 `PluginManager::dependencyOrder`、`dependents` 的函数体搬过去，`m_entries[x].manifest.dependencies` 换成 `nodes[x].dependencies`，`m_entries[x].manifest.version` 换成 `nodes[x].version`，`m_entries.contains` 换成 `nodes.contains`，`m_order` 换成参数 `order`。错误文本不变。

`PluginManager` 中：

- 新增私有函数 `DependencyNodes dependencyNodes() const;`，遍历 `m_entries` 生成节点。
- `dependencyOrder(id)` 与 `dependents(id)` 两个成员函数保留声明，函数体改为一行转发：`return nekotune::dependencyOrder(id, dependencyNodes());`、`return dependentsOf(id, m_order, dependencyNodes());`。这样调用处不需要改。

### 3.3 移动 `crash_guard`

```sh
mkdir -p backend/infrastructure/extensions/runtime
git mv backend/infrastructure/extensions/crash_guard.h backend/infrastructure/extensions/runtime/crash_guard.h
git mv backend/infrastructure/extensions/crash_guard.cpp backend/infrastructure/extensions/runtime/crash_guard.cpp
```

更新所有 include（`plugin_manager.cpp`、`crash_guard.cpp`、`tests/native_extensions_test.cpp`）和 CMake 路径。

### 3.4 `CallGuard`

文件：`runtime/call_guard.h`

```cpp
#pragma once
#include "domain/extensions/extension_backend.h"
#include "infrastructure/extensions/runtime/crash_guard.h"
#include <QPointer>
#include <optional>

namespace nekotune {
namespace timeouts {
constexpr int activation = 30'000;
constexpr int call = 30'000;
constexpr int download = 610'000;
constexpr int downloaded = 120'000;
} // namespace timeouts

/// Run extension code with crash attribution. Returns the error message when it threw.
template <class F> std::optional<QString> runAttributed(const QString &extensionId, F &&call) {
    crashguard::Scope scope(extensionId);
    try {
        call();
        return std::nullopt;
    } catch (const std::exception &exception) {
        return QStringLiteral("Unhandled exception: %1").arg(QString::fromLocal8Bit(exception.what()));
    } catch (...) {
        return QStringLiteral("Unhandled exception");
    }
}

/// Completions handed to one run's extension code. Each completes at most once, on the owner's
/// thread, fails after its timeout and fails when the run stops first.
class PendingCalls {
  public:
    using Completion = IExtensionBackend::Completion;
    Completion add(QObject *owner, Completion done, int timeoutMs);
    void failAll(const QString &message);

  private:
    struct Call;
    QList<std::shared_ptr<Call>> m_calls;
};
} // namespace nekotune
```

`call_guard.cpp`：

- `struct PendingCalls::Call` 即原 `PendingCall`；`finishCall` 成为匿名命名空间函数。
- `add()` 来自 `PluginManager::pending()`：先 `m_calls.removeIf(...)`，计时器父对象为 `owner`，返回的回调在 `owner` 线程完成（原代码用 `guard->post(...)`；这里用 `QMetaObject::invokeMethod(owner, ..., Qt::AutoConnection)` 等价实现：同线程直接执行，跨线程排队）。用 `QPointer<QObject>` 守护 `owner`。
- `failAll()` 来自 `stop()` 中的 `for (const auto &call : std::exchange(run->calls, {})) finishCall(call, failure(...))`。

`PluginManager` 中：

- 删除 `PendingCall`、`finishCall`、`activationTimeout`、`callTimeout`，改用 `timeouts::*`。`providerCall()` 中的 `610'000`、`120'000` 改为 `timeouts::download`、`timeouts::downloaded`。
- `guarded()` 改为：

```cpp
template <class F> bool PluginManager::guarded(const QString &id, F &&call) {
    const auto error = runAttributed(id, std::forward<F>(call));
    if (!error)
        return true;
    // Fault from the event loop; the caller may still be iterating the run's registrations.
    post([this, id, message = *error] { fault(id, message); });
    return false;
}
```

- `selectShuffle()` 中手写的 `crashguard::Scope` + `try/catch` 改为 `runAttributed`；异常消息保持原文 `Unhandled shuffle exception: %1` / `Unhandled shuffle exception`：

```cpp
    if (const auto error = runAttributed(owner, [&] { result = provider->select(request); }))
        result = failure(QString(*error).replace("Unhandled exception", "Unhandled shuffle exception"));
```

- `pending(run, done, timeout)` 改为 `run->calls.add(this, std::move(done), timeout)`；`Run::calls` 类型改为 `PendingCalls`；`stop()` 中改为 `run->calls.failAll("Extension stopped");`。删除 `PluginManager::pending` 声明。

### 3.5 `NativeLoader`

文件：`runtime/native_loader.h`

```cpp
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
```

`native_loader.cpp`：

- `loadablePath()`：`PluginManager::loadablePath()` 的函数体，参数换成 `manifest`、`development`，运行时目录用 `m_packages.runtimeDirectory()`。原注释（`Never map a development build in place ...`）一起搬。
- `loader()`：来自 `startOne()` 中从 `auto *loader = m_loaders.value(path.value());` 到 IID 检查失败返回为止。`QPluginLoader` 的父对象为 `m_owner`。错误文本不变（`Library is not a Qt plugin: ...`、`Unsupported extension interface %1`）。
- `instance()`：来自 `startOne()` 中 `qobject_cast` 及其失败分支。
- `SearchPath`：来自 `startOne()` 中 `#ifdef Q_OS_WIN` 的 `SetDllDirectoryW` 与 `qScopeGuard`。

`startOne()` 的对应片段改为：

```cpp
    const auto path = m_loader.loadablePath(entry.manifest, entry.development);
    if (!path) {
        activated(run, path.error());
        return;
    }
    const auto loader = m_loader.loader(path.value());
    if (!loader) {
        activated(run, loader.error());
        return;
    }
    // Once mapped, the package directory must survive until exit (Windows also locks the DLL).
    m_retainedDirectories.insert(entry.directory);
    bool loaded = false;
    {
        const NativeLoader::SearchPath searchPath(path.value());
        if (!guarded(id, [&] { loaded = loader.value()->load(); }))
            return;
    }
    const auto plugin = NativeLoader::instance(loader.value(), loaded);
    if (!plugin) {
        activated(run, plugin.error());
        return;
    }
```

后续使用 `plugin` 的地方改为 `plugin.value()`。`PluginManager` 新增成员 `NativeLoader m_loader{this, m_packages};`（声明在 `m_packages` 之后），删除 `m_loaders` 和 `loadablePath` 成员函数。`plugin_manager.cpp` 中不再需要 `<QPluginLoader>`、`<QCryptographicHash>`、`<QDirIterator>`、`qt_windows.h`，以编译结果为准删除。

### 3.6 `RegistrationScope`

文件：`runtime/registration_scope.h`

```cpp
#pragma once
#include "domain/result.h"
#include <QHash>
#include <QMap>
#include <QStringList>
#include <QVariantMap>

namespace nekotune {
/// Descriptors registered by one run, keyed by kind and local ID.
class RegistrationScope {
  public:
    static const QStringList &uiKinds();
    static const QStringList &kinds();
    /// Validates kind and ID, rejects IDs already contributed by the manifest or registered, keeps
    /// sources inside directory and validates themes; then records the descriptor with id,
    /// localId and extensionId filled in.
    Result<void> add(const QString &kind, const QString &localId, QVariantMap descriptor,
                     const QString &extensionId, const QString &directory, const QVariantMap &contributes);
    void remove(const QString &kind, const QString &localId);
    bool contains(const QString &kind, const QString &localId) const;
    QVariantMap descriptor(const QString &kind, const QString &localId) const;
    /// Descriptors of one kind ordered by local ID.
    QMap<QString, QVariantMap> list(const QString &kind) const;
    void clear();

  private:
    QHash<QString, QMap<QString, QVariantMap>> m_items;
};
} // namespace nekotune
```

- `uiKinds()`、`kinds()` 返回原 `uiKinds`、`registrationKinds` 常量；`sourceKinds` 留在 `.cpp` 内部。`plugin_manager.cpp` 删除这三个常量，改用静态函数。
- `add()` 来自 `PluginManager::addRegistration()` 中除 `run->state == stopping` 检查和 `notifyChanged()` 之外的全部逻辑，错误文本不变。
- `Run::registrations` 改为 `RegistrationScope registrations;`。`addRegistration()` 保留，函数体变为：检查 stopping → `run->registrations.add(kind, id, std::move(descriptor), run->id, run->directory, m_entries.value(run->id).manifest.contributes)` → 成功时 `notifyChanged()`。
- 其他访问 `run->registrations` 的地方：`value(kind)` → `list(kind)`，`value(kind).contains(id)` → `contains(kind, id)`，`value(kind).value(id)` → `descriptor(kind, id)`，`[kind].remove(id)` → `remove(kind, id)`，`for (auto &kind : run->registrations) kind.clear();` → `run->registrations.clear()`。`startOne()` 中预先插入空分类的循环删除（`list()` 对不存在的分类返回空）。

### 3.7 `ExtensionRuntime`

文件：`runtime/extension_runtime.h`

把 `struct PluginManager::Run` 搬到这里，改名为 `ExtensionRuntime`，成员不变（`state` 已在 3.1 改为枚举，`calls` 已在 3.4 改为 `PendingCalls`，`registrations` 已在 3.6 改为 `RegistrationScope`，`watcher`/`debounce` 在 3.9 替换）。`context` 成员类型为 `std::unique_ptr<HostContext>`，头文件前置声明 `class HostContext;` 并在文件末尾不定义析构——为保证 `unique_ptr` 能析构不完整类型，在 `extension_runtime.h` 中声明 `~ExtensionRuntime();`，在 `host_context.cpp` 中定义 `ExtensionRuntime::~ExtensionRuntime() = default;`。

`plugin_manager.h` 中删除 `struct Run;` 前置声明，改为：

```cpp
    using Run = ExtensionRuntime;
```

这样 `plugin_manager.cpp` 中的 `Run` 用法不需要改名。

### 3.8 `HostContext` 与 `HostRegistration`

把 `class PluginManager::Context` 搬到 `runtime/host_context.{h,cpp}`，改名 `HostContext`；把 `class PluginManager::Handle` 搬过去，改名 `HostRegistration`。类体、注释原样搬，只做名字替换：`Context` → `HostContext`，`Handle` → `HostRegistration`。

- `host_context.h` 只放 `HostContext` 的类声明（继承 `sdk::IExtensionContext`、`sdk::IShuffleExtensionContext`、`sdk::IPcmExtensionContext`）及 `setConfig()`；成员函数实现放 `.cpp`。
- 它们访问 `PluginManager` 的私有成员，在 `plugin_manager.h` 的类定义中加入：

```cpp
    friend class HostContext;
    friend class HostRegistration;
```

  并删除原 `class Context;`、`class Handle;` 前置声明。
- `plugin_manager.cpp` 中 `std::make_unique<Context>(...)` 改为 `std::make_unique<HostContext>(...)`。
- `HostContext` 构造函数原来接收 `const Entry &`，改为接收 `id`、`version`、`directory` 三个字符串，避免依赖 `PluginManager::Entry`。

### 3.9 `DevWatcher`

文件：`runtime/dev_watcher.h`

```cpp
#pragma once
#include <QObject>
#include <QTimer>
#include <memory>

class QFileSystemWatcher;

namespace nekotune {
/// Watches a development directory (non-recursive Qt watcher, so every entry is added) and emits
/// changed() once edits settle.
class DevWatcher final : public QObject {
    Q_OBJECT
  public:
    explicit DevWatcher(const QString &directory, QObject *parent = nullptr);
    ~DevWatcher() override;
  signals:
    void changed();

  private:
    std::unique_ptr<QFileSystemWatcher> m_watcher;
    QTimer m_debounce;
};
} // namespace nekotune
```

实现来自 `watchDevelopment()` 中收集路径、创建 watcher 和 350 ms 防抖的部分（路径过滤规则、上限 1024 不变）。`ExtensionRuntime` 的 `watcher`、`debounce` 两个成员替换为 `std::unique_ptr<DevWatcher> watcher;`。`watchDevelopment()` 改为：

```cpp
void PluginManager::watchDevelopment(const std::shared_ptr<Run> &run) {
    run->watcher = std::make_unique<DevWatcher>(run->directory);
    const auto id = run->id;
    connect(run->watcher.get(), &DevWatcher::changed, this, [this, id] {
        enqueue(
            [this, id](Completion done) {
                reload(id, [done](Result<void> result) {
                    done(result ? Result<QVariantMap>(QVariantMap{}) : Result<QVariantMap>(result.error()));
                });
            },
            [this, id](Result<QVariantMap> result) {
                if (!result)
                    recordLog(id, "error", result.error().message);
            });
    });
}
```

`stop()` 中 `run->debounce.reset(); run->watcher.reset();` 改为 `run->watcher.reset();`。

### 3.10 CMake

`nekotune_infrastructure` 加入 `runtime/` 下全部 `.cpp`（`call_guard`、`crash_guard`、`dependency_graph`、`dev_watcher`、`extension_state`、`host_context`、`native_loader`、`registration_scope`），删除旧的 `infrastructure/extensions/crash_guard.cpp`。`dev_watcher.h` 含 `Q_OBJECT`，确认目标开启了 `AUTOMOC`（`nekotune_infrastructure` 已有其他 `Q_OBJECT` 类，通常已开启）。

### 3.11 测试

文件：`tests/native_extensions_test.cpp`，加入 include：

```cpp
#include "infrastructure/extensions/runtime/call_guard.h"
#include "infrastructure/extensions/runtime/dependency_graph.h"
#include "infrastructure/extensions/runtime/extension_state.h"
```

在 `semverRanges()` 之后新增：

```cpp
    void dependencyGraph() {
        const DependencyNodes nodes{{"a", {"1.0.0", {}}},
                                    {"b", {"1.2.0", {{"a", "^1.0.0"}}}},
                                    {"c", {"1.0.0", {{"b", "^1.0.0"}}}},
                                    {"d", {"1.0.0", {{"a", "^2.0.0"}}}},
                                    {"x", {"1.0.0", {{"y", "*"}}}},
                                    {"y", {"1.0.0", {{"x", "*"}}}}};
        QCOMPARE(dependencyOrder("c", nodes).value(), (QStringList{"a", "b", "c"}));
        QVERIFY(dependencyOrder("d", nodes).error().message.contains("must satisfy ^2.0.0"));
        QVERIFY(dependencyOrder("x", nodes).error().message.contains("Dependency cycle"));
        QVERIFY(dependencyOrder("missing", nodes).error().message.contains("Missing extension"));
        QCOMPARE(dependentsOf("a", {"a", "b", "c", "d"}, nodes), (QStringList{"a", "b", "c", "d"}));
    }
    void extensionStateStrings() {
        QCOMPARE(toString(ExtensionState::Disabled), "disabled");
        QCOMPARE(toString(ExtensionState::Blocked), "blocked");
        QCOMPARE(toString(ExtensionState::Starting), "starting");
        QCOMPARE(toString(ExtensionState::Running), "running");
        QCOMPARE(toString(ExtensionState::Stopping), "stopping");
        QCOMPARE(toString(ExtensionState::Failed), "failed");
    }
    void pendingCallsCompleteOnce() {
        QObject owner;
        PendingCalls calls;
        int completed = 0;
        QString error;
        auto reply = calls.add(&owner, [&](Result<QVariantMap> result) {
            ++completed;
            error = result ? QString() : result.error().message;
        }, 50);
        reply(QVariantMap{});
        reply(QVariantMap{});
        QCOMPARE(completed, 1);
        QVERIFY(error.isEmpty());

        auto timed = calls.add(&owner, [&](Result<QVariantMap> result) { error = result.error().message; }, 20);
        QTRY_COMPARE(error, "Extension request timed out");
        timed(QVariantMap{});

        error.clear();
        auto stopped = calls.add(&owner, [&](Result<QVariantMap> result) { error = result.error().message; }, 10'000);
        calls.failAll("Extension stopped");
        QCOMPARE(error, "Extension stopped");
    }
```

`dependentsOf` 的期望顺序来自原 `dependents()` 的算法：按 `order` 扫描，结果列表在扫描中即时增长，所以 `c` 在 `b` 加入后的同一轮就会被识别。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen ./build/tests/nekotune_native_extensions_test
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
wc -l backend/infrastructure/extensions/plugin_manager.cpp
grep -nE 'class PluginManager::(Context|Handle)|struct PluginManager::Run|PendingCall\b|loadablePath\(const Entry' backend/infrastructure/extensions/plugin_manager.cpp   # 应无输出
```

## 完成标准

- [ ] 3.1–3.9 每一步都单独构建并通过了扩展测试。
- [ ] `plugin_manager.cpp` 不再包含状态字符串、`Context`/`Handle`/`Run` 定义、`PendingCall`、`loadablePath` 实现、`QFileSystemWatcher`。
- [ ] 新增测试通过；已有测试断言未修改（include 路径除外）。
- [ ] 全量测试失败列表不比基线多。
- [ ] 记录 `plugin_manager.cpp` 行数（预期从约 1850 降到 1100 左右）。

## 提交

```sh
git add backend/infrastructure/extensions backend/CMakeLists.txt tests/native_extensions_test.cpp
git status --short
git commit -m "refactor(extensions): 拆出扩展运行时部件"
```
