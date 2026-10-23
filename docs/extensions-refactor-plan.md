# 扩展系统重构方案

> 状态：已定稿（2026-10-23），可执行步骤见 [phase/README.md](phase/README.md)。本文记录现状诊断、目标架构与取舍。现行规则仍以[扩展开发与使用](extensions.md)为准。

## 1. 背景与目标

[C++ 原生扩展迁移](native-extensions-plan.md)（2026-10-20）移除了 Node.js 运行时，但为了让 `ExtensionService`、`MusicService` 和前端保持不变，`PluginManager` 原样保留了 Node 管理进程的字符串 request/snapshot 协议。之后陆续加入的随机算法、PCM、主题壁纸都叠加在这个结构上，扩展系统越来越难读、难改、难测。

本次重构的目标：

1. **结构清晰**：按「包管理 / 运行时 / 能力」分层，每层职责单一、可以单独测试。
2. **进程内强类型**：同一进程内的模块直接调用强类型接口，删除内部字符串协议。
3. **SDK 3**：去掉为兼容 SDK 2.0 ABI 打的补丁，按能力分组重新设计接口。
4. **补上已知的安全和稳定性缺口**：`hostCall` 越权、崩溃归因盲区、后端卡死无感知。

### 1.1 不在范围内

- **进程隔离和安全沙箱**：继续在后端进程内加载原生插件，受信任扩展仍拥有当前用户的完整权限。
- **跨编译器 ABI / C ABI**：仍要求与宿主相同的工具链和 Qt 小版本。
- **macOS**。
- **兼容 SDK 2.x**：IID 升级为 `org.nekotune.IExtensionPlugin/3`，旧插件拒绝加载并在管理页提示需要更新。仓库内示例和测试夹具随本次重构迁移；外部插件（酷狗、网易云、世界计划）之后单独重写，不在本文范围内。
- **强类型的宿主 API**：`hostCall(method, params)` 暂时保留字符串形式。

## 2. 现状诊断

### 2.1 进程内的字符串协议

`MusicService` 通过 `ExtensionService::request()` 调用音源：

```cpp
m_extensions.request("extensions.provider_call",
    {{"source", source}, {"kind", "music"}, {"operation", "search"}, {"params", params}}, done);
```

`PluginManager::providerCall` 再按 `kind`、`operation` 字符串分派，把 SDK 返回的 `SearchPage`、`Track`、`ResolvedAudio` 转成 `QVariantMap`，调用方再解析回来。同类的内部方法还有 `resolve_audio`、`release_audio`、`download_audio`、`cancel_download`、`download_completed`。这带来几个问题：

- 一次调用经历「强类型 → 字典 → 强类型」的往返，字段名约定散落在两端。
- 超时按操作名硬编码（`610'000`、`120'000`）。
- 这些方法和前端可调用的 `extensions.*` 共用一个 `request()` 入口，内外边界不清。

歌词（`ExtensionLyricsProvider`）同样经过 `provider_call`。随机算法则走另一条路：`PluginManager::selectShuffle` 由 `BackendSession` 包装成 lambda 交给 `ShuffleStrategyService`。三种来源三种接入方式。

### 2.2 `PluginManager` 是上帝对象

`plugin_manager.cpp` 1890 行，同时负责：

| 职责 | 主要函数 |
| --- | --- |
| 持久化 `registry.json` | `load`、`save` |
| 安装、更新、回滚、卸载、清理旧包 | `install`、`uninstall`、`cleanUnusedPackages` |
| 依赖解析与连锁启停 | `dependencyOrder`、`dependents`、`startChain`、`startup` |
| 原生库加载 | `loadablePath`、`startOne` |
| 生命周期与故障 | `enable`、`activated`、`stop`、`fault`、`disable`、`reload` |
| 串行变更队列 | `enqueue`、`runMutations` |
| SDK Context 实现 | `Context`（约 280 行） |
| 服务、命令、事件 | `invoke`、`deliver`、`broadcast` |
| 音源、歌词、下载 | `providerCall`、`resolveAudio`、`AudioProxy` |
| 随机算法、PCM | `selectShuffle`、`Context::subscribePcm` |
| 界面插槽选择、主题导出 | `select`、`validateSelection`、`exportTheme` |
| 日志、凭据脱敏 | `recordLog`、`redact` |
| 开发目录监听 | `watchDevelopment` |
| 对外快照 | `publicEntry`、`snapshot` |

其中还有一些结构性问题：

- 状态是字符串（`"disabled"`、`"starting"`、`"running"`、`"stopping"`、`"blocked"`、`"failed"`），分别存在 `Entry::state` 和 `Run::state` 两处，没有集中定义合法的状态转换。
- `ExtensionManifest` 只是 `QVariantMap data` 加几个访问函数，消费方各自从 `data` 里取字段。
- 注册信息同时存在 `Run::registrations`（描述字典）和 `Run::services/commands/music/...`（实现对象）两套结构，`Context::add` 负责让二者同步。

### 2.3 巨型 snapshot 字典

`publicEntry()` 把清单贡献和运行时注册拼在一起，改写 `id`、补 `localId`、`extensionId`，把 `source` 换成绝对路径，最后放进一个大 `QVariantMap`。下游各自再解析一遍：

- 后端 `ExtensionService::sources(kind)` 遍历它找音源和歌词源，`BackendSession` 据此增删 `ExtensionLyricsProvider`。
- 前端 `ExtensionsController` 从中计算页面、命令、菜单、工具栏、插槽、`browserSources`。
- `ThemeController` 从 `items()` 和 `selections()` 推导主题和外观。

字段约定没有写在任何类型定义里，改一个字段需要同时检查后端、前端和 QML。

### 2.4 外观逻辑放在扩展运行时里

主题、Shell、插槽的选择（`extensions.select`）、选择校验和主题导出（`extensions.export_theme`）都属于外观，现在由 `PluginManager` 实现并存进 `registry.json`。主题包本身没有代码，却要经过扩展运行时才能被选择。

### 2.5 SDK 的兼容补丁

为了不改变 SDK 2.0 的虚表，随机算法（2.1）和 PCM（2.2）放在独立接口 `IShuffleExtensionContext`、`IPcmExtensionContext` 里，宿主的 `Context` 同时继承三个接口，扩展通过 `dynamic_cast` 探测。描述符一律是 `QVariantMap`（`{"name", ...}`、`{"download", true}`、`{"page", "browse"}`），拼错键名不会报错。

### 2.6 已知缺陷

- 🔴 **`hostCall` 可以越权授信。** `Context::hostCall` 只在扩展激活完成前拦截 `extensions.(install|enable|disable|reload|uninstall)`。激活后，`hostCall` 经 `BackendSession` 转给 `IpcRouter::dispatch`，与前端请求走同一入口，而 `enable(id, trusted)` 会直接把 `trusted` 写入注册表。因此运行中的扩展可以调用 `hostCall("extensions.enable", {id, trusted: true})`，信任并启用任意已安装的扩展，绕过用户确认。
- 🟠 **崩溃归因有盲区。** `crashguard::Scope` 只包住后端线程上对扩展代码的调用，记录的「当前扩展」是进程级全局变量。扩展自建线程或 `QtConcurrent` 中的崩溃不会写入哨兵文件，下次启动不会自动停用该扩展，可能反复崩溃；如果崩溃时后端线程恰好在另一个扩展的 Scope 内，还会误判成那个扩展。
- 🟠 **后端卡死无感知。** 扩展回调在后端线程同步执行，随机算法的 `select` 也是同步调用。宿主不检测卡死，死循环会让播放控制和 IPC 全部失去响应，重启后也不会自动停用。
- 🟡 **库不卸载。** 开发模式每次重载都会复制并加载一份新库，长时间开发会话内旧库一直驻留。这是设计取舍（Qt 插件卸载不可靠），本次只增加提示，不改变行为。

## 3. 目标架构

### 3.1 分层

项目用 `tests/dependency_boundaries.cmake` 约束分层：`domain/` 与 `application/` 不得包含 `infrastructure/`、`ipc/`，`ipc/` 不得包含 `infrastructure/`。因此扩展系统**留在现有分层内**，按子目录拆分；业务服务依赖的接口放在 `domain/extensions/`，实现放在 `infrastructure/extensions/`。`domain/` 已允许使用 SDK 头文件（`domain/playback/playback_backend.h` 已包含 `pcm_provider.h`），接口可以直接使用 SDK 值类型。

```text
backend/domain/extensions/
├── extension_backend.h            IExtensionBackend：IPC extensions.* 管理入口（保留，收窄）
├── music_source_catalog.h         IMusicSourceCatalog：MusicService 使用的音源接口
└── lyrics_source_catalog.h        ILyricsSourceCatalog：歌词来源接口
backend/domain/appearance/
└── appearance_backend.h           IAppearanceBackend：IPC appearance.* 入口

backend/infrastructure/extensions/
├── package/                 包管理：只处理文件和元数据，不加载代码
│   ├── extension_manifest.{h,cpp} 强类型 Manifest、Contribution、Dependency
│   ├── package_store.{h,cpp}      安装解包、目录命名、清理旧包
│   ├── extension_registry.{h,cpp} registry.json：enabled、trusted、previous、开发目录
│   ├── zip_archive.{h,cpp}
│   ├── semver.{h,cpp}
│   └── theme_package.{h,cpp}
├── runtime/                 运行时：加载、启停、隔离故障，不了解具体业务
│   ├── extension_state.h          ExtensionState 枚举与字符串转换
│   ├── dependency_graph.{h,cpp}   排序、环检测、依赖方
│   ├── native_loader.{h,cpp}      运行时副本、QPluginLoader、IID 检查、DLL 搜索路径
│   ├── call_guard.{h,cpp}         崩溃归因、异常捕获、超时、一次性回调、看门狗
│   ├── crash_guard.{h,cpp}
│   ├── registration_scope.{h,cpp} 一次运行的全部注册，停止时统一撤销
│   ├── extension_context.{h,cpp}  SDK Context 实现，委托给各能力注册表
│   └── dev_watcher.{h,cpp}        开发目录监听和防抖
├── capabilities/            能力：扩展注册的对象交给对应业务
│   ├── music_sources.{h,cpp}           实现 IMusicSourceCatalog，内部使用 AudioProxy
│   ├── audio_proxy.{h,cpp}
│   ├── lyrics_sources.{h,cpp}          实现 ILyricsSourceCatalog
│   ├── extension_lyrics_provider.{h,cpp}
│   ├── shuffle_strategies.{h,cpp}      供 ShuffleStrategyService 使用
│   ├── service_bus.{h,cpp}             扩展服务、命令、事件订阅与广播
│   ├── contribution_registry.{h,cpp}   页面、设置、插槽、菜单、工具栏、主题
│   ├── extension_storage.{h,cpp}       config、storage.json
│   ├── extension_secrets.{h,cpp}       凭据与日志脱敏集合
│   └── extension_log.{h,cpp}           每扩展 500 条环形日志
├── host_gateway.{h,cpp}     hostCall 方法过滤
└── plugin_manager.{h,cpp}   门面：组装以上部件，生命周期编排，实现 IExtensionBackend

backend/infrastructure/appearance/
└── appearance_service.{h,cpp}  实现 IAppearanceBackend：外观选择、校验、导出
backend/ipc/api/appearance_api.cpp
```

门面保留 `PluginManager` 类名，减少 `BackendSession` 和测试的改名噪音；目标是降到 600 行以内，只负责编排生命周期（启用、停用、重载、安装、卸载、启动顺序、回滚）。

### 3.2 依赖方向

```text
IPC extensions_api ──► IExtensionBackend ◄── PluginManager ──► package/*
                                                  │
                                                  ├──► runtime/* ──► RegistrationScope
                                                  └──► capabilities/*（通过 RegistrationScope 注册）

MusicService (application) ──► IMusicSourceCatalog (domain) ◄── MusicSources (infrastructure)
LyricsController           ──► ExtensionLyricsProvider ──► ILyricsSourceCatalog ◄── LyricsSources
ShuffleStrategyService     ──► Selector 回调 ◄── ShuffleStrategies
IPC appearance_api ──► IAppearanceBackend ◄── AppearanceService ──► ContributionRegistry（只读）
```

规则：

- `package/` 不依赖 `runtime/` 和 `capabilities/`。
- `runtime/` 只通过 `RegistrationScope` 接触能力层，不知道音源、歌词等具体概念。
- 业务服务依赖 `domain/` 接口，不依赖 `PluginManager`，也不通过 `request("extensions.*")` 调用扩展。
- `IExtensionBackend::request()` 只承接 IPC 公开的 `extensions.*` 方法，内部方法全部删除。

### 3.3 生命周期状态机

```cpp
enum class ExtensionState { Disabled, Blocked, Starting, Running, Stopping, Failed };
```

| 当前 | 事件 | 下一个 |
| --- | --- | --- |
| Disabled | enable，依赖满足 | Starting |
| Disabled | enable，依赖未满足 | Blocked |
| Blocked | 依赖进入 Running | Starting |
| Starting | `done` 成功 | Running |
| Starting | `done` 失败、超时、异常 | Failed |
| Running | disable、reload、依赖停止 | Stopping |
| Running | 异常、看门狗判定卡死 | Failed |
| Stopping | `deactivate()` 返回 | Disabled（reload 时接着进入 Starting） |
| Failed | enable | Starting |

`ExtensionHost` 是唯一修改状态的地方，每次转换统一触发 `changed`。持久化只保存 `enabled`、`trusted` 和崩溃记录，运行状态不落盘。原来的 `Entry` 与 `Run` 合并为：

```cpp
struct ExtensionRecord {          // 来自 ExtensionRegistry，持久化
    QString id, directory;
    bool development, enabled, trusted;
};
struct ExtensionRuntime {         // 只存在于内存
    ExtensionState state;
    QString error;
    quint64 generation;
    sdk::IExtensionPlugin *plugin;
    std::unique_ptr<ExtensionContext> context;
    RegistrationScope registrations;
};
```

### 3.4 注册与能力层

每个能力注册表提供相同形状的接口：

```cpp
class MusicSourceRegistry : public QObject {
    Q_OBJECT
  public:
    struct Source { QString id, extensionId, name, page; bool download; };
    QList<Source> sources() const;
    // 按来源 ID 调用；来源不存在或扩展已停止时返回 Unavailable
    void search(const QString &source, const QString &query, const QString &cursor,
                std::function<void(Result<sdk::SearchPage>)> done);
    void track(...);
    void resolve(...);
    void downloaded(...);
  signals:
    void sourcesChanged();
  private:
    friend class ExtensionContext;
    sdk::RegistrationPtr add(const QString &extensionId, const QString &localId,
                             sdk::IMusicProvider *provider, sdk::MusicSourceInfo info, CallGuard &guard);
};
```

要点：

- 注册表持有 `(扩展 ID, 局部 ID) → provider` 和描述。`add()` 返回的句柄同时登记到该扩展的 `RegistrationScope`，扩展停止时统一撤销，扩展主动释放句柄也能撤销。
- 所有调用经过 `CallGuard`：切到后端线程、设置崩溃归因、捕获异常、套用超时，回调只触发一次且在扩展停止后丢弃。这部分逻辑现在分散在 `guarded`、`pending`、`forward` 中，统一后各注册表不再重复处理。
- 超时作为调用参数传入（`CallGuard::Timeout::Normal / Download / PostDownload`），不再按操作名判断。
- `MusicService` 改为依赖 `MusicSourceRegistry`，直接得到 `sdk::SearchPage` 等类型。音频代理和下载仍由 `AudioProxy` 实现，由 `MusicService` 显式调用，不再经过 `request("extensions.download_audio")`。
- `ExtensionLyricsProvider` 改为由 `LyricsSourceRegistry` 根据 `sourcesChanged` 创建和移除，`BackendSession` 不再遍历 snapshot。
- `ShuffleRegistry` 取代 `PluginManager::shuffleStrategies/selectShuffle` 和 `shuffleCatalogueChanged` 信号。

### 3.5 `hostCall` 网关

`HostGateway` 在转给 `IpcRouter` 之前过滤方法：

- **默认拒绝**：`extensions.*` 中的生命周期、授信、配置其他扩展、选择外观的方法；以及备份恢复、删除曲库这类不可逆操作（具体列表实施时按 [API](API.md) 逐项确定）。
- `extensions.call` 等价于 `callService`，允许。
- 被拒绝的调用返回 `PermissionDenied` 并写入扩展日志。

这不能阻止原生代码直接读写文件，但能让「信任」只能由用户在宿主界面授予。

本节作为 **Phase 1 单独修复**，不等待重构。

### 3.6 崩溃归因与看门狗

- **线程归因**：`crashguard` 的「当前扩展」改为 `thread_local`，信号处理函数读取崩溃线程自己的值。SDK 提供 `sdk::ExtensionThread` / `sdk::runAttributed(ctx, fn)` 辅助，让扩展自建线程和 `QtConcurrent` 任务也带上归因。
- **启动标记兜底**：后端启动时写入「本次运行加载过的扩展集合」，正常退出时清除。下次启动发现标记仍在、但哨兵文件没有归因结果时，认为是无法归因的崩溃；连续两次后自动进入安全模式，并在管理页提示用户逐个排查。
- **看门狗**：`CallGuard` 记录后端线程上正在执行的扩展调用及开始时间，独立线程每秒检查一次。超过 5 秒写日志并标记「疑似卡死」；超过 30 秒把该扩展 ID 写入哨兵文件，下次启动按崩溃处理。看门狗不强行中断线程，只负责让问题可见、可恢复。

### 3.7 外观从扩展运行时移出

`AppearanceService` 负责：

- 主题、Shell、各插槽的选择，持久化到独立的 `appearance.json`（启动时从 `registry.json` 的 `selections` 迁移一次）。
- 选择校验：从 `ContributionRegistry` 读取当前可用的主题和插槽贡献。
- 主题导出：调用现有的 `theme_package`。
- 启动顺序：把已选中外观所属的扩展告知 `ExtensionHost`，由后者优先启动（现在这段逻辑写在 `PluginManager::start` 里）。

IPC 方法从 `extensions.select`、`extensions.export_theme` 改为 `appearance.select`、`appearance.export_theme`；主题包的安装、卸载、启用仍走 `extensions.*`。

### 3.8 对外数据模型

`extensions.list` 拆成按用途组织的结构，取代一个混合的 `publicEntry`：

```jsonc
{
  "runtime": { "ready": true, "startupComplete": true, "safeMode": false },
  "extensions": [ { "id", "name", "version", "type", "state", "error", "trusted", "enabled", "development", "hasCode", "nativeModules" } ],
  "contributions": {
    "pages":    [ { "id": "ext/local", "extensionId", "title", "source", "group" } ],
    "settings": [...], "slots": [...], "menus": [...], "toolbars": [...], "themes": [...]
  },
  "sources": { "music": [...], "lyrics": [...], "shuffle": [...] },
  "commands": [...]
}
```

- 贡献已在后端统一完成 ID 拼接、路径解析和可用性过滤，前端只做展示和本地化。
- 外观选择通过 `appearance.get` 单独下发。
- 后端定义对应的 C++ 结构体和 `toVariant()`，字段约定集中在一处。

前端 `ExtensionsController`、`ThemeController` 和相关 QML 随之简化。这部分改动面最大，放在最后一个阶段。

## 4. SDK 3

### 4.1 Context 按能力分组

```cpp
namespace nekotune::sdk {
class ExtensionContext {
  public:
    virtual ~ExtensionContext() = default;
    virtual const ExtensionInfo &info() const = 0;   // id、version、directory、dataDirectory

    virtual Host &host() = 0;             // call(method, params, done)
    virtual Events &events() = 0;         // on、publish
    virtual Services &services() = 0;     // add、call
    virtual Commands &commands() = 0;     // add
    virtual Ui &ui() = 0;                 // add(kind, descriptor)
    virtual Music &music() = 0;           // addProvider
    virtual Lyrics &lyrics() = 0;         // addProvider
    virtual Shuffle &shuffle() = 0;       // addStrategy
    virtual Pcm &pcm() = 0;               // capabilities、subscribe
    virtual Config &config() = 0;         // get、update
    virtual Storage &storage() = 0;       // get、set
    virtual Secrets &secrets() = 0;       // get、set、remove
    virtual Tasks &tasks() = 0;           // report
    virtual Logger &log() = 0;
};
}
```

所有能力都是 Context 的一部分，宿主不支持时返回明确的错误或 `capabilities().available == false`，不再需要 `dynamic_cast`。以后新增能力时提升 SDK 次版本，在末尾追加虚函数；破坏性变化才提升 IID。

### 4.2 强类型描述符

```cpp
struct MusicSourceInfo { QString name; bool download = false; QString page; };
struct CommandInfo     { QString title; QString shortcut; };
struct ShuffleInfo     { QString name; };

m_music = ctx.music().addProvider("music", &m_provider, {.name = "Example", .download = true});
m_pause = ctx.commands().add("pause", handler, {.title = "Pause", .shortcut = "Ctrl+Shift+P"});
```

界面贡献（`ui().add`）仍使用 `Params`，因为字段与清单 JSON 一一对应，由宿主校验。

### 4.3 其他调整

- 歌词接口从 `Params` 改为 `LyricsQuery`、`LyricsCandidate`、`LyricsDocument` 结构体；来源自定义字段放在 `QVariantMap extra` 中。
- `resolve` 的 `purpose` 改为 `enum class ResolvePurpose { Play, Download }`。
- `activate` 的回调类型命名为 `sdk::Done`。
- 线程归因辅助见 3.6。
- `IExtensionPlugin` 生命周期约定不变：库不卸载、`deactivate()` 必须恢复初始状态。

### 4.4 清单

`apiVersion` 升为 `3`，字段基本保持不变。带原生库的扩展必须声明 `3`；纯界面扩展和主题包继续接受 `1`、`2`、`3`，避免无代码的主题包被迫改版本号。

## 5. 测试策略

| 层 | 测试方式 |
| --- | --- |
| `package/` | 纯文件操作：清单解析、ZIP 安全、安装回滚、清理旧包，不加载任何库 |
| `runtime/` | 用 `tests/native_extensions/` 下的假插件验证状态机、依赖、超时、异常、看门狗、崩溃哨兵 |
| `capabilities/` | 用内存中的假 provider 直接注册，验证调用、撤销、超时、停止后丢弃回调 |
| `HostGateway` | 白名单与拒绝路径，特别是 `extensions.enable` 越权 |
| 集成 | 保留少量端到端测试：安装示例 ZIP → 启用 → 搜索 → 播放解析 → 下载 → 停用 |

现有 `native_extensions_test.cpp`（909 行）和 `extensions_test.cpp`（1784 行）大量通过字符串协议断言，会随各阶段改写或拆分到上表对应的测试中。每个阶段结束时 `ctest` 必须全绿。

## 6. 分阶段路线

可执行步骤见 [phase/README.md](phase/README.md)。每个阶段单独提交，结束时 `ctest` 全绿。

| 阶段 | 内容 | 主要风险 |
| --- | --- | --- |
| 0 | 建分支、构建、记录测试基线 | 无 |
| 1 | `HostGateway` 修复 `hostCall` 越权（3.5），独立于重构 | 低 |
| 2 | 包管理层：强类型 Manifest、`PackageStore`、`ExtensionRegistry`，代码搬迁 | 低 |
| 3 | 运行时层：`ExtensionState`、`DependencyGraph`、`NativeLoader`、`CallGuard`、`RegistrationScope`、`DevWatcher` | 中：生命周期边界情况多 |
| 4 | 能力层：音源、歌词、随机、服务总线、贡献直连，删除内部字符串方法 | 中：改动 `MusicService` 和下载流程 |
| 5 | SDK 3：接口重写，迁移示例、测试夹具、模板 | 中 |
| 6 | 崩溃归因 `thread_local`、启动标记、看门狗（3.6） | 中：信号处理与多线程 |
| 7 | `AppearanceService` 与新的对外数据模型，简化前端控制器 | 中高：前端改动面大 |
| 8 | 文档与完整验收 | 低 |

阶段 2–4 期间 SDK 保持 2.x，只改宿主内部，便于逐步验证行为不变；阶段 5 再一次性升级 SDK。

## 7. 已定的取舍

| 问题 | 决定 |
| --- | --- |
| 越权修复时机 | Phase 1 单独修复，不等重构 |
| `HostGateway` 拒绝范围 | 拒绝 `extensions.*`（`extensions.call`、`extensions.list` 除外）、`appearance.*`、`library.delete/migrate/restore/restore_preview/remove_rollback`、`history.clear`、`ai.config.*`。暂不做清单权限声明 |
| 目录 | 留在现有分层内，按 3.1 拆子目录；不新建 `backend/extensions/` |
| 外观 IPC | `extensions.select` → `appearance.select`，`extensions.export_theme` → `appearance.export_theme`，选择迁到 `appearance.json` |
| 看门狗阈值 | 后端线程上的扩展调用 5 秒告警、30 秒判定卡死；随机算法 `select` 超过 200 毫秒告警 |
| 纯 QML 扩展的 `apiVersion` | 带原生库必须为 `3`；纯界面扩展与主题包接受 `1`–`3` |
| 前端 QML 侧授权 | 已信任扩展的 QML 拿得到完整 `controllers`，无法在同一引擎内限制，记为已知限制，本次不处理 |
