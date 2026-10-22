# Phase 9：同步文档与完整验收

## 目标

1. 把 Phase 1–8 的行为变化同步到用户文档和开发文档，把已实现的条目从“目标”改为“现状”。
2. 完成全量测试、静态检查和手动冒烟测试，并如实报告结果。

## 前置条件

- Phase 8 已完成并提交。

## 文档规则

- 只描述**已经实现并验证**的行为；没实现的部分保留【目标】标记。
- 不写测试数量、日期等很快会过期的信息，除非文件里本来就有这种验收记录（例如 `docs/wallpaper.md`），并且你确实完成了对应验收。
- 修改后用 `grep` 检查链接锚点是否仍然有效（见 9.6）。

## 步骤

### 9.1 `docs/theme-boundaries.md`

| 位置 | 修改 |
| --- | --- |
| §5.1 当前机制 | 改写第一条：现在外观（`selections.theme`）和界面（`selections.themeLayout`）分开保存，壁纸来源可单独指定；`themeLayout` 不存在时界面跟随外观。插槽解析改为“界面选择 → 独立扩展插槽 → 内置实现”。删除“这种绑定带来的问题……见 §5.3”这句 |
| §5.1 插槽表 | 表头“清单字段”不变；在表格下方补一句：界面插槽来自界面选择的主题，`variants.*.background` 静态背景来自外观选择的主题 |
| §5.3 标题下的【目标】说明 | 改为：§5.3.1–§5.3.8 已实现（标【现状】），§5.3.9 仍为【目标】 |
| §5.3.7 迁移 | 按设计决定 D2 改写：不做一次性迁移；`themeLayout` 键不存在时界面跟随外观选择；新的选择操作总是同时写入两个键。删除“迁移只执行一次，失败时……”这一条 |
| §5.3.4 变量归属表 | 补一句：归属写在 `shared/themes/catalog.json` 每个变量的 `group` 字段中 |
| §5.3.6 信任 | 补充实现方式：`type: "theme"` 的主题包无需信任即可启用；含 QML 的主题包在被信任之前，前端不生成它的任何界面或壁纸入口 |
| §6.1 | 在“含 QML 或 JavaScript 的主题……需要用户确认信任”处改为：首次**加载其界面或壁纸**时需要确认信任；只使用配色不需要 |
| §9 实现对照 | 在 `theme_controller.cpp` 一行的“后续重点”改为 `—`；新增一行 `shared/themes/theme_resolution.*`：前后端共用的变量合成与界面能力判断 |

### 9.2 `docs/themes.md`

| 位置 | 修改 |
| --- | --- |
| 第一段末尾 | 删除“当前主题选择仍同时包含配色和可选界面入口，独立选择外观与布局尚未实现。”，改为一段说明：点击卡片应用整套主题；含界面的主题可以“只用配色”或“只用界面”；配色、界面分别保存，可以分别恢复默认 |
| 第二段 | 补一句：「设置 → 外观」提供“减少动态效果”，所有主题都必须遵守，主题无法更改 |
| 「更新与重新加载」一节 | “新增 QML 权限仍须点击卡片确认信任”改为：更新后新增的 QML 在确认信任前不会加载，配色仍可使用 |
| 「文件格式」一节，在“完整变量、类型、默认值和范围以 catalog.json 为准”之后 | 补一句：每个变量的 `group` 说明它归外观（`appearance`）、界面（`layout`）、共享（`shared`）还是宿主（`host`）；外观主题中的 `layout` 组变量只在搭配内置界面或它自己的界面时生效；`host` 组（`reducedMotion`）由用户设置决定，主题中的值会被忽略 |
| 「QML 界面」一节第二段 | “插槽优先级为当前主题、独立扩展选择、内置实现”改为“插槽优先级为当前界面选择、独立扩展选择、内置实现” |
| 「QML 界面」一节，“含 QML/JavaScript 的主题首次启用需要信任……”这一段 | 改为：主题包无需信任即可启用并提供配色；含 QML/JavaScript 的界面或壁纸在首次加载前需要信任，信任后拥有与现有扩展相同的访问能力。后半段关于“不是安全沙箱”的说明保留 |
| 「主题音乐壁纸」一节第一段 | 补充：选择“跟随当前主题”后，可在“主题壁纸来源”中保持“跟随当前主题”（先取当前界面的壁纸，没有再取当前配色的壁纸），或指定任意已启用主题的壁纸 |
| 「主题音乐壁纸」一节最后一段 | 补充：指定的来源不可用时提示并回退经典卡片，`wallpaper.themeWallpaper` 保留，来源恢复后自动接回 |

### 9.3 `docs/API.md`

找到 `extensions.select` 所在的表格行与“主题”相关的列表，修改：

1. 表格行 `extensions.select` 的参数列改为 `slot,contribution` 或 `selections`，说明列补充：`selections` 是 `{插槽: 贡献 ID}` 对象，全部校验通过后一起保存，任何一项失败都不生效；值未变化的项不重新校验。
2. 在“`slot: "theme"` 接受……”这条之后新增一条：`slot: "themeLayout"` 接受运行中、且声明了 `shell`、`bottomPlayer`、`background` 或非空 `pages` 的主题贡献 ID；空字符串表示内置界面；键不存在时界面跟随 `theme`。删除主题包时，指向它的 `theme`、`themeLayout` 都会重置为空字符串。
3. 修改“主题包沿用 `extensions.enable/...`”这一条：主题包启用不需要 `trusted`；对已运行的主题包调用 `extensions.enable` 并传 `trusted: true` 会记录信任，不会重启。
4. 修改“前端 `controllers.theme` 暴露……”这一段，补充新增的属性和方法：
   - 属性：`layoutSelectedId`、`activeLayoutId`、`layoutUnavailable`、`reducedMotion`、`wallpaperSlots`；`items` 中每项新增 `hasLayout`、`hasWallpaper`。
   - 方法：`selectAppearance`、`selectLayout`、`resetAppearance`、`resetLayout`、`setReducedMotion`；`selectTheme` 对含界面的主题同时设置配色和界面，对不含界面的主题只设置配色。
   - 信号：`trustRequested(id, target)`，`target` 为 `theme`、`appearance` 或 `layout`。
   - 本地设置：`reduced_motion`（布尔）。
5. 修改“主题可声明 `ui.wallpaper`……”这一段：补充 `wallpaper.themeWallpaper`（字符串，空表示跟随），以及 `themeStatus` 新增的 `source-missing`。

### 9.4 `docs/wallpaper.md`

1. 第 19 行附近“样式保存在 `wallpaper.style`……”之后补充：`wallpaper.themeWallpaper` 保存主题壁纸来源，默认空字符串（跟随当前主题），只在 `style: "theme"` 时生效；非法值不会写入。
2. 第 27 行附近“「跟随当前主题」使用当前活动主题的 `ui.wallpaper`”改为：使用当前界面选择的 `ui.wallpaper`，没有时使用当前配色选择的 `ui.wallpaper`；指定来源时使用该主题的入口。未信任的含代码主题不提供壁纸入口。
3. 第 29 行附近 `themeStatus` 的取值列表中加入 `source-missing`（指定的来源不可用）。
4. **不要**修改文件中带日期的历史验收记录。

### 9.5 `docs/extensions.md`

找到第 380 行附近“主题包使用 `type: "theme"`……纯数据主题可以直接启用；含 QML/JavaScript 时按扩展信任流程启用。”改为：主题包无需信任即可启用；含 QML/JavaScript 的界面和壁纸在用户确认信任前不会加载。该段末尾“切勿根据 `themes` 列表直接加载 QML”保留，并补一句：前端只加载 `ThemeController` 生成的、已信任主题的入口。

### 9.6 检查文档链接与残留描述

```sh
grep -rno "theme-boundaries.md#[^)]*" docs README.md AGENT.md
grep -n "^#" docs/theme-boundaries.md
grep -rn "独立选择外观与布局尚未实现\|首次启用需要信任" docs
```

- 第一条输出的每个锚点，都必须能在第二条输出的标题中找到对应项（GitHub 锚点规则：去掉标点，空格换成 `-`，英文转小写）。
- 第三条**应当没有输出**；有输出说明还有旧描述没改。

### 9.7 全量验收

```sh
cmake -S . -B build
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/nekotune-final.txt
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check main...HEAD
```

如果 Qt6 的 `qmllint` 可用：

```sh
qmllint frontend/qt-qml/qml/components/AppearancePanel.qml \
  frontend/qt-qml/qml/components/Theme.qml \
  frontend/qt-qml/qml/components/ExtensionsPanel.qml \
  frontend/qt-qml/qml/wallpaper/Wallpaper.qml \
  frontend/qt-qml/qml/wallpaper/WallpaperSettings.qml
```

对比 `/tmp/nekotune-final.txt` 与 Phase 0 的 `/tmp/nekotune-baseline.txt`：失败列表必须相同或更少。

### 9.8 手动冒烟测试

使用隔离的数据目录，**不要**使用自己的真实配置：

```sh
export NEKOTUNE_HOME=$(mktemp -d)
export NEKOTUNE_SOCKET=$NEKOTUNE_HOME/nekotune.sock
export NEKOTUNE_DB_PATH=$NEKOTUNE_HOME/library.sqlite3
./build/frontend/qt-qml/nekotune
```

可执行文件路径以实际构建产物为准，可以用 `find build -name nekotune -type f -perm -u+x` 查找。

按顺序检查：

1. 「设置 → 外观」显示“当前配色：夜樱”“当前界面：内置界面”。
2. 导入 `examples/extensions/theme-qml`（在扩展页挂载开发目录）。外观页的“薄荷示例 · QML”卡片显示“含界面 · 含壁纸”，以及“只用配色”“只用界面”。
3. 点“只用配色”：**不弹授权**，配色变化，播放栏仍是内置的。
4. 点“只用界面”：弹出授权；取消后什么都不变；再点并确认后，播放栏和背景换成主题的。
5. 点内置“深海”卡片：配色变为深海，主题播放栏**保留**。
6. 点“恢复内置界面”：播放栏回到内置；配色仍为深海。
7. 打开“减少动态效果”，切换歌曲或打开歌词页，确认歌词不再滚动动画；重启应用后开关保持打开。
8. 「设置 → 动态音乐壁纸」：样式选“跟随当前主题”，“主题壁纸来源”选“薄荷示例 · QML”，开启壁纸后显示主题壁纸（平台不支持壁纸时，只确认设置项和提示文字正常）。
9. 按 `Ctrl+Shift+E` 打开恢复窗口的“外观”，能看到同样的分项控件；点“全部恢复默认”后回到夜樱和内置界面。
10. 以 `--safe-mode` 启动：外观为夜樱、界面为内置；“只用界面”按钮不可用；退出后正常启动，原来的选择仍在。

无法执行的步骤（例如没有图形环境）要逐条写明“未执行”及原因。

### 9.9 最终报告

报告必须分别列出：

- 构建结果；
- 全量测试结果，以及与基线的对比；
- 依赖边界检查、`git diff --check`、`qmllint` 结果（有警告就照实列出）；
- Phase 8 的视觉检查结果；
- 9.8 每一步的冒烟结果；
- 偏离阶段文档的地方，以及原因。

## 提交

```sh
git add docs/theme-boundaries.md docs/themes.md docs/API.md docs/wallpaper.md docs/extensions.md
git commit -m "docs(theme): 同步外观、界面与壁纸分开选择"
```

提交前确认没有带入 Phase 0 时工作区里已有的、与本计划无关的改动。
