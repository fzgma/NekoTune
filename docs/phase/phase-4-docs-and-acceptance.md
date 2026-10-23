# Phase 4：同步文档与验收

## 目标

1. 用户文档、作者文档和接口文档改为顶层 `wallpaper` 字段，并说明外观页的「壁纸」分类。
2. 写明 `ui.wallpaper` 已废弃以及旧包的修改方法。
3. 完成全量构建、测试和手动验收，如实记录结果。

## 前置条件

- Phase 3 已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `docs/themes.md` | 清单示例、`ui` 字段列表、「主题音乐壁纸」一节、信任说明 |
| `docs/theme-boundaries.md` | §1.1 表格、§5.1 插槽表、§5.3.2、§5.3.3、§5.3.5、§5.3.6、§5.3.7、§5.3.8 |
| `docs/wallpaper.md` | 设置说明与“跟随当前主题”说明 |
| `docs/API.md` | 主题壁纸一段 |
| `docs/extensions.md` | `themes` 贡献说明 |
| `README.md` | 主题壁纸一段 |
| `docs/README.md` | 文档导航中本目录的说明 |

## 步骤

每一处都用给出的原文片段搜索定位。原文已经不一致时，停下来报告，不要猜测着改写。只改与壁纸字段或壁纸分类有关的句子，其余内容保持不变。

### 4.1 `docs/themes.md`

1. 清单示例中，从 `"ui"` 对象删除 `"wallpaper": "Wallpaper.qml",`，并在 `"ui": {` 之前（与 `"ui"` 同级）加入 `"wallpaper": "Wallpaper.qml",`。改完后示例的 JSON 仍须合法。
2. 示例后的说明“`ui`、`tokens` 和 `background` 可省略”改为“`ui`、`wallpaper`、`tokens` 和 `background` 可省略”。
3. 「QML 界面」一节第一句：

   > `ui` 可包含 `background`、`shell`、`bottomPlayer`、`wallpaper` 与 `pages`。

   改为：

   > `ui` 可包含 `background`、`shell`、`bottomPlayer` 与 `pages`；壁纸入口写在与 `ui` 同级的 `wallpaper` 字段（见[主题音乐壁纸](#主题音乐壁纸)）。

4. 「主题音乐壁纸」一节第一段改写为下面的内容（保留原段落中关于切换、更新和每屏实例的句子）：

   > 每个主题可通过顶层字段 `wallpaper` 提供一个完整音乐壁纸布局，值为包内 QML 相对路径，深浅色共用入口，须声明 `schemaVersion: 1`。旧写法 `ui.wallpaper` 已废弃，导入时报错 `Theme wallpaper moved from ui.wallpaper to wallpaper`；把该字段移到与 `ui` 同级即可。
   >
   > 用户可以在「设置 → 外观 → 壁纸」中选择内置样式、“跟随当前主题”（先取当前界面的壁纸，没有再取当前配色的壁纸）或任意主题的壁纸；「设置 → 动态音乐壁纸」中的“壁纸样式”和“主题壁纸来源”是同一份设置。选择未信任的含代码主题壁纸时会先请求授权。两处的选择都不会开启壁纸，开关仍在动态音乐壁纸设置中。

5. 信任段落中的“含 QML/JavaScript 的界面或壁纸在首次加载前需要信任”保持不变；在“一键应用含代码主题仍会请求信任”之后补充“，在外观页的壁纸分类选用含代码主题的壁纸也会请求信任”。

### 4.2 `docs/theme-boundaries.md`

1. §1.1 表格“界面主题”一行的清单字段：删除 `、ui.wallpaper`，改为 `` `ui.shell`、`ui.pages`、`ui.bottomPlayer`、`ui.background`、`wallpaper` ``。
2. §5.1 插槽表 `wallpaper` 一行：清单字段 `` `ui.wallpaper` `` 改为 `` `wallpaper`（顶层） ``；“由谁加载”改为“壁纸窗口；用户须在外观页壁纸分类或壁纸设置中选择主题壁纸”。
3. §5.1 末尾的“安全模式下……不生成任何 `ui` 插槽”改为“不生成任何 `ui` 插槽或主题壁纸入口”。
4. §5.3.2 表格：
   - “界面”一行的来源 `` 主题贡献中的 `ui`（`wallpaper` 除外） `` 改为 `` 主题贡献中的 `ui` ``。
   - “壁纸”一行的来源改为 `` 内置样式、“跟随当前主题”，或指定任意可用贡献的 `wallpaper` ``。
   - 表格后第二条改为：“一个包可以提供配色、界面和壁纸。外观页分为「主题」「壁纸」两类：主题卡片提供整套应用及分项按钮；壁纸分类集中列出内置样式、“跟随当前主题”和所有提供壁纸的主题，与动态音乐壁纸设置读写同一份偏好。”
5. §5.3.3 表格“壁纸”一行：`` 外观选择的 `ui.wallpaper` `` 改为 `` 外观选择的 `wallpaper` ``。
6. §5.3.5：
   - 第二条“壁纸通过独立设置选择”改为“壁纸在外观页壁纸分类或动态音乐壁纸设置中单独选择”。
   - 第四条改为：“选择外观或界面**不得**自动开启壁纸；壁纸分类只切换样式与来源，不改变壁纸开关（§6.2）。”
7. §5.3.6：
   - 第一条中“即使包带有 `ui.wallpaper`”改为“即使包带有 `wallpaper`”。
   - 第三条“壁纸来源列表只公开可用且允许加载的入口，不替用户授权。”改为“在壁纸分类选用含代码主题的壁纸时请求信任，取消不改变壁纸偏好；动态音乐壁纸设置的来源列表只公开可用且允许加载的入口，不替用户授权。”
8. §5.3.7：
   - 表格“壁纸 `style: "theme"`”一行的说明末尾“因此只在 `ui` 中提供壁纸的旧主题继续可用”删除。
   - 表格之后第一条“旧主题包和清单字段**不需要**任何修改即可在新机制下使用。”改为：“除壁纸字段外，旧主题包和清单字段不需要修改。`ui.wallpaper` 已废弃（2026-10），含该字段的包会被拒绝，作者须把它移到顶层 `wallpaper`；已保存的 `wallpaper.style` 与 `wallpaper.themeWallpaper` 偏好不需要迁移。”
9. §5.3.8 第一条末尾“壁纸样式与来源在动态音乐壁纸设置中单独管理。”改为“壁纸样式与来源在外观页壁纸分类或动态音乐壁纸设置中单独管理。”；第三条后补充“恢复窗口不显示壁纸分类。”

### 4.3 `docs/wallpaper.md`

1. 「设置」一段（以“设置页的「动态音乐壁纸」独立组件提供”开头）末尾补充一句：“外观页的「壁纸」分类也可以选择样式与主题壁纸来源，与这里读写同一份设置，但不提供开关。”
2. 以“「跟随当前主题」使用当前界面选择的 `ui.wallpaper`”开头的一段：两处 `ui.wallpaper` 改为 `wallpaper`（顶层字段）。
3. 以“样式保存在 `wallpaper.style`”开头的一段末尾补充：“外观页通过 `WallpaperManager.setOptions()` 一次写入样式与来源，任一值非法时整体拒绝。”

### 4.4 `docs/API.md`

找到以“主题可声明 `ui.wallpaper` QML `Item`”开头的一段：

- `ui.wallpaper` 改为“顶层 `wallpaper`”；
- 在该段末尾补充：“`ui.wallpaper` 已废弃，校验时报错。前端 `ThemeController.selectWallpaper(id, trusted)` 校验并在需要时请求授权，成功后发出 `wallpaperReady(id)`；它不修改 `extensions.select` 的选择，也没有新增 IPC 方法。”

### 4.5 `docs/extensions.md`

`themes` 贡献一行中“可带 `schemaVersion:1`、`variants.light/dark`、`author` 与 `ui`；支持配色、背景、QML 界面及可选 `ui.wallpaper` 音乐壁纸”改为“可带 `schemaVersion:1`、`variants.light/dark`、`author`、`ui` 与 `wallpaper`；支持配色、背景、QML 界面及可选 `wallpaper` 音乐壁纸”。

### 4.6 `README.md`

以“主题包可通过 `ui.wallpaper` 提供完整 QML 音乐壁纸”开头的一段改为：

> 主题包可通过顶层 `wallpaper` 字段提供完整 QML 音乐壁纸（旧写法 `ui.wallpaper` 已废弃）。可在「设置 → 外观 → 壁纸」或动态音乐壁纸设置中选择内置样式、「跟随当前主题」或任意主题的壁纸，主题切换与重新加载立即更新；未提供或加载失败时回退经典卡片并提示，保留原选择。选择主题或壁纸不会自动开启壁纸，安全模式不加载自定义壁纸。

保留原段落末尾指向主题制作指南的链接。

### 4.7 `docs/README.md`

把文档导航中本目录一行说明末尾的“（待实施）”改为“（已实施，步骤保留供追溯）”。

### 4.8 残留检查

```sh
grep -rn 'ui\.wallpaper' README.md docs sdk examples --include='*.md' --include='*.json' --include='*.qml'
grep -rn '"ui", Q[A-Za-z]*{{"wallpaper"' tests backend frontend shared
```

允许出现的结果只有：

- 说明“`ui.wallpaper` 已废弃”的句子；
- `docs/phase/` 中的说明；
- Phase 1 在 `tests/native_extensions_test.cpp` 中故意保留的旧写法断言。

其他结果都要修改；不属于本阶段文件的，停下来报告。

## 验收

### 自动化

```sh
cmake -S . -B build -DNEKOTUNE_ENABLE_WALLPAPER=ON
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/nekotune-wallpaper-final.txt

cmake -S . -B build/wallpaper-off -DNEKOTUNE_ENABLE_WALLPAPER=OFF
cmake --build build/wallpaper-off -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
ctest --test-dir build/wallpaper-off --output-on-failure

cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
```

两套构建的失败列表都不能比 Phase 0 的基线多。`OFF` 构建中外观页仍会拿到 `wallpaperManager`（平台为 unavailable），壁纸分类可以显示，只是壁纸无法开启；这是预期行为。

### 手动

| 检查项 | 预期 |
| --- | --- |
| 导入 `examples/extensions/theme-qml` | 成功；外观页主题卡片显示“QML · 含界面 · 含壁纸” |
| 导入一个仍使用 `ui.wallpaper` 的包 | 失败，错误信息为 `Theme wallpaper moved from ui.wallpaper to wallpaper` |
| 外观 → 壁纸，点示例主题 | 弹出授权；同意后显示“使用中”，壁纸设置的样式和来源同步变化，开关不变 |
| 外观 → 壁纸，点“黑胶唱片” | 样式切换为黑胶，主题来源偏好保留 |
| 开启壁纸后在两处来回切换 | 壁纸内容随之切换，原生窗口不重建（Linux Wayland 下检查 `niri msg layers` 或日志） |
| 停用示例主题 | 壁纸分类显示“已选来源（不可用）”，壁纸回退经典卡片；重新启用后自动接回 |
| `Ctrl+Shift+E` 恢复窗口 | 没有「主题」「壁纸」分类按钮 |
| `nekotune --safe-mode` | 壁纸分类中的主题卡片不可点，内置样式可选 |

不能在真机上执行的项目写明“未验证”和原因，**不要**把 offscreen 测试结果写成真机验收结论。

### 记录

在 `docs/wallpaper.md` 的验收记录部分新增一条，写明日期、构建选项、CTest 结果（通过数／总数及跳过项）和手动检查中实际完成的部分。不要修改已有的验收记录。

## 完成标准

- [ ] 4.1–4.8 全部完成，残留检查只剩允许的结果。
- [ ] `ON`、`OFF` 两套构建的 CTest 都没有新增失败。
- [ ] 手动验收已完成或逐项注明未验证。
- [ ] 验收记录已写入 `docs/wallpaper.md`。

## 提交

```sh
git add README.md docs/README.md docs/themes.md docs/theme-boundaries.md docs/wallpaper.md docs/API.md docs/extensions.md
git commit -m "docs(theme): 同步顶层壁纸字段与外观页壁纸分类"
```
