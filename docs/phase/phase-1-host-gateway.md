# Phase 1：`HostGateway` 修复 `hostCall` 越权

## 目标

1. 扩展经 `IExtensionContext::hostCall` 调用宿主方法前，先经过 `HostGateway` 过滤（决定 R4）。
2. 被拒绝的调用不转给 `IpcRouter`，返回固定错误并写入扩展日志。
3. 删除原来只在激活期间生效的生命周期拦截（被网关完全覆盖）。
4. 补测试和文档。

本阶段独立于后续重构，可以单独合并。

## 前置条件

- Phase 0 已完成。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `backend/infrastructure/extensions/host_gateway.h` | 新建：`checkHostCall()` 声明 |
| `backend/infrastructure/extensions/host_gateway.cpp` | 新建：拒绝规则 |
| `backend/infrastructure/extensions/plugin_manager.cpp` | `Context::hostCall` 改用网关 |
| `backend/CMakeLists.txt` | `nekotune_infrastructure` 加入 `host_gateway.cpp` |
| `tests/native_extensions/faulty/plugin.cpp` | 新增服务 `host`，转发任意 `hostCall` |
| `tests/native_extensions_test.cpp` | 新增 `hostCallGateway` 与 `_data` |
| `docs/extensions.md` | 能力表“调用宿主业务”一行补充限制 |

## 步骤

### 1.1 新建网关

文件：`backend/infrastructure/extensions/host_gateway.h`

```cpp
#pragma once
#include "domain/result.h"
#include <QString>

namespace nekotune {
/// Host IPC methods an extension may reach through hostCall. Trust, lifecycle, appearance and
/// irreversible library operations stay with the user; refused calls never reach the router.
Result<void> checkHostCall(const QString &method);
} // namespace nekotune
```

文件：`backend/infrastructure/extensions/host_gateway.cpp`

```cpp
#include "infrastructure/extensions/host_gateway.h"
#include <QSet>

namespace nekotune {
Result<void> checkHostCall(const QString &method) {
    static const QSet<QString> extensionMethods{"extensions.call", "extensions.list"};
    static const QSet<QString> refused{"library.delete",          "library.migrate", "library.restore",
                                       "library.restore_preview", "library.remove_rollback",
                                       "history.clear",           "ai.config.get",   "ai.config.set",
                                       "ai.config.clear_key"};
    if ((method.startsWith("extensions.") && !extensionMethods.contains(method)) ||
        method.startsWith("appearance.") || refused.contains(method))
        return failure("Host method is not available to extensions: " + method);
    return {};
}
} // namespace nekotune
```

`failure()` 默认错误码是 `ErrorCode::InvalidArgument`，符合 R4。不要新增错误码（R11 在 Phase 5 处理）。

### 1.2 CMake

文件：`backend/CMakeLists.txt`，在 `nekotune_infrastructure` 的源文件列表中，`infrastructure/extensions/extension_secrets.cpp` 之后加入：

```cmake
    infrastructure/extensions/host_gateway.cpp
```

### 1.3 `Context::hostCall`

文件：`backend/infrastructure/extensions/plugin_manager.cpp`

1. 在 include 区，`#include "infrastructure/extensions/extension_secrets.h"` 之后加入：

```cpp
#include "infrastructure/extensions/host_gateway.h"
```

2. 找到 `Context` 中的：

```cpp
    void hostCall(const QString &method, const sdk::Params &params, sdk::Reply done) override {
        auto reply = forward(std::move(done));
        onRun([manager = m_manager, method, params, reply](const std::shared_ptr<Run> &run) {
            // A starting extension holds the mutation queue; queuing behind it would deadlock.
            static const QRegularExpression lifecycle(R"(^extensions\.(install|enable|disable|reload|uninstall)$)");
            if (run->state != "running" && lifecycle.match(method).hasMatch())
                reply(failure("Defer lifecycle mutations until activation has completed"));
            else if (!manager->m_hostCall)
                reply(failure("Host unavailable"));
            else
                manager->m_hostCall(method, params, reply);
        });
    }
```

整体替换为：

```cpp
    void hostCall(const QString &method, const sdk::Params &params, sdk::Reply done) override {
        auto reply = forward(std::move(done));
        onRun([manager = m_manager, method, params, reply](const std::shared_ptr<Run> &run) {
            // Also keeps lifecycle mutations out: a starting extension holds the mutation queue.
            if (const auto allowed = checkHostCall(method); !allowed) {
                manager->recordLog(run->id, "error", allowed.error().message);
                reply(allowed.error());
            } else if (!manager->m_hostCall)
                reply(failure("Host unavailable"));
            else
                manager->m_hostCall(method, params, reply);
        });
    }
```

3. 检查 `QRegularExpression` 是否还有其他用途：

```sh
grep -n 'QRegularExpression' backend/infrastructure/extensions/plugin_manager.cpp
```

`cleanUnusedPackages()` 仍在使用，**保留** `#include <QRegularExpression>`。

### 1.4 测试夹具

文件：`tests/native_extensions/faulty/plugin.cpp`

在 `activate()` 中，`"echo-event"` 服务注册之后、`context.on("test.host", ...)` 之前加入：

```cpp
        m_registrations.push_back(context.registerService("host", [&context](const sdk::Params &params, sdk::Reply reply) {
            context.hostCall(params.value("method").toString(), params.value("params").toMap(), reply);
        }));
```

### 1.5 测试

文件：`tests/native_extensions_test.cpp`

在 `dependenciesBlockAndRecover()` 之前加入：

```cpp
    void hostCallGateway_data() {
        QTest::addColumn<QString>("method");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("player") << "player.pause" << true;
        QTest::newRow("music") << "music.enqueue" << true;
        QTest::newRow("service") << "extensions.call" << true;
        QTest::newRow("list") << "extensions.list" << true;
        QTest::newRow("enable") << "extensions.enable" << false;
        QTest::newRow("install") << "extensions.install" << false;
        QTest::newRow("config") << "extensions.set_config" << false;
        QTest::newRow("select") << "extensions.select" << false;
        QTest::newRow("appearance") << "appearance.select" << false;
        QTest::newRow("restore") << "library.restore" << false;
        QTest::newRow("delete") << "library.delete" << false;
        QTest::newRow("history") << "history.clear" << false;
        QTest::newRow("ai") << "ai.config.get" << false;
    }
    void hostCallGateway() {
        QFETCH(QString, method);
        QFETCH(bool, allowed);
        auto manager = startManager();
        install(*manager, faultyDirectory);
        enable(*manager, "test.faulty");
        m_hostCalls.clear();
        const auto result = call(*manager, "extensions.call",
                                 {{"id", "test.faulty"},
                                  {"service", "host"},
                                  {"params", QVariantMap{{"method", method},
                                                         {"params", QVariantMap{{"id", "test.faulty"}, {"trusted", true}}}}}});
        QCOMPARE(bool(result), allowed);
        const bool forwarded = std::any_of(m_hostCalls.cbegin(), m_hostCalls.cend(),
                                           [&](const auto &host) { return host.first == method; });
        QCOMPARE(forwarded, allowed);
        if (allowed)
            return;
        const auto message = "Host method is not available to extensions: " + method;
        QCOMPARE(result.error().message, message);
        bool logged = false;
        for (const auto &log : call(*manager, "extensions.logs", {{"id", "test.faulty"}}).value().value("logs").toList())
            logged = logged || (log.toMap().value("level") == "error" && log.toMap().value("message") == message);
        QVERIFY(logged);
    }
```

如果文件顶部没有 `#include <algorithm>`，在其他标准库 include 旁边加入。

### 1.6 文档

文件：`docs/extensions.md`

找到：

```markdown
| 调用宿主业务 | `hostCall(method, params, done)`，方法见 [API](API.md) |
```

改为：

```markdown
| 调用宿主业务 | `hostCall(method, params, done)`，方法见 [API](API.md)。扩展管理与授信（`extensions.*`，`extensions.call`、`extensions.list` 除外）、外观选择、曲库删除／迁移／恢复、清空历史与 AI 配置方法会被拒绝 |
```

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen ./build/tests/nekotune_native_extensions_test hostCallGateway
QT_QPA_PLATFORM=offscreen ./build/tests/nekotune_native_extensions_test lyricsAndAutomationExamples
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
grep -n 'Defer lifecycle mutations' backend/infrastructure/extensions/plugin_manager.cpp   # 应无输出
```

## 完成标准

- [ ] `hostCallGateway` 全部数据行通过。
- [ ] `lyricsAndAutomationExamples`（自动化示例的 `player.pause`）仍然通过。
- [ ] 全量测试失败列表不比基线多。
- [ ] 依赖边界检查与 `git diff --check` 通过。

## 提交

```sh
git add backend/infrastructure/extensions/host_gateway.h backend/infrastructure/extensions/host_gateway.cpp \
        backend/infrastructure/extensions/plugin_manager.cpp backend/CMakeLists.txt \
        tests/native_extensions/faulty/plugin.cpp tests/native_extensions_test.cpp docs/extensions.md
git commit -m "fix(extensions): 禁止扩展经 hostCall 授信或执行不可逆操作"
```
