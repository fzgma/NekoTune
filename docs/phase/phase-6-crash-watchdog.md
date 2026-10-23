# Phase 6：崩溃归因与看门狗

## 目标

1. 崩溃归因按线程记录（R12）：后端线程和扩展自己的线程都能归因到正确的扩展。
2. SDK 3 提供 `ExtensionContext::attributeThread()`，让扩展为自建线程、`QtConcurrent` 任务标记归属。
3. 启动标记兜底（R12）：加载过扩展代码的会话异常退出、又无法归因时计数，连续 2 次后本次启动自动进入安全模式。
4. 看门狗（R13）：后端线程上的扩展调用超过 5 秒告警，超过 30 秒写入崩溃哨兵；调用最终返回时立即停用该扩展。随机算法 `select` 超过 200 毫秒告警。

## 前置条件

- Phase 5 已完成并提交。

> SDK 3 在 Phase 8 合并前没有发布，本阶段向 `ExtensionContext` 追加虚函数不需要再升级 IID。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `backend/infrastructure/extensions/runtime/crash_guard.{h,cpp}` | `active` 改为 `thread_local`；新增 `writeSentinel`、`clearSentinel` |
| `backend/infrastructure/extensions/runtime/watchdog.{h,cpp}` | 新建 |
| `backend/infrastructure/extensions/runtime/call_guard.h` | `runAttributed` 接入看门狗 |
| `backend/infrastructure/extensions/runtime/session_marker.{h,cpp}` | 新建：启动标记与计数 |
| `backend/infrastructure/extensions/runtime/host_context.{h,cpp}` | 实现 `attributeThread()` |
| `backend/infrastructure/extensions/capabilities/shuffle_strategies.cpp` | 200 ms 告警 |
| `backend/infrastructure/extensions/plugin_manager.{h,cpp}` | 启动、停止、快照接入 |
| `sdk/include/nekotune/sdk/extension_context.h` | 新增 `ThreadAttribution`、`attributeThread()` |
| `examples/extensions/test-source/plugin.cpp` | 若有自建线程，示范 `attributeThread()`（没有则不改） |
| `tests/native_extensions/faulty/plugin.cpp` | 新增服务 `crash-thread`、`block` |
| `tests/native_extensions_test.cpp` | 新增测试 |
| `backend/CMakeLists.txt` | 新文件 |

## 步骤

### 6.1 线程归因

文件：`runtime/crash_guard.cpp`

1. 把

```cpp
char active[128];
std::atomic<int> activeLength{0};
```

改为

```cpp
// Per thread: synchronous fatal signals and the Windows exception filter run on the faulting
// thread, so the handler reads the attribution of the code that crashed. The library is linked
// statically into the executable, so these use static TLS that is safe to read in the handler.
thread_local char active[128];
thread_local int activeLength = 0;
```

   `handler()` 与 `filter()` 中 `activeLength.load()` 改为直接读 `activeLength`；`setActive()` 中的 `store` 改为赋值。`Scope` 的实现不变（构造与析构必须在同一线程，这一点写进头文件注释）。
2. 删除“Only the backend thread writes them.”注释，改为上述新注释。
3. 新增两个普通函数（不在信号处理中使用）：

```cpp
/// Write the sentinel outside a crash handler, e.g. when the watchdog detects a hang.
void writeSentinel(const QString &extensionId);
/// Remove a sentinel written by writeSentinel() once the hang resolved.
void clearSentinel();
```

   使用 `install()` 时保存的路径（另存一份 `QString` 副本），用 `QSaveFile` 写入、`QFile::remove` 删除。

### 6.2 SDK：`attributeThread()`

文件：`sdk/include/nekotune/sdk/extension_context.h`

在 `ExtensionContext` 之前加入：

```cpp
/// While alive, a crash on the creating thread is attributed to this extension. Create it at the
/// start of work on a thread the extension owns (std::thread, QThread, QtConcurrent) and destroy
/// it on the same thread.
class ThreadAttribution {
  public:
    virtual ~ThreadAttribution() = default;
};
```

在 `ExtensionContext` 末尾追加：

```cpp
    /// Mark the calling thread as running this extension. Host callbacks are already attributed.
    virtual std::unique_ptr<ThreadAttribution> attributeThread() = 0;
```

`HostContext` 实现：返回一个持有 `crashguard::Scope` 的 `ThreadAttribution` 子类，构造时传入扩展 ID。注意 `Scope` 不可拷贝，用 `std::optional<crashguard::Scope>` 或直接成员构造。

### 6.3 看门狗

文件：`runtime/watchdog.h`

```cpp
#pragma once
#include <QString>
#include <functional>

class QThread;

namespace nekotune::watchdog {
struct Limits {
    int warnMs = 5'000;
    int hangMs = 30'000;
};
/// Start monitoring extension calls made on thread. onHang runs on the watchdog thread.
void start(QThread *thread, Limits limits, std::function<void(const QString &extensionId)> onHang);
void stop();

/// Tracks one extension call on the monitored thread; no-op elsewhere and when nested.
class Call {
  public:
    explicit Call(const QString &extensionId);
    ~Call();
    /// How long the call ran and whether the watchdog marked it as hung, read after it returned.
    qint64 elapsedMs() const;
    bool hung() const;
  ...
};
} // namespace nekotune::watchdog
```

实现要求（`watchdog.cpp`）：

- 全局状态：被监控线程指针、`std::atomic<qint64>` 开始时间（`steady_clock` 毫秒，0 表示空闲）、当前扩展 ID（受 `std::mutex` 保护）、`std::atomic<bool>` 已告警/已判定卡死标记、嵌套深度（仅被监控线程访问）。
- `start()` 启动一个 `std::thread`，每 1000 ms 被 `std::condition_variable` 唤醒检查一次：
  - 运行时间超过 `warnMs` 且未告警：`qWarning("Extension %s has blocked the backend for %lld ms", ...)`，置已告警。
  - 超过 `hangMs` 且未判定：置已判定，调用 `onHang(id)`。
- `stop()` 通知并 `join` 线程。重复调用安全。
- `Call` 仅在 `QThread::currentThread()` 为被监控线程且嵌套深度为 0 时登记；析构时清除状态，并记录 `elapsedMs`、`hung` 供调用方读取。

文件：`runtime/call_guard.h`，`runAttributed` 改为同时创建 `watchdog::Call`，并把结果交给调用方：

```cpp
struct AttributedOutcome {
    std::optional<QString> error;   // exception message
    qint64 elapsedMs = 0;
    bool hung = false;
};
template <class F> AttributedOutcome runAttributed(const QString &extensionId, F &&call);
```

调用方（`PluginManager::guarded()`、`ShuffleStrategies::select()`）相应改为读取 `outcome.error`。`guarded()` 额外处理：

```cpp
    if (outcome.hung) {
        crashguard::clearSentinel();
        post([this, id, ms = outcome.elapsedMs] {
            fault(id, QStringLiteral("Extension blocked the backend for %1 ms").arg(ms));
        });
        return false;
    }
    if (outcome.elapsedMs > watchdog::Limits{}.warnMs)
        post([this, id, ms = outcome.elapsedMs] {
            recordLog(id, "warn", QStringLiteral("Extension call blocked the backend for %1 ms").arg(ms));
        });
```

`PluginManager::start()` 在 `crashguard::install(sentinel)` 之后调用：

```cpp
    watchdog::start(thread(), {}, [](const QString &id) { crashguard::writeSentinel(id); });
```

`shutdown()` 开头调用 `watchdog::stop()`。

### 6.4 随机算法 200 ms 告警

`ShuffleStrategies::select()` 中，`runAttributed` 返回后：

```cpp
    if (outcome.elapsedMs > 200)
        m_host.recordLog(owner, "warn",
                         QStringLiteral("Shuffle strategy %1 took %2 ms; select must return quickly").arg(id).arg(outcome.elapsedMs));
```

`select()` 只在后端线程同步调用，看门狗的 5 s/30 s 规则同样适用（卡死判定后停用整个扩展）。

### 6.5 启动标记

文件：`runtime/session_marker.h`

```cpp
#pragma once
#include <QStringList>

namespace nekotune {
/// config/extensions/session-active: extensions whose code this session loaded, and how many
/// previous sessions ended abnormally without crash attribution.
class SessionMarker {
  public:
    explicit SessionMarker(QString path) : m_path(std::move(path)) {}
    struct Previous {
        bool ended = true;          // false when the last session did not shut down cleanly
        QStringList loaded;
        int unattributed = 0;       // consecutive unattributed abnormal exits, including the last
    };
    /// Read what the previous session left, then start a new session record.
    Previous begin(bool attributed);
    void loaded(const QString &extensionId);
    /// Clean shutdown: remove the marker and reset the counter.
    void end();

  private:
    QString m_path;
    QStringList m_loaded;
    int m_unattributed = 0;
};
} // namespace nekotune
```

文件格式（JSON，用 `writeJsonFile` 写）：`{"loaded": [...], "unattributed": N}`。

`begin(attributed)` 规则：

1. 文件不存在：上次正常结束，`unattributed = 0`。
2. 文件存在（上次未调用 `end()`）：
   - `attributed`（哨兵里有扩展 ID）或 `loaded` 为空：不计入，`unattributed = 0`。
   - 否则 `unattributed = 文件中的值 + 1`。
3. 写入新的 `{"loaded": [], "unattributed": unattributed}`。

`loaded(id)`：不重复地追加并写盘（每个扩展本会话写一次）。`end()`：删除文件。

`PluginManager` 接入：

- 新成员 `SessionMarker m_session{m_packages.root() + "/session-active"};`（在 `start()` 里构造路径也可以，路径必须遵循 `NEKOTUNE_HOME`）。
- `start()` 中，`takeSentinel` 之后：

```cpp
    const auto previous = m_session.begin(!crashed.isEmpty());
    if (!m_safeMode && previous.unattributed >= 2) {
        m_safeMode = true;
        m_safeModeReason = "repeated_crashes";
        reportError("Extension code was skipped after repeated crashes that could not be attributed: " +
                    previous.loaded.join(", "));
    }
```

- `startOne()` 中，确定要加载原生库（`QPluginLoader::load()` 之前）时调用 `m_session.loaded(id)`。
- `shutdown()` 末尾调用 `m_session.end()`。
- `snapshot()` 增加 `{"safeModeReason", m_safeModeReason}`（未自动进入时为空字符串；环境变量安全模式也为空字符串）。新增成员 `QString m_safeModeReason;`。

被 `SIGKILL` 等外部强制结束的会话同样会计入；只有加载过扩展代码时才计数，误判概率低，且只影响下一次启动。

### 6.6 测试夹具

文件：`tests/native_extensions/faulty/plugin.cpp`，在 Phase 1 新增的 `host` 服务之后加入：

```cpp
        m_registrations.push_back(context.services().add("crash-thread", [&context](const sdk::Params &, sdk::Reply) {
            std::thread([&context] {
                const auto attribution = context.attributeThread();
                std::abort();
            }).detach();
        }));
        m_registrations.push_back(context.services().add("block", [](const sdk::Params &params, sdk::Reply reply) {
            std::this_thread::sleep_for(std::chrono::milliseconds(params.value("ms").toInt()));
            reply(sdk::Params{});
        }));
```

### 6.7 测试

文件：`tests/native_extensions_test.cpp`

1. `crashHandlerWritesSentinel()` 旁新增 `crashOnExtensionThreadIsAttributed()`（仅 Unix，与原测试相同的 `fork` 方式）：子进程启动管理器、启用 `test.faulty`、调用 `crash-thread` 服务；父进程等待子进程因 `SIGABRT` 结束，断言哨兵文件内容为 `test.faulty`。参照 `crashHandlerWritesSentinel` 的结构编写，不要改动原测试。
2. `watchdogStopsBlockingExtension()`：

```cpp
    void watchdogStopsBlockingExtension() {
        auto manager = startManager();
        install(*manager, faultyDirectory);
        enable(*manager, "test.faulty");
        // 31 s exceeds the 30 s hang limit; the call itself still completes.
        call(*manager, "extensions.call", {{"id", "test.faulty"}, {"service", "block"}, {"params", QVariantMap{{"ms", 31'000}}}}, 40'000);
        QTRY_COMPARE_WITH_TIMEOUT(entry(*manager, "test.faulty").value("state").toString(), "failed", 5'000);
        QVERIFY(entry(*manager, "test.faulty").value("error").toString().contains("blocked the backend"));
        QVERIFY(!QFileInfo::exists(QDir(AppPaths::extensionsDirectory()).filePath("crash-sentinel")));
    }
```

   这个测试耗时约 31 秒。如果 `nekotune_native_extensions_test` 有整体超时设置（`set_tests_properties ... TIMEOUT`），确认仍然足够；不够时停止并报告，不要私自缩短限值。
3. `slowCallIsLogged()`：调用 `block` 服务 `ms = 5'500`，断言扩展日志有 `warn` 级 `Extension call blocked the backend for` 消息，且扩展仍为 `running`。
4. `sessionMarkerCountsUnattributedExits()`：直接测试 `SessionMarker`：

```cpp
    void sessionMarkerCountsUnattributedExits() {
        const auto path = QDir(m_home->path()).filePath("session-active");
        SessionMarker first(path);
        QCOMPARE(first.begin(false).unattributed, 0);       // no previous session
        first.loaded("x.one");                              // abnormal exit: end() not called
        SessionMarker second(path);
        auto previous = second.begin(false);
        QVERIFY(!previous.ended);
        QCOMPARE(previous.loaded, QStringList{"x.one"});
        QCOMPARE(previous.unattributed, 1);
        second.loaded("x.one");
        QCOMPARE(SessionMarker(path).begin(true).unattributed, 0);   // attributed crash resets
        SessionMarker clean(path);
        clean.begin(false);
        clean.end();
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(SessionMarker(path).begin(false).unattributed, 0);
    }
```

5. `repeatedUnattributedExitsEnterSafeMode()`：启动前写入 `session-active` 为 `{"loaded":["test.faulty"],"unattributed":1}`（本次启动会计为第 2 次），然后：

```cpp
        const auto marker = QDir(AppPaths::extensionsDirectory()).filePath("session-active");
        // ... write the JSON above to marker (QDir().mkpath first) ...
        auto manager = startManager();
        QVERIFY(manager->snapshot().value("safeMode").toBool());
        QCOMPARE(manager->snapshot().value("safeModeReason").toString(), "repeated_crashes");
        manager->shutdown();
        QVERIFY(!QFileInfo::exists(marker));
```

6. `shuffleFailuresFallback` 等已有测试不改。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen ./build/tests/nekotune_native_extensions_test
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
```

## 完成标准

- [ ] 扩展线程上的崩溃写入正确的扩展 ID；后端线程上的原有归因测试仍通过。
- [ ] 卡死超过 30 秒的调用返回后扩展被停用，哨兵已清除；超过 5 秒有告警日志。
- [ ] 连续两次无法归因的异常退出后自动进入安全模式，快照带 `safeModeReason`。
- [ ] 全量测试失败列表不比基线多。

## 提交

```sh
git add backend sdk tests examples
git status --short
git commit -m "feat(extensions): 按线程归因崩溃并检测阻塞后端的扩展调用"
```
