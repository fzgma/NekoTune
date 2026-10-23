# 扩展系统重构：分阶段实施手册

本目录是[扩展系统重构方案](../extensions-refactor-plan.md)的实施步骤。执行者按阶段顺序完成，每个阶段都能单独构建、测试和提交。

上一轮“主题壁纸独立分类”的手册已经接入源码并删除，需要追溯时查看 git 历史（提交 `d57f608` 起）。

## 1. 背景与目标

现状（2026-10-23 按源码核对）：

- `backend/infrastructure/extensions/plugin_manager.cpp` 约 1890 行，同时负责持久化、安装、依赖、加载、生命周期、SDK Context、服务与事件、音源与下载、随机算法、PCM、外观选择、主题导出、日志、开发目录监听和对外快照。
- 进程内模块之间仍使用 Node 时代的字符串协议：`MusicService` 和 `ExtensionLyricsProvider` 通过 `request("extensions.provider_call" / "extensions.resolve_audio" / ...)` 调用扩展。
- 扩展的 `hostCall` 直接转给 `IpcRouter::dispatch`，激活完成后可以调用 `extensions.enable` 并传 `trusted: true`，绕过用户授信。
- 崩溃归因只覆盖后端线程上的调用；后端卡死无感知。

目标见方案 §1，架构见方案 §3，SDK 3 见方案 §4。

## 2. 执行者必读

### 2.1 总规则

1. **严格按阶段顺序执行**（Phase 0 → Phase 8），不能跳过，也不能合并。每个阶段的“完成标准”全部满足后，才能进入下一阶段。
2. **只改阶段文档列出的文件。** 需要改动未列出的文件时，先停下来说明原因。新建文件以阶段文档的“涉及文件”表为准。
3. **行号只作参考。** 一律用函数名或文中给出的代码片段定位。片段与源码对不上时，停下来报告差异，不要猜测。
4. **Phase 2–4 是行为不变的重构**：IPC 协议、`extensions.list` 字段、`registry.json` 格式、日志与错误文本都不变；已有测试只允许做文档列出的调整（例如改 include 路径、改为直接调用新类）。如果某个已有断言必须改，停下来报告。
5. **不要顺手优化**，也不要改动无关代码的格式、命名或结构。项目约定见仓库根目录 [AGENT.md](../../AGENT.md)。
6. **每个阶段结束都要运行“验证命令”**，并如实报告结果。失败时不能声称完成；跳过的检查要写明“已跳过”。
7. **代码风格与周围代码保持一致：**
   - C++：后端、SDK、示例使用 4 空格缩进；`frontend/qt-qml/src/controllers/theme_controller.cpp` 使用 2 空格缩进。
   - 新类每个文件一个主要类型；头文件 `#pragma once`；命名空间 `nekotune`（SDK 为 `nekotune::sdk`）。
   - 注释：少写，只解释“为什么”。从 `plugin_manager.cpp` 搬出的代码**连同原注释一起搬**。
8. **界面文案**必须同时加到 `frontend/qt-qml/i18n/zh.json` 和 `en.json`，两边的键集合保持一致。
9. **CMake**：新建的 `.cpp` 加入 `backend/CMakeLists.txt` 中原文件所在的目标（`nekotune_infrastructure`、`nekotune_ipc` 等），按目录分组、保持现有顺序风格。
10. **提交**：每个阶段一个提交（Phase 4 分 4A、4B 两次提交），使用文中给出的 Conventional Commits 信息。只提交本阶段的文件，提交前用 `git status` 和 `git diff --cached` 检查。

### 2.2 遇到以下情况必须停止并报告

- 阶段的“前置条件”不满足。
- 文档引用的函数、文件或代码片段不存在，或者与描述明显不同。
- 修改后出现与本阶段无关的测试失败，并且 Phase 0 的基线里没有这个失败。
- Phase 2–4 中需要修改 IPC 协议、`extensions.list` 字段、`registry.json` 格式，或改变已有测试断言。
- 迁移后的文件仍需要 include 违反分层规则的头文件（`tests/dependency_boundaries.cmake` 失败）。

## 3. 已确定的设计决定

以下决定已经定稿，执行时**不得更改**。

| 编号 | 决定 | 原因 |
| --- | --- | --- |
| R1 | 继续在后端进程内用 `QPluginLoader` 加载原生插件，不做进程隔离、沙箱、C ABI | 当前痛点是结构混乱，不是隔离；保留 2026-10-20 迁移的成果 |
| R2 | 代码留在现有分层：业务接口放 `backend/domain/extensions/`，实现放 `backend/infrastructure/extensions/{package,runtime,capabilities}/`，外观实现放 `backend/infrastructure/appearance/` | `tests/dependency_boundaries.cmake` 禁止 application/domain/ipc 依赖 infrastructure |
| R3 | 门面保留类名 `PluginManager` 和文件 `plugin_manager.{h,cpp}`，目标 ≤ 600 行，只编排生命周期 | 减少 `BackendSession`、测试的改名噪音 |
| R4 | `HostGateway` 拒绝下列方法，其余放行：`extensions.*` 中除 `extensions.call`、`extensions.list` 以外的全部方法；`appearance.*`；`library.delete`、`library.migrate`、`library.restore`、`library.restore_preview`、`library.remove_rollback`；`history.clear`；`ai.config.get`、`ai.config.set`、`ai.config.clear_key`。拒绝时返回 `ErrorCode::InvalidArgument`，消息为 `Host method is not available to extensions: <method>`，并写一条 `error` 级扩展日志 | 信任只能由用户在宿主界面授予；不可逆操作和其他凭据不交给扩展。错误码到 SDK 3 再细化（R11） |
| R5 | 扩展状态用 `enum class ExtensionState { Disabled, Blocked, Starting, Running, Stopping, Failed }`，对外字符串保持 `disabled/blocked/starting/running/stopping/failed` | 内部类型安全，协议不变 |
| R6 | 删除内部方法 `extensions.provider_call`、`extensions.resolve_audio`、`extensions.release_audio`、`extensions.download_audio`、`extensions.cancel_download`、`extensions.download_completed`；业务服务改用 `IMusicSourceCatalog`、`ILyricsSourceCatalog` | 方案 §2.1 |
| R7 | 超时常量集中在 `runtime/call_guard.h`：激活 30 s、普通调用 30 s、下载解析 610 s、`downloaded` 120 s，数值不变 | 只换位置，不改行为 |
| R8 | SDK 3：IID `org.nekotune.IExtensionPlugin/3`，`NekoTuneSDK` 版本 `3.0.0`；不兼容 2.x，旧库加载时报 `Unsupported extension interface org.nekotune.IExtensionPlugin/2`。带 `library` 的清单必须 `apiVersion: 3`；纯界面扩展和主题包接受 `1`、`2`、`3` | 方案 §4；主题包不因 SDK 改版被迫改清单 |
| R9 | SDK 3 Context 按能力分组（方案 §4.1），描述符为强类型结构体（§4.2）；`hostCall` 仍是字符串方法 | 方案 §4 |
| R10 | 只迁移仓库内示例、测试夹具和 SDK 模板；外部插件（酷狗、网易云、世界计划）不在范围内 | 用户决定以后重写 |
| R11 | SDK 3 在 `nekotune/result.h` 的 `ErrorCode` 末尾追加 `PermissionDenied`，`HostGateway` 在 Phase 5 改用它 | 追加枚举值放在 ABI 破坏的那次升级里 |
| R12 | 崩溃归因改为 `thread_local`；SDK 3 提供 `sdk::AttributedScope`；启动标记 `config/extensions/session-active` 记录本次加载过代码的扩展，异常退出且无哨兵归因时计数，连续 2 次进入安全模式 | 方案 §3.6 |
| R13 | 看门狗：后端线程上的扩展调用 5 s 告警（`warn` 日志），30 s 写入崩溃哨兵（下次启动按崩溃停用）；随机算法 `select` 超过 200 ms 告警。看门狗不中断线程 | 方案 §3.6 |
| R14 | 外观选择移到 `AppearanceService`，保存到 `config/extensions/appearance.json`；首次启动从 `registry.json` 的 `selections` 迁移，迁移后从 `registry.json` 删除该键。IPC `extensions.select` → `appearance.select`，`extensions.export_theme` → `appearance.export_theme`，新增 `appearance.get` 和事件 `appearance.changed`；旧方法删除，不做别名 | 方案 §3.7；项目未正式发布 |
| R15 | `extensions.list` 改为方案 §3.8 的结构；前端 `ExtensionsController` 的 Q_PROPERTY 名称和 QML 可见行为保持不变 | 协议整理，QML 页面不需要跟着改 |
| R16 | 已信任扩展的 QML 能拿到完整 `controllers`，同一引擎内无法限制；本次不处理，写入文档的已知限制 | 范围控制 |

### 名词对照

| 文档用语 | 代码中的名字 |
| --- | --- |
| 门面 | `PluginManager`（实现 `IExtensionBackend`） |
| 一次运行 | `PluginManager::Run`；Phase 3 后由 `ExtensionRuntime` 表示 |
| 安装记录 | `PluginManager::Entry`；Phase 2 后由 `ExtensionRecord`（持久化部分）表示 |
| 注册 | `Context::registerXxx` / SDK 3 `ctx.xxx().add(...)` 返回的 `RegistrationPtr` |
| 内部方法 | R6 列出的 `extensions.*` 方法，只在后端进程内使用 |
| 公开方法 | `backend/ipc/api/extensions_api.cpp` 注册的 `extensions.*` 方法 |

## 4. 阶段一览

| 阶段 | 文档 | 内容 | 用户可见变化 |
| --- | --- | --- | --- |
| 0 | [phase-0-baseline.md](phase-0-baseline.md) | 建分支、构建、记录测试基线 | 无 |
| 1 | [phase-1-host-gateway.md](phase-1-host-gateway.md) | `HostGateway` 修复 `hostCall` 越权 | 扩展无法再调用被拒绝的方法 |
| 2 | [phase-2-package-layer.md](phase-2-package-layer.md) | 强类型 Manifest、`ExtensionRegistry`、`PackageStore` | 无 |
| 3 | [phase-3-runtime-layer.md](phase-3-runtime-layer.md) | 状态枚举、依赖图、加载器、`CallGuard`、`RegistrationScope`、`DevWatcher` | 无 |
| 4 | [phase-4-capabilities.md](phase-4-capabilities.md) | 能力注册表，删除内部字符串方法 | 无 |
| 5 | [phase-5-sdk3.md](phase-5-sdk3.md) | SDK 3，迁移示例、夹具、模板 | 旧插件需要重新构建 |
| 6 | [phase-6-crash-watchdog.md](phase-6-crash-watchdog.md) | 线程归因、启动标记、看门狗 | 卡死与无法归因的崩溃可恢复 |
| 7 | [phase-7-appearance-and-snapshot.md](phase-7-appearance-and-snapshot.md) | `AppearanceService`、新的 `extensions.list` | 无（协议变化，界面行为不变） |
| 8 | [phase-8-docs-and-acceptance.md](phase-8-docs-and-acceptance.md) | 同步文档，完整验收 | 无 |

每个阶段都依赖前一个阶段。Phase 1 可以单独合并发布；Phase 2–7 之间不要发布。

## 5. 通用命令

在仓库根目录执行，构建目录统一为 `build`。

```sh
# 配置与构建
cmake -S . -B build -DNEKOTUNE_ENABLE_WALLPAPER=ON
cmake --build build -j2

# 全量测试
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
ctest --test-dir build --output-on-failure

# 单个 C++ 测试函数
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
./build/tests/nekotune_native_extensions_test 测试函数名

QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
./build/tests/nekotune_extensions_test 测试函数名

# 单个 QML 测试文件
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Fusion \
./build/tests/nekotune_slider_quick_test -input tests/qml/tst_文件名.qml

# 依赖边界与空白检查
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check

# 门面行数（Phase 3 起每阶段记录）
wc -l backend/infrastructure/extensions/plugin_manager.cpp
```

测试可执行文件的实际路径以 `build/tests/` 下的产物为准；找不到时用 `find build -name 'nekotune_*_test' -type f` 查找。

## 6. 关键源码索引

| 文件 | 本计划中的作用 |
| --- | --- |
| `backend/infrastructure/extensions/plugin_manager.{h,cpp}` | 被拆分的门面；`Context`、`Run`、`Entry`、`request()` 分派 |
| `backend/infrastructure/extensions/extension_manifest.{h,cpp}` | 清单解析，Phase 2 改为强类型 |
| `backend/infrastructure/extensions/audio_proxy.{h,cpp}` | 在线音频代理与下载，Phase 4 移入 `capabilities/` |
| `backend/infrastructure/extensions/crash_guard.{h,cpp}` | 崩溃哨兵，Phase 3 移入 `runtime/`，Phase 6 改为线程归因 |
| `backend/infrastructure/extensions/extension_lyrics_provider.{h,cpp}` | 歌词来源适配，Phase 4 改用 `ILyricsSourceCatalog` |
| `backend/domain/extensions/extension_backend.h` | `IExtensionBackend` |
| `backend/application/extensions/{extension_service.h,music_service.{h,cpp}}` | Phase 4 改用 `IMusicSourceCatalog` |
| `backend/application/playback/shuffle_strategy_service.{h,cpp}` | `setCatalogue(catalogue, Selector)` |
| `backend/runtime/backend_session.{h,cpp}` | 组装：`m_extensionBackend.start(...)`、歌词来源同步、随机目录同步 |
| `backend/ipc/api/extensions_api.cpp` | 公开 `extensions.*` 方法表 |
| `sdk/include/nekotune/sdk/*.h`、`sdk/include/nekotune/result.h` | SDK 头文件，Phase 5 重写 |
| `sdk/CMakeLists.txt`、`sdk/cmake/NekoTuneExtension.cmake` | SDK 版本与 `apiVersion` 写入 |
| `examples/extensions/*`、`sdk/templates/native/*` | Phase 5 迁移 |
| `tests/native_extensions_test.cpp`、`tests/native_extensions/*` | 后端扩展测试与夹具 |
| `tests/extensions_test.cpp` | 前后端联合测试 |
| `frontend/qt-qml/src/controllers/{extensions_controller,theme_controller}.{h,cpp}` | Phase 7 适配新协议 |
