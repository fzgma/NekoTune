# chore · 工程化与重构

[返回待办总览](README.md) · 更新：2026-10-22

编译检查、CI、测试组织与结构改进；重构随相关功能逐步推进。

保留原条目编号，优先级与工作量说明见[总览](README.md)。

## 清单

| 状态 | 优先级 | 项目 | 分类 | 原估算 |
| --- | --- | --- | --- | --- |
| 待开发 | P1 | 编译警告、Sanitizer 与静态分析 | 工程化 | M |
| 待开发 | P2 | CI 依赖缓存与发布流程加固 | 工程化 | S–M |
| 待开发 | P3 | 测试拆分与覆盖补齐 | 工程化 | M |
| 待开发 | P3 | 超大模块拆分与分层收敛 | 工程化 | L |

## 详细说明

### 11.1 编译警告、Sanitizer 与静态分析 — P1 · M

- 现状：各 CMake 目标没有警告选项；仓库无 `.clang-tidy`、`.clang-format`、`CMakePresets.json`。[bug 9.1、9.4](bug.md) 这类问题本可由 TSan／ASan 暴露。
- 方案：GCC／Clang 开启 `-Wall -Wextra -Wpedantic`（逐步清理后再考虑 `-Werror`）；CI 增加 Linux Debug + ASan／UBSan，播放与扩展相关测试另跑 TSan；增加只检查改动的 clang-tidy；提供 CMakePresets。

### 11.2 CI 依赖缓存与发布流程加固 — P2 · S–M

- `.github/workflows/ci.yml:62-133`：每次从源码构建 Wayland、ECM、LayerShellQt、fcitx5-qt、QtKeychain，未缓存；`:154` 的 ccache 键包含提交 SHA，每次新增缓存挤占配额。按固定依赖版本缓存安装目录，ccache 使用前缀恢复键。
- `.github/workflows/release.yml:31-36`：`gh run list --branch=main` 取 `.[0]`，同一提交重跑 CI 时可能选错；`:68` 产物数量写死为 8。改为按 `headSha` 与成功状态筛选，产物数量从清单推导；后续评估 Windows 代码签名与构建来源证明。
- macOS 构建按产品需要另行评估，不在本项范围。

### 11.3 测试拆分与覆盖补齐 — P3 · M

- `tests/refactor_test.cpp`（约 1800 行）混合 IPC／运行时测试与完整 `Main.qml` 界面测试，失败难定位、无法并行；拆为运行时与界面两个目标，引擎搭建移入 `tests/support/`。`tests/extensions_test.cpp` 同步评估。
- 缺少直接测试：`RecordModel` 对比逻辑（见 [perf 10.2](perf.md)）、酷狗账号会话、`ThemeController`、`backend/ipc/api/*` 参数校验。

### 11.4 超大模块拆分与分层收敛 — P3 · L

- `backend/runtime/library_maintenance.cpp`（约 800 行）直接执行 SQL、注册 IPC 方法并包含 `ipc/api` 头，违反 [架构文档](../architecture.md) 中 runtime 只负责装配与生命周期的约定；拆为 storage 仓储、`application/library` 用例与 `ipc/api` 路由。
- `backend/infrastructure/extensions/plugin_manager.cpp`（约 1850 行）可拆为 SDK 桥接、注册表持久化、包存储、生命周期与依赖图、提供方与音频路由；`:1757` 起的方法分派改为表驱动。
- `player_engine.cpp:160`、`:252`、`:305` 用 `QFileInfo::isFile()` 判断本地／远程来源，文件 I/O 应下沉到 infrastructure。
- 遵循最小改动原则，在相关功能改动时分步进行，不单独大规模重构。
