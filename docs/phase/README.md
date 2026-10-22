# 外观、界面、壁纸分开选择：分阶段实施手册

本目录是 [主题边界规范 §5.3](../theme-boundaries.md#53-开发方向外观界面壁纸分开选择) 的实施步骤。执行者按阶段顺序完成，每个阶段都能单独构建、测试和提交。

## 1. 执行者必读

### 1.1 总规则

1. **严格按阶段顺序执行**（Phase 0 → Phase 9）。不能跳过阶段，也不能合并阶段。每个阶段的“完成标准”全部满足后，才能进入下一阶段。
2. **只改阶段文档列出的文件。** 如需改动未列出的文件，先停下来说明原因。
3. **行号只作参考。** 文档中的行号基于编写时的代码，执行时一律**用函数名或文中给出的代码片段搜索定位**。代码片段和文档描述对不上时，停下来报告差异，不要猜测。
4. **不要顺手重构。** 不改无关代码的格式、命名或结构。项目约定见仓库根目录 [AGENT.md](../../AGENT.md)。
5. **每个阶段结束都要运行该阶段的“验证命令”**，并如实报告结果。失败时不能声称完成；跳过的检查要写明“已跳过”。
6. **代码风格与周围代码保持一致：**
   - C++：后端 4 空格缩进；`frontend/qt-qml/src/controllers/theme_controller.cpp` 使用 2 空格缩进。改哪个文件就沿用哪个文件的风格。
   - QML：4 空格缩进，属性多的组件沿用文件中已有的“分号分隔写在一行”风格。
   - 注释：少写，只解释“为什么”。
7. **界面文案**必须同时加到 `frontend/qt-qml/i18n/zh.json` 和 `frontend/qt-qml/i18n/en.json`，两边键集合保持一致。
8. **提交**：每个阶段一个提交，使用文中给出的 Conventional Commits 信息。只提交本阶段文件，不要把工作区里原有的其他改动带进去（提交前用 `git status` 和 `git diff --cached` 检查）。

### 1.2 遇到以下情况必须停止并报告

- 阶段的“前置条件”不满足。
- 文档引用的函数、文件或代码片段不存在，或与描述明显不同。
- 修改后出现与本阶段无关的测试失败，且在修改前基线（Phase 0 的记录）中并不存在。
- 需要修改 IPC 协议、配置键或清单字段，但文档没有写到。

## 2. 已确定的设计决定

以下决定已经定稿，执行时**不得更改**。

| 编号 | 决定 | 原因 |
| --- | --- | --- |
| D1 | 外观选择继续使用 `registry.json` 中 `selections` 的 `theme` 键 | 旧配置不需要迁移 |
| D2 | 界面选择使用新键 `selections.themeLayout`：**键不存在**表示“跟随外观选择”（旧行为）；`""` 表示内置界面；其他值是主题贡献 ID | 旧用户升级后效果不变，不需要一次性迁移脚本 |
| D3 | 新的前端选择接口**每次都同时写入 `theme` 和 `themeLayout` 两个键** | 用户一旦使用新界面，`themeLayout` 就变成显式值，“只改一项”的语义才可靠 |
| D4 | `extensions.select` 新增批量写法 `{selections: {slot: contribution, ...}}`，全部校验通过后一次性保存，任何一项失败就全部不生效；旧写法 `{slot, contribution}` 保持兼容 | 两个键必须原子更新 |
| D5 | 变量归属写在 `shared/themes/catalog.json` 每个变量的 `group` 字段，取值为 `appearance`、`layout`、`shared`、`host` | 归属表与变量定义放在一起维护 |
| D6 | 前后端共用一份变量合成实现：新静态库 `nekotune_theme_resolution`，源码在 `shared/themes/theme_resolution.{h,cpp}` | 消除两份实现不一致的风险 |
| D7 | `reducedMotion` 改为用户设置，`AppPaths` 键为 `reduced_motion`（布尔，默认 `false`），主题无法覆盖 | 无障碍偏好归用户 |
| D8 | `type: "theme"` 的主题包启用时**不再需要信任**；含 QML 的主题包在被信任之前，前端**不加载**它的任何 QML 入口。普通扩展的信任规则不变 | 纯配色选择不需要代码授权（§5.3.6） |
| D9 | 壁纸“跟随当前主题”的含义：先取界面选择的 `ui.wallpaper`，没有再取外观选择的 `ui.wallpaper` | 兼容只在 `ui` 中提供壁纸的旧主题 |
| D10 | 壁纸可以指定具体来源：壁纸设置新键 `themeWallpaper`（字符串，`""` 表示跟随当前主题，其他值是主题贡献 ID），只在 `style: "theme"` 时生效 | 允许 A 主题配 B 包的壁纸 |
| D11 | 点击**不含界面**的主题卡片（`selectTheme`）只改变配色，保留当前界面；点击含界面的卡片同时设置配色和界面 | §5.3.5“只提供一项的卡片只改变对应选项”。现有测试 `themePackagesAndAppearance` 中有一条断言依赖旧行为，Phase 4 的 4.10 专门处理 |

### 名词对照

| 文档用语 | 代码中的名字 |
| --- | --- |
| 外观选择 | `selections.theme`；前端 `ThemeController::selectedId()`、`activeId()` |
| 界面选择 | `selections.themeLayout`；前端 `layoutSelectedId()`、`activeLayoutId()` |
| 提供界面 | 主题的 `ui` 中至少有 `shell`、`bottomPlayer`、`background` 之一，或 `pages` 非空；用 `providesLayout()` 判断 |
| 提供壁纸 | 主题的 `ui.wallpaper` 是非空字符串；用 `providesWallpaper()` 判断 |

## 3. 阶段一览

| 阶段 | 文档 | 内容 | 用户可见变化 |
| --- | --- | --- | --- |
| 0 | [phase-0-baseline.md](phase-0-baseline.md) | 建分支、构建、记录测试基线 | 无 |
| 1 | [phase-1-token-groups.md](phase-1-token-groups.md) | `catalog.json` 为每个变量标注 `group` | 无 |
| 2 | [phase-2-shared-resolver.md](phase-2-shared-resolver.md) | 抽出共享的变量合成库，前后端改用它 | 无 |
| 3 | [phase-3-backend-layout-selection.md](phase-3-backend-layout-selection.md) | 后端支持 `themeLayout` 和批量选择 | 无 |
| 4 | [phase-4-frontend-layout-resolution.md](phase-4-frontend-layout-resolution.md) | 前端分开解析外观与界面，提供新的选择接口 | 无（界面入口在 Phase 8） |
| 5 | [phase-5-reduced-motion.md](phase-5-reduced-motion.md) | 减少动态效果改为用户设置 | 外观页新增开关 |
| 6 | [phase-6-per-contribution-trust.md](phase-6-per-contribution-trust.md) | 主题包按用途授权 | 只用配色时不再弹授权 |
| 7 | [phase-7-wallpaper-selection.md](phase-7-wallpaper-selection.md) | 壁纸来源可以单独选择 | 壁纸设置新增来源选项 |
| 8 | [phase-8-appearance-ui.md](phase-8-appearance-ui.md) | 外观页支持分开选择和分项重置 | 外观页改版 |
| 9 | [phase-9-docs-and-acceptance.md](phase-9-docs-and-acceptance.md) | 同步文档，完整验收 | 无 |

依赖关系：每个阶段都依赖前一个阶段。Phase 5、6、7 互相独立，但为了降低合并冲突，仍按顺序执行。

## 4. 通用命令

在仓库根目录执行。构建目录统一为 `build`。

```sh
# 配置与构建
cmake -S . -B build
cmake --build build -j2

# 全量测试
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
ctest --test-dir build --output-on-failure

# 单个 C++ 测试可执行文件中的单个测试函数
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
./build/tests/nekotune_native_extensions_test 测试函数名

QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
./build/tests/nekotune_extensions_test 测试函数名

# 单个 QML 测试文件（所有 tests/qml/tst_*.qml 都由 nekotune_slider_quick_test 运行）
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Fusion \
./build/tests/nekotune_slider_quick_test -input tests/qml/tst_文件名.qml

# 依赖边界与空白检查
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
```

测试可执行文件的实际路径以 `build/tests/` 下的产物为准。如果不在这个位置，用 `find build -name 'nekotune_*_test' -type f` 查找。

## 5. 关键源码索引

| 文件 | 本计划中的作用 |
| --- | --- |
| `shared/themes/catalog.json` | 变量定义、浅色覆盖值、内置主题 |
| `backend/infrastructure/extensions/theme_package.{h,cpp}` | 主题校验、导出；目前包含一份变量合成实现 `resolveTokens` |
| `backend/infrastructure/extensions/plugin_manager.{h,cpp}` | `select()`、`uninstall()`、`enable()`、`install` 的信任处理、`selections` 持久化 |
| `backend/infrastructure/extensions/extension_manifest.h` | `isTheme()`、`needsTrust()` |
| `frontend/qt-qml/src/controllers/theme_controller.{h,cpp}` | 前端主题状态、变量合成、插槽生成、选择操作 |
| `frontend/qt-qml/src/controllers/extensions_controller.{h,cpp}` | `selections()`、`request()` |
| `frontend/qt-qml/src/wallpaper/wallpaper_manager.cpp` | 壁纸设置的默认值与校验 |
| `frontend/qt-qml/qml/components/Theme.qml` | QML 变量单例 |
| `frontend/qt-qml/qml/components/AppearancePanel.qml` | 外观页（恢复窗口 `ExtensionsWindow.qml` 也复用它） |
| `frontend/qt-qml/qml/components/ExtensionsPanel.qml` | 扩展管理页的启用与信任 |
| `frontend/qt-qml/qml/wallpaper/Wallpaper.qml`、`WallpaperSettings.qml` | 壁纸窗口与设置 |
| `tests/native_extensions_test.cpp` | 后端 `PluginManager` 测试，有 `call()`、`entry()`、`startManager()` 等辅助函数 |
| `tests/extensions_test.cpp` | 前后端联合测试，如 `themePackagesAndAppearance`、`themeStartup` |
| `tests/wallpaper_test.cpp`、`tests/qml/tst_wallpaper_settings.qml` | 壁纸测试 |
