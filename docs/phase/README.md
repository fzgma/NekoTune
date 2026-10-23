# 主题壁纸独立分类：分阶段实施手册

本目录是“主题壁纸成为独立分类”的实施步骤，承接[主题边界规范 §5.3](../theme-boundaries.md#53-开发方向外观界面壁纸分开选择)。执行者按阶段顺序完成，每个阶段都能单独构建、测试和提交。

上一轮“外观、界面、壁纸分开选择”（原 Phase 0–9）已经接入源码，手册已删除，需要追溯时查看 git 历史（提交 `0381c4f` 起）。

## 1. 背景与目标

现状（2026-10-23 按源码核对）：

- 主题通过 `ui.wallpaper` 声明壁纸入口，和 `ui.shell`、`ui.bottomPlayer` 等界面入口放在一起；`theme_package.cpp` 的 `uiSlots` 也把 `wallpaper` 当作界面插槽。
- 壁纸来源只能在「设置 → 动态音乐壁纸 → 主题壁纸来源」里选择，外观页只用卡片上的“含壁纸”标签提示。
- 未信任的含代码主题不会出现在壁纸来源列表里，用户无法从这里授权它。

目标：

1. **清单**：壁纸从 `ui` 中拆出来，成为主题贡献的顶层字段 `wallpaper`，和 `variants`（配色）、`ui`（界面）并列。
2. **外观页**：新增「壁纸」分类，与现有的主题卡片列表分开显示，集中选择内置壁纸样式、“跟随当前主题”和任意主题的壁纸。
3. 「设置 → 动态音乐壁纸」保留现有控件，两处读写同一份设置。

## 2. 执行者必读

### 2.1 总规则

1. **严格按阶段顺序执行**（Phase 0 → Phase 4），不能跳过，也不能合并。每个阶段的“完成标准”全部满足后，才能进入下一阶段。
2. **只改阶段文档列出的文件。** 如果需要改动没有列出的文件，先停下来说明原因。
3. **行号只作参考。** 一律用函数名或文中给出的代码片段搜索定位。片段与源码对不上时，停下来报告差异，不要猜测。
4. **不要顺手重构**，也不要改动无关代码的格式、命名或结构。项目约定见仓库根目录 [AGENT.md](../../AGENT.md)。
5. **每个阶段结束都要运行该阶段的“验证命令”**，并如实报告结果。失败时不能声称完成；跳过的检查要写明“已跳过”。
6. **代码风格与周围代码保持一致：**
   - C++：后端与 `frontend/qt-qml/src/wallpaper/` 使用 4 空格缩进；`frontend/qt-qml/src/controllers/theme_controller.cpp` 使用 2 空格缩进。
   - QML：4 空格缩进；属性多的组件沿用文件里已有的“分号分隔写在一行”风格。
   - 注释：少写，只解释“为什么”。
7. **界面文案**必须同时加到 `frontend/qt-qml/i18n/zh.json` 和 `en.json`，两边的键集合保持一致。
8. **提交**：每个阶段一个提交，使用文中给出的 Conventional Commits 信息。只提交本阶段的文件，提交前用 `git status` 和 `git diff --cached` 检查。

### 2.2 遇到以下情况必须停止并报告

- 阶段的“前置条件”不满足。
- 文档引用的函数、文件或代码片段不存在，或者与描述明显不同。
- 修改后出现与本阶段无关的测试失败，并且 Phase 0 的基线里没有这个失败。
- 需要修改 IPC 协议、后端 `selections` 键或设置键，但文档里没有写到。

## 3. 已确定的设计决定

以下决定已经定稿，执行时**不得更改**。

| 编号 | 决定 | 原因 |
| --- | --- | --- |
| W1 | 主题贡献新增顶层字段 `wallpaper`：值是包内 `.qml` 相对路径（字符串），根对象继承 `Item`；必须同时声明 `schemaVersion: 1` | 壁纸与配色、界面并列，成为独立分类 |
| W2 | **废弃 `ui.wallpaper`，不做兼容读取。** 校验遇到 `ui.wallpaper` 时报错 `Theme wallpaper moved from ui.wallpaper to wallpaper`，旧包需要作者修改后重新导入 | 项目尚未正式发布；明确的报错比静默兼容更容易排查 |
| W3 | `providesWallpaper()` 改为读取顶层 `wallpaper`；`providesLayout()` 不变（壁纸本来就不算界面） | 共享判断只改一处 |
| W4 | 壁纸选择**继续保存在前端设置** `wallpaper.style` 与 `wallpaper.themeWallpaper`，语义不变：`themeWallpaper` 为 `""` 表示跟随当前主题，否则是主题贡献 ID，只在 `style: "theme"` 时生效。后端 `selections` 不新增键，IPC 不变 | 壁纸是前端功能；保持旧配置有效，不需要迁移 |
| W5 | “跟随当前主题”的规则不变：先取界面选择的 `wallpaper`，没有时再取外观选择的 `wallpaper`，都没有就回退经典卡片 | 只换字段位置，不改变行为 |
| W6 | 外观页新增「主题」「壁纸」两个分类标签。「主题」就是现有页面内容，保持不变；「壁纸」依次列出：三个内置样式（经典、黑胶、全屏歌词）、“跟随当前主题”、每个提供壁纸的主题 | 壁纸集中在一处选择，不打乱现有主题卡片 |
| W7 | 在「壁纸」分类中点选**只修改样式和来源**，**不改变 `enabled`**：内置样式只写 `style`，并保留 `themeWallpaper`；“跟随当前主题”写 `style: "theme"` 和 `themeWallpaper: ""`；具体主题写 `style: "theme"` 和该主题 ID。两个键必须在一次保存中写入 | 开关壁纸仍然归用户在壁纸设置里决定（§6.2）；一次写入避免只保存了一半 |
| W8 | `WallpaperManager` 新增 `Q_INVOKABLE bool setOptions(const QVariantMap &values)`：全部校验通过后一次保存，任何一项非法就整体拒绝；`setOption` 改为调用它 | W7 需要原子写入；校验和保存只保留一份实现 |
| W9 | 选择未信任的含代码主题壁纸时，复用现有的信任对话框，`target` 为 `"wallpaper"`。`ThemeController::selectWallpaper(id, trusted)` 负责校验和授权，成功后发出信号 `wallpaperReady(id)`，由外观页写入壁纸设置 | `ThemeController` 不持有 `WallpaperManager`，用信号避免新增依赖；信任流程和其他选择保持一致 |
| W10 | 外观页新增可选属性 `wallpaper`（`WallpaperManager` 或 `null`）。为 `null` 时（未注入 `wallpaperManager`，例如部分测试或恢复窗口）不显示「壁纸」标签。安全模式下禁用主题壁纸卡片，内置样式仍然可以选 | 外观页在恢复窗口中也会被复用 |

### 名词对照

| 文档用语 | 代码中的名字 |
| --- | --- |
| 提供壁纸 | 主题贡献顶层 `wallpaper` 是非空字符串；用 `providesWallpaper()` 判断，前端条目字段为 `hasWallpaper` |
| 跟随当前主题 | `wallpaper.style === "theme"` 且 `wallpaper.themeWallpaper === ""`；入口来自 `ThemeController::uiSlots().wallpaper` |
| 指定来源 | `wallpaper.themeWallpaper` 为主题 ID；入口来自 `ThemeController::wallpaperSlots()[id]` |
| 可用的壁纸 | 主题 `available` 为真，并且 `codeAllowed()` 为真（无代码或已信任） |

## 4. 阶段一览

| 阶段 | 文档 | 内容 | 用户可见变化 |
| --- | --- | --- | --- |
| 0 | [phase-0-baseline.md](phase-0-baseline.md) | 建分支、构建、记录测试基线 | 无 |
| 1 | [phase-1-manifest-wallpaper.md](phase-1-manifest-wallpaper.md) | 清单新增顶层 `wallpaper`，废弃 `ui.wallpaper`，更新示例 | 旧写法的主题导入时报错 |
| 2 | [phase-2-frontend-wallpaper.md](phase-2-frontend-wallpaper.md) | 前端读取新字段；`setOptions`；`selectWallpaper` 与信任 | 无（入口在 Phase 3） |
| 3 | [phase-3-appearance-wallpaper.md](phase-3-appearance-wallpaper.md) | 外观页「壁纸」分类 | 外观页新增壁纸标签 |
| 4 | [phase-4-docs-and-acceptance.md](phase-4-docs-and-acceptance.md) | 同步文档，完整验收 | 无 |

每个阶段都依赖前一个阶段。

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

QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
./build/tests/nekotune_wallpaper_test 测试函数名

# 单个 QML 测试文件（所有 tests/qml/tst_*.qml 都由 nekotune_slider_quick_test 运行）
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Fusion \
./build/tests/nekotune_slider_quick_test -input tests/qml/tst_文件名.qml

# 依赖边界与空白检查
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
```

测试可执行文件的实际路径以 `build/tests/` 下的产物为准；找不到时用 `find build -name 'nekotune_*_test' -type f` 查找。

## 6. 关键源码索引

| 文件 | 本计划中的作用 |
| --- | --- |
| `backend/infrastructure/extensions/theme_package.cpp` | `validate()` 校验主题；`uiSlots` 列表；`exportTheme()` 的 `includesQml` |
| `shared/themes/theme_resolution.{h,cpp}` | `providesLayout()`、`providesWallpaper()` |
| `examples/extensions/theme-qml/extension.json` | 含壁纸的示例主题 |
| `frontend/qt-qml/src/controllers/theme_controller.{h,cpp}` | `rebuild()` 生成 `uiSlots.wallpaper` 与 `wallpaperSlots`；选择与信任流程 |
| `frontend/qt-qml/src/wallpaper/wallpaper_manager.{h,cpp}` | 壁纸设置的默认值、校验和保存 |
| `frontend/qt-qml/qml/components/AppearancePanel.qml` | 外观页（`SettingsPage.qml` 和恢复窗口 `ExtensionsWindow.qml` 都会复用） |
| `frontend/qt-qml/qml/pages/SettingsPage.qml` | 创建 `AppearancePanel`，可以拿到 `wallpaperManager` |
| `frontend/qt-qml/qml/wallpaper/WallpaperSettings.qml` | 壁纸设置；内置样式名称和主题来源列表 |
| `tests/native_extensions_test.cpp` | `themeUiCapabilities`、`themeLayoutSelection` 等后端测试 |
| `tests/extensions_test.cpp` | `wallpaperOnlyThemeDoesNotRequestTrust`、`themeCodeLoadsOnlyAfterTrust` 等联合测试 |
| `tests/wallpaper_test.cpp` | `publishTheme()` 辅助函数与主题壁纸测试 |
| `tests/qml/tst_appearance_panel.qml`、`tst_wallpaper_settings.qml` | QML 测试 |
