# perf · 性能优化

[返回待办总览](README.md) · 更新：2026-10-22

来源：2026-10-22 前端代码审查。聚焦 QML 加载、编译与曲库列表性能。

保留原条目编号，优先级与工作量说明见[总览](README.md)。

## 清单

| 状态 | 优先级 | 项目 | 分类 | 原估算 |
| --- | --- | --- | --- | --- |
| 待开发 | P2 | QML 模块化与编译期检查 | 性能 | M–L |
| 待开发 | P2 | 曲库多选与列表模型性能 | 性能 | S–M |
| 待开发 | P2 | 页面按需加载 | 性能 | S |

## 详细说明

### 10.1 QML 模块化与编译期检查 — P2 · M–L

- 现状：`frontend/qt-qml/CMakeLists.txt` 以 `resources.qrc` 打包 QML，`src/main.cpp` 通过 `setContextProperty` 注入 `i18n`、控制器等全局对象；qmlcachegen／qmlsc 不生效，qmllint 无法解析全局名称，绑定只能运行时解释。
- 方案：迁移到 `qt_add_qml_module`，控制器改为 `QML_SINGLETON` 或 required property；CI 增加 `all_qmllint`。
- 注意：迁移会改变 `AGENT.md` 中“新增 QML 同步 `resources.qrc`”的约定，需同步文档。

### 10.2 曲库多选与列表模型性能 — P2 · S–M

- `qml/components/LibraryPanel.qml:52-54`、`:323`、`:330`：每行执行 `selectedSongIds.indexOf`，全选判断为 `every` 套 `indexOf`，选择变化时全部行重算；每行还通过 `songActions(modelData)` 新建菜单数组。改为 C++ 侧 `QSet` 维护选择并以 role 或方法暴露，全选状态由控制器计算，菜单在打开时生成。
- `src/models/record_model.cpp:12-22`（已复核）：常规增量更新接近 O(n)，整体重排（如切换排序）退化为 O(n²)，且每次比较都 `toMap()` 复制。可先建标识到行号的索引，并缓存标识值。
- 验收：数千首曲库切换排序、全选、批量选择不卡顿；用 `QAbstractItemModelTester` 覆盖 `RecordModel` 增删改移。

### 10.3 页面按需加载 — P2 · S

- 现状：`qml/Main.qml:290-297` 与 `qml/shell/PageHost.qml:19-21` 中非调试页面始终 `active`，仅切换可见性，冷启动实例化全部页面。
- 方案：首次访问时激活并保留（维持页面状态），可选异步加载。
