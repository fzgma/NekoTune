# Phase 8：同步文档与验收

## 目标

1. 用户文档、作者文档、接口文档与架构文档改为重构后的现状：SDK 3、`hostCall` 限制、线程归因与看门狗、自动安全模式、`appearance.*`、新的 `extensions.list`。
2. 历史方案文档标注状态。
3. 完成全量构建、测试、SDK 模板独立构建和手动验收，如实记录结果。

## 前置条件

- Phase 7 已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `docs/extensions.md` | 全文按 SDK 3 更新（见 8.1） |
| `docs/API.md` | 「动态扩展接口」「主题与界面」两节，新增 `appearance.*` |
| `docs/architecture.md` | 源码目录、扩展约定、线程与生命周期中涉及扩展的段落 |
| `docs/themes.md`、`docs/theme-boundaries.md`、`docs/wallpaper.md` | `extensions.select` / `extensions.export_theme` → `appearance.*` |
| `docs/audio-engine.md` | `pcmContext()` 用法改为 `context.pcm()` |
| `sdk/README.md`、`sdk/templates/native/README.md` | SDK 3 写法（Phase 5 已改头文件，此处只改文字） |
| `README.md` | 扩展与 SDK 版本相关的段落 |
| `docs/native-extensions-plan.md` | 状态说明补充“SDK 与运行时结构已被重构方案取代” |
| `docs/extensions-refactor-plan.md` | 状态改为“已实施（日期）” |
| `docs/README.md` | 文档导航 |

## 步骤

每一处先用 `grep` 定位，只改与本次重构有关的句子。原文已与源码不一致、且不属于本次重构时，记录下来报告，不要顺手改写。

```sh
grep -rnE 'extensions\.(select|export_theme)|IExtensionContext|registerMusicProvider|registerLyricsProvider|registerShuffleStrategy|registerCommand|registerService|registerUi|shuffleContext|pcmContext|hostCall\(|IExtensionPlugin/2|apiVersion.{0,4}2|SDK 2\.|runtimeReady|"selections"' \
    docs README.md sdk --include=*.md | grep -v 'docs/phase/\|docs/extensions-refactor-plan.md\|docs/native-extensions-plan.md'
```

以上命令的每一行都要在本阶段处理掉，或在报告中说明保留原因（例如描述历史行为）。

### 8.1 `docs/extensions.md`

1. 「目录与清单」：示例清单 `apiVersion` 改为 `3`；字段表 `apiVersion` 一行改为“带原生库的扩展必须为 `3`。纯界面扩展与主题包可写 `1`–`3`”。
2. 「ABI 约定」：IID 改为 `org.nekotune.IExtensionPlugin/3`；补充“SDK 2.x 编译的插件会被拒绝，提示 `Unsupported extension interface org.nekotune.IExtensionPlugin/2`”。
3. 「构建与打包」：`find_package(NekoTuneSDK 2 REQUIRED)` 改为 `3`。
4. 「后台 SDK」：
   - 示例代码改为 SDK 3（`activate(sdk::ExtensionContext &, sdk::Done)`、`context.commands().add(...)`、`context.services().add(...)`、`context.events().on(...)`、`context.host().call(...)`）。
   - 能力表改为按 `context.xxx()` 分组列出：`host`、`events`、`services`、`commands`、`ui`、`music`、`lyrics`、`shuffle`、`pcm`、`config`、`storage`、`secrets`、`tasks`、`log`、`attributeThread()`。
   - “调用宿主业务”一行沿用 Phase 1 补充的限制说明，并写明错误码 `PermissionDenied`。
   - 线程与生命周期约定补充：
     - 自建线程和 `QtConcurrent` 任务在开始时调用 `context.attributeThread()` 并在同一线程销毁返回值，崩溃才能归因到扩展。
     - 后端线程上的单次调用超过 5 秒记录告警；超过 30 秒视为卡死，调用返回后扩展被停用，若始终不返回，下次启动按崩溃停用。随机算法 `select` 超过 200 毫秒记录告警。
     - 加载过扩展代码的会话连续两次异常退出且无法归因时，下次启动自动进入安全模式，管理页显示原因；正常退出后恢复。
   - 删除原“宿主不检测后台卡死”的说法。
5. 「自定义随机算法」：删除 `IShuffleExtensionContext`、`shuffleContext()` 与“SDK 2.1”说明，改为 `context.shuffle().add(id, &provider, {.name = ...})`。
6. 「音乐和歌词来源」：示例改为 `context.music().add("music", &m_provider, {.name = "Example music", .download = true})`；`resolve` 参数改为 `sdk::ResolvePurpose`；歌词改为 `LyricsQuery` / `LyricsCandidate` / `LyricsResolution`，说明 `extra` 存放来源自定义字段、只留在后端。
7. 「播放器 PCM 与频谱扩展」：标题去掉“（SDK 2.2）”，用法改为 `context.pcm().capabilities()` / `subscribe()`。
8. 「QML 界面、菜单和主题」：主题通过「设置 → 外观」选择的说明不变；`ExtensionApi.call("extensions.call", ...)` 的说明不变。
9. 已知限制处补充 R16：已信任扩展的 QML 拿得到完整的 `controllers`，`hostCall` 的限制不约束 QML 侧。

### 8.2 `docs/API.md`

1. 「动态扩展接口」：
   - 方法表删除 `extensions.select`；`extensions.list` 一行改为新结构的概述，并在表后给出 Phase 7「新协议」中的 JSON 示例。
   - “扩展记录包含 ...”一句改为新的扩展条目字段；说明贡献、主题、来源、命令在顶层分组，只有运行中扩展的贡献与注册公开，`themes` 包含未运行扩展的主题（带 `available`）。
   - 补充：`runtime.safeModeReason` 为 `repeated_crashes` 表示自动安全模式。
   - 补充：扩展通过 `hostCall` 调用被拒绝的方法时返回 `PermissionDenied`，列出 R4 的方法范围。
2. 「主题与界面」：所有 `extensions.select` 改为 `appearance.select`，`extensions.export_theme` 改为 `appearance.export_theme`；新增 `appearance.get` 与 `appearance.changed` 事件说明；说明选择保存在 `config/extensions/appearance.json`，旧 `registry.json` 的 `selections` 会在首次启动迁移。

### 8.3 `docs/architecture.md`

1. 源码与构建目标中描述 `backend/infrastructure/extensions` 的位置：改为方案 §3.1 的子目录划分（`package/`、`runtime/`、`capabilities/`、`host_gateway`、`plugin_manager` 门面）以及 `domain/extensions/`、`domain/appearance/`、`infrastructure/appearance/`。
2. 「8. 扩展约定」中新增歌词来源、新增业务功能等段落：如提到 `ExtensionService::sources()`、`provider_call` 或 SDK 2 接口，改为对应的 `IMusicSourceCatalog`、`ILyricsSourceCatalog`、`ShuffleStrategies` 与 SDK 3 写法。
3. 「6. 线程与生命周期」：补充看门狗与线程归因一句，引用 `docs/extensions.md`。

### 8.4 其他文档

- `docs/themes.md`、`docs/theme-boundaries.md`、`docs/wallpaper.md`：`extensions.select` → `appearance.select`，`extensions.export_theme` → `appearance.export_theme`；“选择保存在 `registry.json`”之类的描述改为 `appearance.json`。
- `docs/audio-engine.md`：`pcmContext()` 示例改为 `context.pcm()`。
- `sdk/README.md`：Phase 5 已删除 SDK 2.1/2.2 章节；确认概览、版本号（3.0.0）、`attributeThread()` 说明齐全。
- `sdk/templates/native/README.md`：`find_package` 版本、示例代码、`NEKOTUNE_SDK_TAG` 需指向包含 SDK 3 附件的宿主 Release。
- `README.md`：扩展段落中 SDK 版本与 `apiVersion`。
- `docs/native-extensions-plan.md`：开头状态段落末尾补充“SDK 接口与运行时结构已由[扩展系统重构方案](extensions-refactor-plan.md)（2026-10）取代。”
- `docs/extensions-refactor-plan.md`：状态改为“已实施（<完成日期>），实施步骤见 [phase/README.md](phase/README.md)”。
- `docs/README.md`：导航中 `phase/README.md` 一行确认指向本手册；`extensions-refactor-plan.md` 一行标注“已实施”。

### 8.5 全量验收

```sh
cmake -S . -B build -DNEKOTUNE_ENABLE_WALLPAPER=ON
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/nekotune-extensions-final.txt
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
wc -l backend/infrastructure/extensions/plugin_manager.cpp
find backend/infrastructure/extensions -name '*.cpp' | xargs wc -l | sort -n | tail -15
```

与 `/tmp/nekotune-extensions-baseline.txt` 对比，失败列表不比基线多。

SDK 独立使用：

```sh
cmake --install build --prefix /tmp/nekotune-host
cmake -S sdk/templates/native -B /tmp/nekotune-template -DCMAKE_PREFIX_PATH=/tmp/nekotune-host
cmake --build /tmp/nekotune-template -j2
```

### 8.6 手动验收

使用独立的 `NEKOTUNE_HOME`（例如 `/tmp/nekotune-accept`）和 `NEKOTUNE_SOCKET`，运行 `./build/nekotune`：

| # | 操作 | 期望 |
| --- | --- | --- |
| 1 | 挂载 `build/extensions/example.custom-ui`，信任并启用 | 页面、设置页、菜单、工具栏出现；命令可执行 |
| 2 | 挂载 `example.test-source`，在「扩展音源」搜索、播放、下载 | 播放与下载正常，下载进度事件出现 |
| 3 | 挂载 `example.lyrics`，在歌词搜索中选择示例来源 | 返回示例歌词 |
| 4 | 挂载 `example.shuffle`，在队列页选择“随机 → Avoid consecutive artists” | 算法生效，切换不打断当前歌曲 |
| 5 | 挂载 `example.automation`，触发睡眠定时器命令 | 到时暂停播放（`player.pause` 经 `hostCall` 放行） |
| 6 | 设置 → 外观选择示例主题和界面，重启 | 选择保留；`config/extensions/appearance.json` 存在 |
| 7 | 扩展管理页切换 Shell 插槽，`Ctrl+Shift+E` → 恢复默认界面 | 替换全部清除 |
| 8 | 修改 `example.custom-ui` 源码后重新构建 | 宿主自动重载 |
| 9 | 用 SDK 2 编译的旧插件（`build/tests/native-extensions/test.legacy`）挂载并启用 | 启用失败，管理页显示 `Unsupported extension interface ...IExtensionPlugin/2` |
| 10 | `./build/nekotune --safe-mode` | 扩展代码不加载，数据保留 |

每项记录“通过 / 失败（现象）/ 跳过（原因）”。

## 完成标准

- [ ] “步骤”开头的 `grep` 结果全部处理或说明。
- [ ] 全量测试失败列表不比基线多。
- [ ] SDK 模板独立构建通过，或已记录失败原因。
- [ ] 手动验收表已填写。
- [ ] `plugin_manager.cpp` 最终行数已记录。

## 提交

```sh
git add docs README.md sdk/README.md sdk/templates/native/README.md
git status --short
git commit -m "docs(extensions): 同步 SDK 3、外观接口与运行时保护说明"
```

提交后向用户汇报：每个阶段的提交 SHA、最终测试结果、手动验收表、`plugin_manager.cpp` 行数变化（基线 → 最终），以及执行中所有偏离文档的地方。
