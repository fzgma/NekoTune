# Phase 4：能力层

## 目标

1. 音源、歌词、随机算法改为**强类型注册表**，业务服务通过 `domain/` 接口直接调用，删除 R6 列出的内部字符串方法。
2. 服务、命令、事件、存储、配置、凭据、日志从 `PluginManager` 移到独立的能力类。
3. `PluginManager` 只保留生命周期编排、`extensions.*` 公开方法分派和快照。

**IPC 协议、事件格式、错误文本不变。** 界面贡献（`ContributionRegistry`）放到 Phase 7，与外观一起改。PCM 订阅逻辑较小，留在 `HostContext` 不动。

本阶段分两次提交：4A（业务能力）和 4B（扩展基础能力）。4A 提交后必须全绿再开始 4B。

## 前置条件

- Phase 3 已完成并提交。

## 涉及文件

### 4A

| 文件 | 改动 |
| --- | --- |
| `backend/domain/extensions/music_source_catalog.h` | 新建：`IMusicSourceCatalog` |
| `backend/domain/extensions/lyrics_source_catalog.h` | 新建：`ILyricsSourceCatalog` |
| `backend/infrastructure/extensions/capabilities/capability_host.h` | 新建：能力类访问运行时的接口 |
| `backend/infrastructure/extensions/capabilities/music_sources.{h,cpp}` | 新建 |
| `backend/infrastructure/extensions/capabilities/audio_proxy.{h,cpp}` | 从上级目录 `git mv` |
| `backend/infrastructure/extensions/capabilities/lyrics_sources.{h,cpp}` | 新建 |
| `backend/infrastructure/extensions/capabilities/extension_lyrics_provider.{h,cpp}` | 从上级目录 `git mv`，改用 `ILyricsSourceCatalog` |
| `backend/infrastructure/extensions/capabilities/shuffle_strategies.{h,cpp}` | 新建 |
| `backend/infrastructure/extensions/runtime/call_guard.{h,cpp}` | `PendingCalls::add` 改为模板 |
| `backend/infrastructure/extensions/runtime/{host_context,extension_runtime}.*` | 注册改为写入能力类 |
| `backend/infrastructure/extensions/plugin_manager.{h,cpp}` | 删除 `providerCall`、`resolveAudio`、`selectShuffle`、`shuffleStrategies`、内部方法分派；实现 `CapabilityHost` |
| `backend/application/extensions/music_service.{h,cpp}` | 依赖 `IMusicSourceCatalog` |
| `backend/application/extensions/extension_service.h` | 删除 `sources()` |
| `backend/runtime/backend_session.{h,cpp}` | 组装方式改变 |
| `backend/CMakeLists.txt` | 新文件；`nekotune_domain` 加入两个新头文件 |
| `tests/native_extensions_test.cpp`、`tests/library_storage_test.cpp`、`tests/shuffle_strategy_test.cpp`、`tests/pcm_capture_test.cpp` | 按 4A.9 调整调用方式 |

### 4B

| 文件 | 改动 |
| --- | --- |
| `backend/infrastructure/extensions/capabilities/service_bus.{h,cpp}` | 新建：服务、命令、事件订阅 |
| `backend/infrastructure/extensions/capabilities/extension_storage.{h,cpp}` | 新建：`storage.json`、`config.json` |
| `backend/infrastructure/extensions/capabilities/extension_secrets.{h,cpp}` | 从上级目录 `git mv`，新增 `SecretVault` |
| `backend/infrastructure/extensions/capabilities/extension_log.{h,cpp}` | 新建：日志环形缓冲与脱敏 |
| `backend/infrastructure/extensions/runtime/{host_context,extension_runtime}.*` | 改为委托能力类 |
| `backend/infrastructure/extensions/plugin_manager.{h,cpp}` | 删除对应实现 |
| `backend/CMakeLists.txt` | 新文件 |
| `tests/native_extensions_test.cpp` | 新增 `serviceBusDeliversInOrder` |

---

## 4A：业务能力

### 4A.1 `PendingCalls::add` 改为模板

文件：`runtime/call_guard.{h,cpp}`

强类型注册表需要在不转成字典的情况下等待扩展回调，因此把 Phase 3 的 `add` 泛化：

```cpp
class PendingCalls {
  public:
    template <class T>
    std::function<void(Result<T>)> add(QObject *owner, std::function<void(Result<T>)> done, int timeoutMs);
    void failAll(const QString &message);

  private:
    struct Call {
        std::atomic<bool> fired{false};
        QTimer *timer = nullptr;
        /// Fails the typed completion; used by the timeout and failAll().
        std::function<void(const AppError &)> fail;
    };
    std::shared_ptr<Call> track(QObject *owner, int timeoutMs);
    static bool claim(const std::shared_ptr<Call> &call);
    QList<std::shared_ptr<Call>> m_calls;
};
```

- `track()`：清理已完成的调用，创建 `Call` 和计时器（超时调用 `fail(failure("Extension request timed out"))`），加入 `m_calls`。
- `claim()`：`fired.exchange(true)` 为假时停止并 `deleteLater` 计时器，返回 `true`；否则返回 `false`。原 `finishCall` 的“只完成一次”语义由它保证。
- 模板 `add()` 在头文件中实现：`track()` 后设置 `call->fail = [call, done](const AppError &e) { if (claim(call)) done(e); }`，返回的回调用 `QMetaObject::invokeMethod(owner, ..., Qt::AutoConnection)` 切到 `owner` 线程后，`if (claim(call)) done(result)`。用 `QPointer<QObject>` 守护 `owner`。
- `failAll()` 对每个 `Call` 调用 `fail`。

已有的 `Completion` 用法（`Result<QVariantMap>`）通过模板推导继续工作；`pendingCallsCompleteOnce` 测试不改。

### 4A.2 领域接口

文件：`backend/domain/extensions/music_source_catalog.h`

```cpp
#pragma once
#include "domain/result.h"
#include <nekotune/sdk/providers.h>
#include <QObject>
#include <QUrl>

namespace nekotune {
/// Music sources registered by running extensions. Lives on the backend thread; callbacks
/// complete once on that thread.
class IMusicSourceCatalog : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    using Progress = std::function<void(qint64 received, qint64 total)>;
    struct Playback {
        QUrl url;
        /// File extension suggested by the source; empty when unknown.
        QString extension;
    };
    /// Public descriptors ({id, localId, extensionId, name, ...}) in installation order.
    virtual QVariantList sources() const = 0;
    virtual void search(const QString &source, const QString &query, const QString &cursor,
                        std::function<void(Result<sdk::SearchPage>)> done) = 0;
    virtual void track(const QString &source, const QString &id, std::function<void(Result<sdk::Track>)> done) = 0;
    /// A loopback proxy lease for HTTP(S) results, or a prepared file inside the extension's data.
    virtual void resolvePlayback(const QString &source, const QString &id,
                                 std::function<void(Result<Playback>)> done) = 0;
    virtual void release(const QUrl &url) = 0;
    /// Download to "<base>.<ext>"; completes with the written path.
    virtual void download(const QString &source, const QString &id, const QString &base, const QString &task,
                          Progress progress, std::function<void(Result<QString>)> done) = 0;
    virtual void cancelDownload(const QString &task) = 0;
    /// Called after the host imported a download; the map is merged into the task report.
    virtual void downloaded(const QString &source, const QString &id, const QString &path, int songId,
                            bool existing, std::function<void(Result<QVariantMap>)> done) = 0;
  signals:
    void sourcesChanged();
};
} // namespace nekotune
```

文件：`backend/domain/extensions/lyrics_source_catalog.h`

```cpp
#pragma once
#include "domain/result.h"
#include <QObject>
#include <QVariantMap>

namespace nekotune {
/// Lyrics sources registered by running extensions; SDK 2 lyrics use JSON-compatible maps.
class ILyricsSourceCatalog : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    using Reply = std::function<void(Result<QVariantMap>)>;
    virtual QVariantList sources() const = 0;
    virtual void search(const QString &source, const QVariantMap &query, Reply done) = 0;
    virtual void resolve(const QString &source, const QVariantMap &candidate, Reply done) = 0;
  signals:
    void sourcesChanged();
};
} // namespace nekotune
```

`backend/CMakeLists.txt` 的 `nekotune_domain` 加入这两个头文件（与 `domain/extensions/extension_backend.h` 放在一起），使 AUTOMOC 处理 `Q_OBJECT`。

### 4A.3 `CapabilityHost`

文件：`capabilities/capability_host.h`

```cpp
#pragma once
#include <QStringList>
#include <functional>

class QObject;

namespace nekotune {
class PendingCalls;
/// The runtime services a capability registry needs; implemented by PluginManager.
class CapabilityHost {
  public:
    virtual ~CapabilityHost() = default;
    /// Backend-thread object owning timers and receiving posted work.
    virtual QObject *owner() = 0;
    /// Installed extensions in installation order.
    virtual QStringList extensionOrder() const = 0;
    virtual bool running(const QString &extensionId) const = 0;
    /// Calls of a running extension; null when it is not running.
    virtual PendingCalls *pendingCalls(const QString &extensionId) = 0;
    /// Run extension code with crash attribution; an exception faults the extension.
    virtual bool guarded(const QString &extensionId, const std::function<void()> &call) = 0;
    virtual QString dataDirectory(const QString &extensionId) const = 0;
    virtual void recordLog(const QString &extensionId, const QString &level, const QString &message) = 0;
};
} // namespace nekotune
```

`PluginManager` 私有继承 `CapabilityHost` 并实现：

- `owner()` 返回 `this`；`extensionOrder()` 返回 `m_order`；`running()` 判断 `m_runs` 中状态为 `Running`；`pendingCalls()` 返回 `&run->calls` 或 `nullptr`；`guarded()` 转调模板 `guarded()`；`dataDirectory()`、`recordLog()` 转调已有实现。

### 4A.4 `MusicSources`

文件：`capabilities/music_sources.h`

```cpp
#pragma once
#include "domain/extensions/music_source_catalog.h"
#include "infrastructure/extensions/capabilities/audio_proxy.h"
#include <QHash>

namespace nekotune {
class CapabilityHost;
class MusicSources final : public IMusicSourceCatalog {
    Q_OBJECT
  public:
    explicit MusicSources(CapabilityHost &host, QObject *parent = nullptr);
    void add(const QString &extensionId, const QString &localId, sdk::IMusicProvider *provider,
             QVariantMap descriptor);
    void remove(const QString &extensionId, const QString &localId);
    /// Drops every provider and proxy lease of one extension.
    void removeExtension(const QString &extensionId);
    void close();
    void notifyChanged() { emit sourcesChanged(); }

    QVariantList sources() const override;
    // ... IMusicSourceCatalog overrides
  private:
    struct Source {
        sdk::IMusicProvider *provider = nullptr;
        QVariantMap descriptor;
    };
    /// Provider and pending calls for a running source; reports why it is unavailable otherwise.
    ...
    CapabilityHost &m_host;
    QHash<QString, QMap<QString, Source>> m_sources;   // extension → local ID → source
    AudioProxy m_audio;
};
} // namespace nekotune
```

先 `git mv` `audio_proxy.{h,cpp}` 到 `capabilities/` 并更新 include。

实现要求：

- `sources()`：按 `m_host.extensionOrder()`，对每个 `running()` 的扩展按局部 ID 顺序输出 `descriptor`。顺序与原 `ExtensionService::sources("music")`（快照中运行扩展的 `registrations.music`）一致。
- 查找来源：`source` 拆成 `扩展ID/局部ID`；扩展未运行或局部 ID 不存在时返回 `failure("Source is unavailable")`（原 `providerCall` 文本）。
- `search`、`track`、`downloaded`：来自 `providerCall()` 对应分支。用 `m_host.pendingCalls(ext)->add<sdk::SearchPage>(m_host.owner(), done, timeouts::call)` 等包装回调，再在 `m_host.guarded(ext, ...)` 内调用 provider。`downloaded` 超时 `timeouts::downloaded`。**不再**把 `SearchPage`、`Track` 转成字典。
- `resolvePlayback`：来自 `PluginManager::resolveAudio()`：调用 `provider->resolve(id, "play", ...)`；本地文件必须位于 `m_host.dataDirectory(ext)` 内（错误文本不变）；HTTP(S) 结果交给 `m_audio.lease(...)`。`lease` 需要字典，在这里用 `ResolvedAudio::toParams()` 转换——这是 `AudioProxy` 内部协议，不对外暴露。把 `lease` 返回的 `{url, extension}` 转成 `Playback`。
- `release`、`cancelDownload`：转调 `m_audio`。
- `download`：转调 `m_audio.download(...)`，`dataDirectory` 参数用 `m_host.dataDirectory(ext)`，回调中取 `path`。
- `AudioProxy` 的解析回调（构造参数）来自原 `PluginManager` 构造函数：对 `purpose == "download"` 使用 `timeouts::download`，否则 `timeouts::call`，调用 `provider->resolve(id, purpose, ...)` 并转成 `toParams()` 字典交给代理。
- `removeExtension()`：`m_audio.revoke(ext)` 后删除 `m_sources[ext]`。
- `close()`：`m_audio.close()`。

### 4A.5 `LyricsSources` 与 `ExtensionLyricsProvider`

`capabilities/lyrics_sources.{h,cpp}`：结构同 `MusicSources`（无代理）。`search`、`resolve` 来自 `providerCall()` 的 `kind == "lyrics"` 分支，回调类型保持 `Result<QVariantMap>`，超时 `timeouts::call`。

`git mv` `extension_lyrics_provider.{h,cpp}` 到 `capabilities/`，构造参数 `IExtensionBackend &backend` 改为 `ILyricsSourceCatalog &catalog`，成员 `QPointer<IExtensionBackend>` 改为 `QPointer<ILyricsSourceCatalog>`。`call()` 中：

```cpp
            backend->request(
                "extensions.provider_call",
                {{"source", source}, {"kind", "lyrics"}, {"operation", operation}, {"params", params}},
                [guard, token, candidates, generation](Result<QVariantMap> result) {
```

改为：

```cpp
            const auto reply = [guard, token, candidates, generation](Result<QVariantMap> result) {
```

并在其后按 `operation` 调用 `catalog->search(source, params, reply)` 或 `catalog->resolve(source, params, reply)`。回调体不变。

### 4A.6 `ShuffleStrategies`

文件：`capabilities/shuffle_strategies.h`

```cpp
#pragma once
#include "domain/playback/shuffle_strategy_descriptor.h"
#include <nekotune/sdk/shuffle_provider.h>
#include <QHash>
#include <QMap>
#include <QObject>

namespace nekotune {
class CapabilityHost;
class ShuffleStrategies final : public QObject {
    Q_OBJECT
  public:
    explicit ShuffleStrategies(CapabilityHost &host, QObject *parent = nullptr);
    void add(const QString &extensionId, const QString &localId, sdk::IShuffleProvider *provider, QString name);
    void remove(const QString &extensionId, const QString &localId);
    void removeExtension(const QString &extensionId);
    QList<ShuffleStrategyDescriptor> strategies() const;
    Result<sdk::ShuffleSelection> select(const QString &id, quint64 generation, const sdk::ShuffleRequest &request);
  signals:
    void catalogueChanged();

  private:
    struct Strategy {
        sdk::IShuffleProvider *provider = nullptr;
        QString name;
        quint64 generation = 0;
        bool failed = false;
    };
    CapabilityHost &m_host;
    QHash<QString, QMap<QString, Strategy>> m_strategies;
    quint64 m_generation = 0;
};
} // namespace nekotune
```

- `strategies()`、`select()` 来自 `PluginManager::shuffleStrategies()`、`selectShuffle()`，逻辑与错误文本不变；`run->state != Running` 的判断改为 `!m_host.running(owner)`；异常处理沿用 Phase 3 的 `runAttributed` 写法（`select` 是同步调用，不经过 `guarded()` 的故障停用，与原行为一致）。失败时 `m_host.recordLog(...)`，并 `emit catalogueChanged()`（原代码调用 `notifyChanged()`，后者会发出 `shuffleCatalogueChanged`，见 4A.7）。
- `add()` 使用自己的 `++m_generation`（原来与运行代次共用 `PluginManager::m_generation`，只要求单调递增，测试只比较大小）。
- `add`、`remove`、`removeExtension` 后都 `emit catalogueChanged()`。

### 4A.7 `PluginManager` 与 `HostContext`

1. 新增成员（声明在 `m_loader` 之后）：

```cpp
    MusicSources m_music{*this};
    LyricsSources m_lyrics{*this};
    ShuffleStrategies m_shuffle{*this};
```

   并提供访问器：

```cpp
    IMusicSourceCatalog &musicSources() { return m_music; }
    ILyricsSourceCatalog &lyricsSources() { return m_lyrics; }
    ShuffleStrategies &shuffle() { return m_shuffle; }
```

2. 删除：`providerCall()`、`resolveAudio()`、`shuffleStrategies()`、`selectShuffle()`、信号 `shuffleCatalogueChanged`、成员 `m_audio`、`Run::music`、`Run::lyrics`、`Run::shuffle`（及 `Run::Shuffle` 结构）。
3. `request()` 删除 `extensions.provider_call`、`extensions.resolve_audio`、`extensions.release_audio`、`extensions.cancel_download`、`extensions.download_audio`、`extensions.download_completed` 六个分支。这些方法现在落到末尾的 `enqueue(...)`，返回 `Unknown extension operation: ...`（`extensions_api.cpp` 从未公开它们，前端不受影响）。
4. `HostContext::registerMusicProvider`、`registerLyricsProvider`、`registerShuffleStrategy` 的 install/remove lambda 改为调用 `manager->m_music.add/remove`、`m_lyrics.add/remove`、`m_shuffle.add/remove`（`add` 传入 `run.id` 和描述；随机算法传 `descriptor.value("name").toString()`）。`add()` 中 `if (kind == "shuffle") emit manager->shuffleCatalogueChanged();` 两处删除（注册表自己发信号）。
5. `stop()`：`run->music.clear(); run->lyrics.clear(); run->shuffle.clear();` 替换为 `m_music.removeExtension(id); m_lyrics.removeExtension(id); m_shuffle.removeExtension(id);`，位置不变（在 `deactivate()` 之后）。删除 `m_audio->revoke(id);`（已含在 `m_music.removeExtension`）和 `emit shuffleCatalogueChanged();`。
6. `activated()` 中的 `emit shuffleCatalogueChanged();` 改为 `emit m_shuffle.catalogueChanged();`（运行状态变化会改变可见算法）。
7. `notifyChanged()` 的计时器回调中，`emit shuffleCatalogueChanged();` 改为：

```cpp
        m_music.notifyChanged();
        m_lyrics.notifyChanged();
        emit m_shuffle.catalogueChanged();
```

8. `shutdown()` 中 `m_audio->close();` 改为 `m_music.close();`。
9. 删除构造函数中创建 `AudioProxy` 的代码。

### 4A.8 业务服务与组装

`MusicService`：

- 构造参数 `ExtensionService &extensions` 改为 `IMusicSourceCatalog &sources`，成员 `m_extensions` 改为 `m_sources`；`connect(&sources, &IMusicSourceCatalog::sourcesChanged, this, &MusicService::sourcesChanged)`。
- `sources()` 返回 `m_sources.sources()`。
- `resolve()`：`m_sources.resolvePlayback(song.providerId, song.providerTrackId, ...)`，成功时 `done(playback.value().url)`。
- `release()`：`m_sources.release(url)`。
- `search(source, params, done)`：调用 `m_sources.search(source, params.value("query").toString(), params.value("cursor").toString(), ...)`，把 `SearchPage` 转为与原来相同的字典：`{"tracks": [track.toParams()...]}`，`cursor` 非空时加入 `cursor`（来自原 `providerCall` 的转换代码）。
- `track()`：成功时 `done(track.value().toParams())`。
- `cancelDownload()`：`m_sources.cancelDownload(task); done(QVariantMap{});`。
- `download()`：`"extensions.download_audio"` 请求改为 `m_sources.download(source, song.providerTrackId, base.value(), task, progress, ...)`，其中 `progress` 发出与原 `PluginManager` 完全相同的事件：

```cpp
[guard, task](qint64 received, qint64 total) {
    if (guard)
        emit guard->taskChanged({{"event", "music.download"}, {"task", task}, {"state", "running"},
                                 {"received", received}, {"total", total}});
}
```

  完成回调参数由 `Result<QVariantMap>` 变为 `Result<QString>`，`result.value().value("path").toString()` 改为 `result.value()`。`"extensions.download_completed"` 请求改为 `m_sources.downloaded(source, song.providerTrackId, path, songId, existing, ...)`，回调体不变。

`ExtensionService`：删除 `sources()`（确认已无调用者）。

`BackendSession`：

- 成员初始化 `m_musicSources(m_extensions, ...)` 改为 `m_musicSources(m_extensionBackend.musicSources(), ...)`。
- `connect(&m_extensionBackend, &PluginManager::shuffleCatalogueChanged, ...)` 改为连接 `&m_extensionBackend.shuffle()` 的 `ShuffleStrategies::catalogueChanged`，回调中 `shuffleStrategies()` → `shuffle().strategies()`，`selectShuffle` → `shuffle().select`，守护指针改为 `QPointer<ShuffleStrategies>`。
- 歌词同步：`connect(&m_extensions, &ExtensionService::changed, ...)` 改为连接 `&m_extensionBackend.lyricsSources()` 的 `sourcesChanged`，遍历 `lyricsSources().sources()`；`new ExtensionLyricsProvider(m_extensionBackend, ...)` 改为传 `m_extensionBackend.lyricsSources()`。
- include 路径更新为 `capabilities/extension_lyrics_provider.h`。

### 4A.9 测试调整

只允许以下调整；断言的期望值不变。

`tests/native_extensions_test.cpp`：

1. 新增辅助函数（放在匿名命名空间 `call()` 之后）：

```cpp
template <class T, class Start> Result<T> wait(Start start, int timeout = 10'000) {
    std::optional<Result<T>> result;
    start([&](Result<T> value) { result = std::move(value); });
    QElapsedTimer timer;
    timer.start();
    while (!result && timer.elapsed() < timeout)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return result ? *result : Result<T>(failure("Test request timed out"));
}
```

2. `shuffleRegistrationsAndGenerations`、`shuffleFailuresFallback`、`artistShuffleExample`：`manager->shuffleStrategies()` → `manager->shuffle().strategies()`；`manager->selectShuffle(` → `manager->shuffle().select(`；`&PluginManager::shuffleCatalogueChanged` → `&manager->shuffle(), &ShuffleStrategies::catalogueChanged`。
3. `lyricsAndAutomationExamples`：`call(*manager, "extensions.provider_call", {... "lyrics" ... "search" ...})` 改为

```cpp
        const auto lyrics = wait<QVariantMap>([&](auto done) {
            manager->lyricsSources().search("example.lyrics/demo", {{"title", "Song"}}, done);
        });
```

4. `servicesStorageEventsAndSources`：
   - 音源搜索改为 `wait<sdk::SearchPage>(... manager->musicSources().search("test.faulty/tones", "A", {}, done) ...)`，断言改为读结构体字段：`search.value().tracks.value(0).title == "Tone A"`、`durationMs == 8000`、`search.value().cursor == "next"`。
   - `extensions.resolve_audio` 改为 `wait<IMusicSourceCatalog::Playback>(... resolvePlayback("test.faulty/tones", "one", done) ...)`，断言 `audio.value().url.toLocalFile().endsWith(...)`；`outside` 一行同理断言失败。
   - 最后读取快照中 `registrations.music` 的断言不变。
5. `audioProxyAndDownloads`：
   - `resolve_audio` → `resolvePlayback`；`leased.value().value("url")` → `leased.value().url`；`value("extension")` → `leased.value().extension`。
   - `release_audio` → `manager->musicSources().release(url);`（原来断言调用成功，改为直接调用后继续后续断言）。
   - `download_audio` → `wait<QString>(... manager->musicSources().download("example.test-source/tones", "one", base, "t1", {}, done) ...)`，断言 `downloaded.value() == base + ".wav"`。
   - 删除 `QSignalSpy events`（原测试没有对它断言；下载进度事件现在由 `MusicService::taskChanged` 发出）。

`tests/library_storage_test.cpp`：

- `DeferredExtensions` 改为实现 `IMusicSourceCatalog` 的 `DeferredSources`：`download()` 保存 `std::function<void(Result<QString>)>`；`downloaded()` 把参数存入 `QVariantMap callback`（键 `path`、`song_id`、`existing`、`id`、`source`）后 `done(QVariantMap{})`；其他方法立即失败或返回空。
- 两个测试中 `ExtensionService extensions(backend);` 删除，`MusicService service(extensions, ...)` 改为 `MusicService service(sources, ...)`；`complete(QVariantMap{{"path", source}})` 改为 `complete(source)`。断言不变。

`tests/shuffle_strategy_test.cpp`、`tests/pcm_capture_test.cpp`：如果使用了 `shuffleStrategies()`、`shuffleCatalogueChanged` 或已删除的头文件，按第 2 条同样替换。

### 4A 验证与提交

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
grep -rnE 'extensions\.(provider_call|resolve_audio|release_audio|download_audio|cancel_download|download_completed)' backend tests   # 应无输出
wc -l backend/infrastructure/extensions/plugin_manager.cpp
```

```sh
git add backend tests
git status --short
git commit -m "refactor(extensions): 音源、歌词与随机算法改用强类型能力注册表"
```

---

## 4B：扩展基础能力

### 4B.1 `ServiceBus`

文件：`capabilities/service_bus.h`

```cpp
#pragma once
#include <nekotune/sdk/extension_context.h>
#include <QHash>
#include <map>

namespace nekotune {
class CapabilityHost;
/// Services, commands and event subscriptions of running extensions.
class ServiceBus {
  public:
    using Completion = std::function<void(Result<QVariantMap>)>;
    explicit ServiceBus(CapabilityHost &host) : m_host(host) {}
    void addService(const QString &extensionId, const QString &id, sdk::Handler handler);
    void addCommand(const QString &extensionId, const QString &id, sdk::Handler handler);
    void removeService(const QString &extensionId, const QString &id);
    void removeCommand(const QString &extensionId, const QString &id);
    /// key is reserved by the caller so a handle can remove a subscription installed later.
    void subscribe(const QString &extensionId, quint64 key, QString event, sdk::EventHandler handler);
    void unsubscribe(const QString &extensionId, quint64 key);
    void removeExtension(const QString &extensionId);
    /// Service or command of a running extension; "<id>/" prefixes are accepted.
    void invoke(const QString &extensionId, QString service, const QVariantMap &params, Completion done);
    void deliver(const QString &extensionId, const QVariantMap &event);
    void broadcast(const QVariantMap &event);

  private:
    struct Extension {
        QHash<QString, sdk::Handler> services, commands;
        std::map<quint64, std::pair<QString, sdk::EventHandler>> subscriptions;
    };
    CapabilityHost &m_host;
    QHash<QString, Extension> m_extensions;
};
} // namespace nekotune
```

- `invoke`、`deliver`、`broadcast` 来自 `PluginManager::invoke()`、`deliver()`、`broadcast()`，错误文本不变（`Extension is not running`、`Service name required`、`Unknown service/command: ...`），超时 `timeouts::call`，调用经 `m_host.guarded()`。`broadcast` 按 `m_host.extensionOrder()` 只投递给 `running()` 的扩展（原实现遍历 `m_runs` 中 `Running` 的扩展，顺序为哈希顺序；改为安装顺序更稳定，测试不依赖该顺序）。
- `deliver` 保留“先复制订阅再逐个调用、调用前确认仍存在”的逻辑和注释。
- `subscribe` 保留原 `on()` 中 `run->state != Stopping` 的判断：由调用方在 `HostContext` 中检查后再调用。

`PluginManager`：新增成员 `ServiceBus m_bus{*this};`；删除 `invoke`、`deliver`、`broadcast` 的实现，`request()` 中 `extensions.call` 改为 `m_bus.invoke(...)`；`publish()` 改为 `m_bus.broadcast(event)`；`setConfig()` 中的 `deliver(run, ...)` 改为 `m_bus.deliver(id, ...)`。`Run::services`、`Run::commands`、`Run::subscriptions`、`Run::nextSubscription` 删除；`stop()` 中对应的 `clear()` 改为 `m_bus.removeExtension(id)`。

`HostContext` 的 `registerService`、`registerCommand`、`on`、`publish`、`callService` 改为委托 `m_bus`。

### 4B.2 `ExtensionStorage`

文件：`capabilities/extension_storage.{h,cpp}`，提供：

```cpp
class ExtensionStorage {
  public:
    explicit ExtensionStorage(const PackageStore &packages) : m_packages(packages) {}
    void load(const QString &extensionId);                 // storage.json → memory
    void unload(const QString &extensionId);
    QVariant get(const QString &extensionId, const QString &key) const;
    Result<void> set(const QString &extensionId, const QString &key, const QVariant &value);
    QVariantMap readConfig(const QString &extensionId, const QVariantMap &defaults) const;
    Result<void> writeConfig(const QString &extensionId, const QVariantMap &config);
};
```

- `set` 的键长度检查（256）、8 MiB 限制和错误文本来自 `HostContext::storageSet`；`get` 的键检查来自 `storageGet`（`Invalid storage key` 由调用方保留在 `HostContext` 中或在此返回 `Result<QVariant>`，二选一，错误文本不变）。
- `readConfig`、`writeConfig` 来自 `PluginManager::readConfig()` 和 `setConfig()` 的文件部分；`setConfig()` 中“大小 256 KiB、必须是对象”的校验与通知运行中扩展的逻辑留在 `PluginManager`。
- `startOne()` 中读取 `storage.json` 的代码改为 `m_storage.load(id)`；`Run::storage` 删除。

### 4B.3 `SecretVault`

`git mv` `extension_secrets.{h,cpp}` 到 `capabilities/`（`extensionSecret()` 函数保留）。在同一头文件新增：

```cpp
/// Per-extension keyring access plus the values to redact from logs.
class SecretVault {
  public:
    using Completion = std::function<void(Result<QVariantMap>)>;
    explicit SecretVault(const PackageStore &packages) : m_packages(packages) {}
    void load(const QString &extensionId);   // secret-keys.json
    void get(const QString &extensionId, const QString &key, Completion done);
    void set(const QString &extensionId, const QString &key, const QString &value, Completion done);
    void remove(const QString &extensionId, const QString &key, Completion done);
    /// Deletes every stored secret of an extension (uninstall with clearData).
    void clear(const QString &extensionId, std::function<void(Result<void>)> done);
    QString redact(QString message) const;
};
```

逻辑来自 `HostContext::secret()`、`PluginManager::redact()`、`uninstall()` 中删除密钥的 `sequence(...)`、`startOne()` 中读取 `secret-keys.json` 的代码。`m_secrets` 和 `Run::secretKeys` 删除。

### 4B.4 `ExtensionLog`

文件：`capabilities/extension_log.{h,cpp}`：

```cpp
class ExtensionLog {
  public:
    explicit ExtensionLog(const SecretVault &secrets) : m_secrets(secrets) {}
    void record(const QString &extensionId, const QString &level, const QString &message);
    QVariantList entries(const QString &extensionId) const;   // already redacted
    void remove(const QString &extensionId);
};
```

来自 `PluginManager::recordLog()`（500 条上限、16 KiB 截断不变）和 `request()` 中 `extensions.logs` 分支的脱敏循环。`PluginManager::recordLog()` 保留为一行转发，供 `CapabilityHost` 使用。

### 4B.5 测试

新增 `serviceBusDeliversInOrder`：用 `test.faulty` 发布 `test.host` 三次（`a`、`b`、`c`），断言扩展发出的 `seen` 事件最终为 `{"a","b","c"}`。其余已有测试不改。

### 4B 验证与提交

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
wc -l backend/infrastructure/extensions/plugin_manager.cpp   # 目标 ≤ 750
```

```sh
git add backend tests
git status --short
git commit -m "refactor(extensions): 拆出服务总线、存储、凭据与日志"
```

## 完成标准

- [ ] 4A、4B 两次提交都在全绿状态下完成。
- [ ] 后端源码与测试中不再出现 R6 列出的六个内部方法名。
- [ ] `MusicService`、`ExtensionLyricsProvider`、`BackendSession` 不再通过 `request()` 调用扩展。
- [ ] `PluginManager` 不再有 `providerCall`、`resolveAudio`、`selectShuffle`、`invoke`、`deliver`、`broadcast` 实现与 `m_audio`、`m_logs`、`m_secrets` 成员。
- [ ] 全量测试失败列表不比基线多。
