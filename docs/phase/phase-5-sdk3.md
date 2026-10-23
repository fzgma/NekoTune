# Phase 5：SDK 3

## 目标

1. SDK 头文件重写为 SDK 3（决定 R8、R9、R11）：Context 按能力分组，描述符强类型化，歌词接口强类型化，删除 SDK 2.1/2.2 的侧向接口。
2. 宿主 `HostContext` 改为实现 SDK 3；IID 升为 `org.nekotune.IExtensionPlugin/3`，清单 `apiVersion` 升为 `3`。
3. 迁移 `examples/extensions/*`、`tests/native_extensions/*`、`tests/fixtures/dedicated-source`、`sdk/templates/native`。
4. `HostGateway` 改用新错误码 `PermissionDenied`。

本阶段**会改变**外部行为：SDK 2.x 插件被拒绝加载。IPC 协议和快照字段仍然不变（描述符字段名保持原样）。

## 前置条件

- Phase 4（4A、4B）已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `sdk/include/nekotune/result.h` | `ErrorCode` 末尾追加 `PermissionDenied` |
| `sdk/include/nekotune/sdk/types.h` | 新建：`Params`、`Reply`、`Done`、`Handler`、`EventHandler`、`Registration` |
| `sdk/include/nekotune/sdk/providers.h` | 音源与歌词接口改为 SDK 3 |
| `sdk/include/nekotune/sdk/shuffle_provider.h` | 删除 `IShuffleExtensionContext`、`shuffleContext()` |
| `sdk/include/nekotune/sdk/pcm_provider.h` | 删除 `IPcmExtensionContext`、`pcmContext()` |
| `sdk/include/nekotune/sdk/extension_context.h` | 重写：`ExtensionContext` 与能力接口 |
| `sdk/include/nekotune/sdk/extension_plugin.h` | `activate(ExtensionContext &, Done)`；IID `/3` |
| `sdk/CMakeLists.txt`、`sdk/cmake/build-info.json.in`、`sdk/cmake/NekoTuneExtension.cmake` | 版本 `3.0.0`，`apiVersion 3` |
| `sdk/README.md` | 删除 SDK 2.1/2.2 章节，改为 SDK 3 概览 |
| `sdk/templates/native/{plugin.cpp,extension.json,CMakeLists.txt,tests/plugin_load_test.cpp,README.md}` | 迁移 |
| `sdk/templates/qml/extension.json` | `apiVersion` 保持 `2`（R8 允许 1–3，不改） |
| `backend/infrastructure/extensions/package/extension_manifest.cpp` | `apiVersion` 规则 |
| `backend/infrastructure/extensions/runtime/host_context.{h,cpp}` | 实现 SDK 3 |
| `backend/infrastructure/extensions/capabilities/{music_sources,lyrics_sources}.cpp` | 适配新 provider 签名 |
| `backend/infrastructure/extensions/host_gateway.cpp` | `ErrorCode::PermissionDenied` |
| `examples/extensions/{automation,custom-ui,lyrics,shuffle,test-source}/{plugin.cpp,extension.json}` | 迁移 |
| `tests/native_extensions/{faulty,shuffle,slow}/plugin.cpp`、`tests/fixtures/dedicated-source/plugin.cpp` | 迁移 |
| `tests/native_extensions/legacy/*` | 保留为 SDK 2 二进制夹具，用于验证拒绝加载 |
| `tests/native_extensions_test.cpp` | 按 5.9 调整 |

## 步骤

### 5.1 `result.h`

```cpp
enum class ErrorCode { InvalidArgument, NotFound, Storage, Io, Cancelled, Unavailable, PermissionDenied };
```

只追加，不调整已有顺序。构建后如果有 `switch (code)` 报未处理枚举，停止并报告（Phase 0 检查时没有发现此类 switch）。

### 5.2 `types.h`

```cpp
#pragma once
#include <nekotune/result.h>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace nekotune::sdk {
using Params = QVariantMap;
using Reply = std::function<void(Result<Params>)>;
using Done = std::function<void(Result<void>)>;
/// Services and commands must reply with an object; wrap scalars in {value}.
using Handler = std::function<void(const Params &params, Reply done)>;
using EventHandler = std::function<void(const Params &event)>;

/// Host-owned registration handle. dispose() or destruction removes the registration; both are
/// no-ops after the extension stopped, so plugins may keep handles until deactivate() returns.
class Registration {
  public:
    virtual ~Registration() = default;
    virtual void dispose() = 0;
};
using RegistrationPtr = std::unique_ptr<Registration>;
} // namespace nekotune::sdk
```

### 5.3 `providers.h`

保留 `Track`、`SearchPage`、`ResolvedAudio` 及其 `toParams()`（宿主内部协议仍在用）。改动：

```cpp
#include <nekotune/sdk/types.h>   // 取代原文件中的 Params/Reply 定义

enum class ResolvePurpose { Play, Download };

struct MusicSourceInfo {
    QString name;
    /// The source supports downloads.
    bool download = false;
    /// Local ID of this extension's browse page; empty matches a page with the source's ID.
    QString page;
};

class IMusicProvider {
  public:
    virtual ~IMusicProvider() = default;
    virtual void search(const QString &query, const QString &cursor,
                        std::function<void(Result<SearchPage>)> done) = 0;
    virtual void track(const QString &id, std::function<void(Result<Track>)> done) = 0;
    /// Downloads may take minutes and return a prepared file://.
    virtual void resolve(const QString &id, ResolvePurpose purpose,
                         std::function<void(Result<ResolvedAudio>)> done) = 0;
    /// Called after the host imported a download; the returned map is merged into the task report.
    virtual void downloaded(const QString &id, const QString &path, int songId, bool existing, Reply done) {
        Q_UNUSED(id) Q_UNUSED(path) Q_UNUSED(songId) Q_UNUSED(existing)
        done(Params{});
    }
};

struct LyricsQuery {
    QString title, artist, album, trackId;
    qint64 durationMs = 0;
};
struct LyricsDocument {
    QString syncedLyrics, plainLyrics, krcLyrics, coverUrl;
    bool instrumental = false;
};
/// One search result. Source-defined fields (IDs, tokens for staged lookups) go in extra; the
/// host keeps candidates in the backend and never sends extra to the frontend.
struct LyricsCandidate {
    QString title, artist, album;
    qint64 durationMs = 0;
    double score = 1.0;
    /// A song version in a staged source; resolving it returns further candidates.
    bool songResult = false;
    LyricsDocument document;
    QVariantMap extra;
};
/// Either a document, or further candidates for staged sources.
struct LyricsResolution {
    std::optional<LyricsDocument> document;
    QVector<LyricsCandidate> candidates;
};
struct LyricsSourceInfo {
    QString name;
};

class ILyricsProvider {
  public:
    virtual ~ILyricsProvider() = default;
    virtual void search(const LyricsQuery &query, std::function<void(Result<QVector<LyricsCandidate>>)> done) = 0;
    virtual void resolve(const LyricsCandidate &candidate, std::function<void(Result<LyricsResolution>)> done) = 0;
};
```

补充 `#include <optional>`。原 `ILyricsProvider` 上方的 “Same contract as the JavaScript SDK ...” 注释删除。

### 5.4 `shuffle_provider.h`、`pcm_provider.h`

- `shuffle_provider.h`：删除 `IShuffleExtensionContext`、`shuffleContext()`，`#include <nekotune/sdk/extension_context.h>` 改为 `#include <nekotune/sdk/types.h>`，新增：

```cpp
struct ShuffleInfo {
    QString name;
};
```

- `pcm_provider.h`：删除 `IPcmExtensionContext`、`pcmContext()`，include 改为 `types.h`。`PcmCapabilities`、`PcmBlock`、`PcmHandler` 不变（宿主 `domain/` 仍在用）。

### 5.5 `extension_context.h`

整体替换为：

```cpp
#pragma once
#include <nekotune/sdk/pcm_provider.h>
#include <nekotune/sdk/providers.h>
#include <nekotune/sdk/shuffle_provider.h>
#include <nekotune/sdk/types.h>
#include <QVariant>
#include <QtGlobal>

namespace nekotune::sdk {
struct ExtensionInfo {
    QString id;
    QString version;
    /// Installed program directory; treat as read-only.
    QString directory;
    /// Private writable data directory that survives updates.
    QString dataDirectory;
};
struct CommandInfo {
    QString title;
    QString shortcut;
};
struct TaskProgress {
    QString state;
    /// 0..1; negative when unknown.
    double progress = -1;
    QString message;
};

class Host {
  public:
    virtual ~Host() = default;
    /// Invoke a host IPC method such as "player.pause". Lifecycle, trust, appearance and
    /// irreversible library methods fail with ErrorCode::PermissionDenied.
    virtual void call(const QString &method, const Params &params, Reply done) = 0;
};
class Events {
  public:
    virtual ~Events() = default;
    /// "player.track_changed", "*", ...; extension events arrive as "extension.<id>.<event>".
    virtual RegistrationPtr on(const QString &event, EventHandler handler) = 0;
    /// Broadcast "extension.<id>.<event>" to the host and other extensions.
    virtual void publish(const QString &event, const Params &data = {}) = 0;
};
class Services {
  public:
    virtual ~Services() = default;
    virtual RegistrationPtr add(const QString &id, Handler handler) = 0;
    virtual void call(const QString &extension, const QString &service, const Params &params, Reply done) = 0;
};
class Commands {
  public:
    virtual ~Commands() = default;
    virtual RegistrationPtr add(const QString &id, Handler handler, const CommandInfo &info = {}) = 0;
};
class Ui {
  public:
    virtual ~Ui() = default;
    /// kind is pages, settings, slots, themes, menus or toolbars; fields match the manifest and
    /// sources are relative to the extension directory.
    virtual RegistrationPtr add(const QString &kind, const QString &id, const Params &descriptor) = 0;
};
class Music {
  public:
    virtual ~Music() = default;
    /// The provider stays owned by the plugin and must outlive its registration.
    virtual RegistrationPtr add(const QString &id, IMusicProvider *provider, const MusicSourceInfo &info) = 0;
};
class Lyrics {
  public:
    virtual ~Lyrics() = default;
    virtual RegistrationPtr add(const QString &id, ILyricsProvider *provider, const LyricsSourceInfo &info) = 0;
};
class Shuffle {
  public:
    virtual ~Shuffle() = default;
    /// info.name must not be empty. The provider must outlive its registration.
    virtual RegistrationPtr add(const QString &id, IShuffleProvider *provider, const ShuffleInfo &info) = 0;
};
class Pcm {
  public:
    virtual ~Pcm() = default;
    virtual PcmCapabilities capabilities() const = 0;
    /// Null when unsupported. Releasing the handle stops callbacks immediately.
    virtual RegistrationPtr subscribe(PcmHandler handler) = 0;
};
class Config {
  public:
    virtual ~Config() = default;
    virtual Params get() const = 0;
    /// Persist a new configuration; the change is also delivered as "extension.config_changed".
    virtual void update(const Params &config, Done done) = 0;
};
class Storage {
  public:
    virtual ~Storage() = default;
    /// JSON-compatible values only; the whole store is limited to 8 MiB.
    virtual void get(const QString &key, std::function<void(Result<QVariant>)> done) = 0;
    virtual void set(const QString &key, const QVariant &value, Done done) = 0;
};
class Secrets {
  public:
    virtual ~Secrets() = default;
    /// System keyring, redacted from logs. An absent secret is a null QString.
    virtual void get(const QString &key, std::function<void(Result<QString>)> done) = 0;
    virtual void set(const QString &key, const QString &value, Done done) = 0;
    virtual void remove(const QString &key, Done done) = 0;
};
class Tasks {
  public:
    virtual ~Tasks() = default;
    virtual void report(const QString &task, const TaskProgress &progress) = 0;
};
class Logger {
  public:
    virtual ~Logger() = default;
    virtual void write(QtMsgType level, const QString &message) = 0;
};

/// Runtime services for one activation. The context outlives activate() and is invalidated when
/// deactivate() returns; do not keep references beyond that point.
///
/// Every method may be called from any thread; the host marshals work to the backend thread.
/// Callbacks passed to the host run on the backend thread, at most once, and are dropped
/// without being called when the extension stops first.
class ExtensionContext {
  public:
    virtual ~ExtensionContext() = default;
    virtual const ExtensionInfo &info() const = 0;
    virtual Host &host() = 0;
    virtual Events &events() = 0;
    virtual Services &services() = 0;
    virtual Commands &commands() = 0;
    virtual Ui &ui() = 0;
    virtual Music &music() = 0;
    virtual Lyrics &lyrics() = 0;
    virtual Shuffle &shuffle() = 0;
    virtual Pcm &pcm() = 0;
    virtual Config &config() = 0;
    virtual Storage &storage() = 0;
    virtual Secrets &secrets() = 0;
    virtual Tasks &tasks() = 0;
    virtual Logger &log() = 0;
};
} // namespace nekotune::sdk
```

### 5.6 `extension_plugin.h`

- `activate` 改为 `virtual void activate(ExtensionContext &context, Done done) = 0;`，注释不变。
- `#define NEKOTUNE_EXTENSION_IID "org.nekotune.IExtensionPlugin/3"`。
- 类注释中的示例代码同步改为 SDK 3 写法。

### 5.7 SDK 构建与版本

- `sdk/CMakeLists.txt`：两处 `2.2.0` 改为 `3.0.0`；`write_basic_package_version_file` 上方注释改为 “Additive capabilities increase the SDK minor; breaking changes increase the major and the plugin IID.”。
- `sdk/cmake/build-info.json.in`：`"sdkVersion": "3.0.0"`。
- `sdk/cmake/NekoTuneExtension.cmake`：`string(JSON json SET "${json}" apiVersion 2)` 改为 `3`。
- `sdk/templates/native/CMakeLists.txt`：`find_package(NekoTuneSDK 2 REQUIRED)` 改为 `3`。
- 全仓库检查，除 `tests/native_extensions/legacy/` 外应无输出：

```sh
grep -rn 'NekoTuneSDK 2\|IExtensionPlugin/2\|apiVersion 2)' --exclude-dir=build . | grep -v tests/native_extensions/legacy
```

### 5.8 宿主实现

**清单**（`extension_manifest.cpp`）：

- 接受的 `apiVersion` 改为 `1`、`2`、`3`；身份校验失败的错误文本改为 `Invalid extension identity, version or apiVersion (expected 3)`。
- Node 扩展错误文本改为 `Node.js extensions are no longer supported; update the extension to apiVersion 3`。
- `if (hasLibrary && apiVersion != 2)` 改为 `!= 3`，文本 `Native libraries require apiVersion 3`。

**`HostContext`**（`runtime/host_context.{h,cpp}`）：

- 继承改为只继承 `sdk::ExtensionContext`。为每个能力接口定义一个私有嵌套类（`HostFacet`、`EventsFacet`、`ServicesFacet`、`CommandsFacet`、`UiFacet`、`MusicFacet`、`LyricsFacet`、`ShuffleFacet`、`PcmFacet`、`ConfigFacet`、`StorageFacet`、`SecretsFacet`、`TasksFacet`、`LoggerFacet`），每个持有 `HostContext &` 并实现对应接口；`HostContext` 持有这些对象并在访问器中返回。`info()` 返回构造时填好的 `ExtensionInfo`。
- 嵌套类方法体来自原 `HostContext` 对应方法（`hostCall`、`on`、`publish`、`registerService`、`callService`、`registerCommand`、`registerUi`、`registerMusicProvider`、`registerLyricsProvider`、`registerShuffleStrategy`、`pcmCapabilities`、`subscribePcm`、`config`、`updateConfig`、`storageGet/Set`、`secretGet/Set/Delete`、`reportTask`、`log`），逻辑不变。
- 结构体描述符转换成注册表与快照使用的字典，**字段名与 SDK 2 相同**：
  - `CommandInfo` → `{"title": title}`，`shortcut` 非空时加 `"shortcut"`；
  - `MusicSourceInfo` → `{"name": name, "download": download}`，`page` 非空时加 `"page"`；
  - `LyricsSourceInfo` → `{"name": name}`；`ShuffleInfo` → `{"name": name}`（空名仍按原规则拒绝并写日志）。
- `TaskProgress` → `{"state", "progress"（≥ 0 时）, "message"（非空时）}`，再按原 `reportTask` 加 `task` 并发布 `task` 事件。

**`MusicSources`**：调用 `provider->resolve(id, purpose == "download" ? sdk::ResolvePurpose::Download : sdk::ResolvePurpose::Play, ...)`。

**`LyricsSources`**：`ILyricsSourceCatalog` 仍是字典接口（Phase 4 定义，`ExtensionLyricsProvider` 依赖它）。在 `lyrics_sources.cpp` 匿名命名空间中实现转换，字段名沿用原协议：

| 方向 | 规则 |
| --- | --- |
| 字典 → `LyricsQuery` | `title`、`artist`、`album`、`track_id`、`duration_ms` |
| `LyricsCandidate` → 字典 | `extra` 的全部键先写入，再写 `title`、`artist`、`album`（非空）、`duration_ms`（> 0）、`score`、`song_result`（为真时）、`synced_lyrics`、`plain_lyrics`、`krc_lyrics`、`cover_url`（非空时）、`instrumental`（为真时） |
| 字典 → `LyricsCandidate` | 上述已知键读入对应字段，其余键放入 `extra` |
| `LyricsResolution` → 字典 | 有 `document` 时写文档字段（同上，不含候选字段）；否则写 `{"candidates": [...]}` |
| 搜索结果 → 字典 | `{"candidates": [...]}` |

**`HostGateway`**：`failure(..., ErrorCode::PermissionDenied)`。

### 5.9 迁移插件

所有插件的通用替换：

| SDK 2 | SDK 3 |
| --- | --- |
| `void activate(sdk::IExtensionContext &context, std::function<void(Result<void>)> done)` | `void activate(sdk::ExtensionContext &context, sdk::Done done)` |
| `context.registerService(id, h)` | `context.services().add(id, h)` |
| `context.callService(e, s, p, r)` | `context.services().call(e, s, p, r)` |
| `context.registerCommand(id, h, {{"title", t}, {"shortcut", s}})` | `context.commands().add(id, h, {.title = t, .shortcut = s})` |
| `context.registerUi(k, id, d)` | `context.ui().add(k, id, d)` |
| `context.registerMusicProvider(id, p, {{"name", n}, {"download", true}})` | `context.music().add(id, p, {.name = n, .download = true})` |
| `context.registerLyricsProvider(id, p, {{"name", n}})` | `context.lyrics().add(id, p, {.name = n})` |
| `sdk::shuffleContext(context)->registerShuffleStrategy(id, p, {{"name", n}})` | `context.shuffle().add(id, p, {.name = n})`（删除空指针检查分支） |
| `sdk::pcmContext(context)->...` | `context.pcm().capabilities()` / `subscribe(...)` |
| `context.on(e, h)` / `context.publish(e, d)` | `context.events().on(e, h)` / `context.events().publish(e, d)` |
| `context.hostCall(m, p, r)` | `context.host().call(m, p, r)` |
| `context.config()` / `updateConfig(c, d)` | `context.config().get()` / `context.config().update(c, d)` |
| `context.storageGet/storageSet` | `context.storage().get/set` |
| `context.secretGet/secretSet/secretDelete` | `context.secrets().get/set/remove` |
| `context.reportTask(t, {{"state", s}, ...})` | `context.tasks().report(t, {.state = s, ...})` |
| `context.log(level, m)` | `context.log().write(level, m)` |
| `context.id()` / `version()` / `directory()` / `dataDirectory()` | `context.info().id` / `.version` / `.directory` / `.dataDirectory` |
| `IMusicProvider::resolve(..., const QString &purpose, ...)` | `resolve(..., sdk::ResolvePurpose purpose, ...)` |

逐个迁移，迁移后的 `extension.json` 中 `apiVersion` 改为 `3`（`theme-palette`、`theme-qml` 保持 `1`）：

1. `examples/extensions/automation`、`custom-ui`、`test-source`：按表替换。
2. `examples/extensions/lyrics`：`DemoLyrics` 改为新接口：

```cpp
class DemoLyrics final : public sdk::ILyricsProvider {
  public:
    void search(const sdk::LyricsQuery &query,
                std::function<void(Result<QVector<sdk::LyricsCandidate>>)> done) override {
        sdk::LyricsCandidate candidate;
        candidate.title = query.title;
        candidate.artist = query.artist;
        candidate.document.syncedLyrics = "[00:00.00]NekoTune extension lyrics\n[00:02.00]歌词来源可独立安装与重载\n"
                                          "[00:05.00]Enjoy your music";
        candidate.extra.insert("id", "demo");
        done(QVector<sdk::LyricsCandidate>{candidate});
    }
    void resolve(const sdk::LyricsCandidate &candidate,
                 std::function<void(Result<sdk::LyricsResolution>)> done) override {
        done(sdk::LyricsResolution{candidate.document, {}});
    }
};
```

3. `examples/extensions/shuffle`：删除 `shuffleContext` 检查与 “requires a host with NekoTune SDK 2.1” 提示。
4. `tests/native_extensions/faulty`、`shuffle`、`slow`、`tests/fixtures/dedicated-source`：按表替换；`faulty` 的 `Tones::resolve` 改为接收 `sdk::ResolvePurpose`。
5. `sdk/templates/native/plugin.cpp`：按表替换；`extension.json` 的 `apiVersion` 改为 `3`；`tests/plugin_load_test.cpp` 无需改动（使用宏）。
6. `tests/native_extensions/legacy/`：**不迁移**。它带有自己的 SDK 2.0 头文件副本（IID `/2`），作为“旧二进制被拒绝”的夹具保留；把 `extension.json` 的 `name` 改为 `"SDK 2 binary fixture"`。

### 5.10 测试调整

文件：`tests/native_extensions_test.cpp`

1. `manifestValidation()`：
   - Node 扩展断言 `contains("apiVersion 2")` 改为 `contains("apiVersion 3")`。
   - `x.qt` 清单中的 `"apiVersion":2` 改为 `3`。
   - 新增：带 `library` 且 `apiVersion: 2` 的清单被拒绝，错误包含 `Native libraries require apiVersion 3`；不带 `library` 的纯界面清单 `apiVersion: 1`、`2`、`3` 都被接受。
2. `sdk20BinaryContextRemainsCompatible()` 改名为 `sdk2BinariesAreRejected()`，内容替换为：

```cpp
    void sdk2BinariesAreRejected() {
        auto manager = startManager();
        install(*manager, QStringLiteral(NEKOTUNE_TEST_EXTENSIONS "/test.legacy"));
        const auto refused = call(*manager, "extensions.enable", {{"id", "test.legacy"}, {"trusted", true}});
        QVERIFY(!refused);
        QVERIFY(refused.error().message.contains("Unsupported extension interface org.nekotune.IExtensionPlugin/2"));
        QCOMPARE(entry(*manager, "test.legacy").value("state").toString(), "failed");
    }
```

   （`nekotune_add_extension` 会把 legacy 夹具清单写成 `apiVersion: 3`，所以它能通过清单校验，到 IID 检查时被拒绝——这正是要验证的路径。）
3. `hostCallGateway()`：拒绝路径追加断言 `QCOMPARE(result.error().code, ErrorCode::PermissionDenied);`。
4. `lyricsAndAutomationExamples()`：候选断言仍通过字典读取 `title`、`synced_lyrics`（经 `LyricsSources` 转换后字段名不变），无需修改。如果失败，检查 5.8 的转换表实现，不要改断言。
5. 其他测试只允许因 SDK 调用方式变化而改动的行（例如测试内直接实现 provider 的代码），期望值不变。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen ./build/tests/nekotune_native_extensions_test
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
grep -rnE 'IShuffleExtensionContext|IPcmExtensionContext|shuffleContext\(|pcmContext\(|registerMusicProvider|registerLyricsProvider|registerShuffleStrategy|IExtensionContext' \
    --exclude-dir=build sdk examples backend tests | grep -v tests/native_extensions/legacy   # 应无输出

# 模板可以独立构建（使用源码树中的 SDK）
cmake -S sdk/templates/native -B /tmp/nekotune-template -DCMAKE_PREFIX_PATH="$PWD/build"
cmake --build /tmp/nekotune-template -j2
```

模板独立构建如果因找不到已安装 SDK 失败，改用 `cmake --install build --prefix /tmp/nekotune-host` 后以 `-DCMAKE_PREFIX_PATH=/tmp/nekotune-host` 重试；仍失败时记录错误并报告，不要修改模板的 CMake 结构。

## 完成标准

- [ ] SDK 头文件为 SDK 3，IID `/3`，SDK 版本 `3.0.0`。
- [ ] 所有示例、测试夹具（legacy 除外）、模板已迁移并构建通过。
- [ ] `sdk2BinariesAreRejected`、`hostCallGateway`（含错误码断言）通过。
- [ ] 全量测试失败列表不比基线多。
- [ ] 模板独立构建结果已记录。

## 提交

```sh
git add sdk examples backend tests
git status --short
git commit -m "feat(sdk)!: 扩展 SDK 升级到 3，按能力分组并强类型化描述符

BREAKING CHANGE: 插件 IID 升为 org.nekotune.IExtensionPlugin/3，带原生库的清单需要 apiVersion 3，SDK 2.x 插件需按新接口重新构建。"
```
