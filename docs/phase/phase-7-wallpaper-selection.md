# Phase 7：壁纸来源单独选择

## 目标

1. 壁纸样式为“跟随当前主题”（`style: "theme"`）时，可以额外指定一个具体来源：任意可用主题的 `ui.wallpaper`（D10）。
2. 不指定来源时保持 Phase 4 的跟随规则：先取界面选择的壁纸，没有再取外观选择的壁纸（D9）。
3. 指定的来源不可用时，显示新的提示，并像现在一样回退到经典卡片；选择本身保留，来源恢复可用后自动接回。

## 前置条件

- Phase 6 已完成并提交（壁纸来源列表要依赖 Phase 6 的 `codeAllowed`）。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `frontend/qt-qml/src/wallpaper/wallpaper_manager.cpp` | 新设置键 `themeWallpaper` 的默认值与校验 |
| `frontend/qt-qml/src/controllers/theme_controller.{h,cpp}` | 新属性 `wallpaperSlots` |
| `frontend/qt-qml/qml/wallpaper/Wallpaper.qml` | 按来源选择壁纸入口，新增状态 `source-missing` |
| `frontend/qt-qml/qml/wallpaper/WallpaperSettings.qml` | 新增来源选择框和提示 |
| `frontend/qt-qml/i18n/zh.json`、`en.json` | 新文案 |
| `tests/wallpaper_test.cpp` | 新增 `themeWallpaperSourceCanDifferFromTheme` |
| `tests/qml/tst_wallpaper_settings.qml` | 模拟对象增加 `wallpaperSlots`，新增测试函数 |

## 步骤

### 7.1 新设置键

文件：`frontend/qt-qml/src/wallpaper/wallpaper_manager.cpp`

1. 在匿名命名空间的 `defaults` 中，把最后一项 `{"style", "classic"}` 改为 `{"style", "classic"}, {"themeWallpaper", ""}`（注意逗号；保持原有的对齐风格即可，不要求严格对齐）。

2. 在 `valid()` 函数中，找到：

```cpp
    if (key == "screenId")
```

在它**之前**加入：

```cpp
    if (key == "themeWallpaper")
        return value.metaType().id() == QMetaType::QString && value.toString().size() <= 200;
```

### 7.2 控制器提供所有可用的壁纸入口

文件：`frontend/qt-qml/src/controllers/theme_controller.h`

1. 在 `Q_PROPERTY(bool reducedMotion ...)` 之后加入：

```cpp
  Q_PROPERTY(QVariantMap wallpaperSlots READ wallpaperSlots NOTIFY changed)
```

2. 在 `bool reducedMotion() const { ... }` 之后加入：

```cpp
  QVariantMap wallpaperSlots() const { return m_wallpaperSlots; }
```

3. 把成员行 `QVariantMap m_catalog, m_active, m_layout, m_tokens, m_background, m_slots;` 改为：

```cpp
  QVariantMap m_catalog, m_active, m_layout, m_tokens, m_background, m_slots,
      m_wallpaperSlots;
```

文件：`frontend/qt-qml/src/controllers/theme_controller.cpp`

在 `rebuild()` 中，找到 Phase 4 写的 `if (!safeMode()) { ... }` 插槽代码块，在这个代码块的右花括号**之后**、`emit changed();` **之前**加入：

```cpp
  // Every usable wallpaper, keyed by theme ID, for an explicit wallpaper source.
  m_wallpaperSlots.clear();
  if (!safeMode())
    for (const auto &value : std::as_const(m_items)) {
      const auto item = value.toMap();
      const auto source =
          item.value("ui").toMap().value("wallpaper").toString();
      if (source.isEmpty() || !item.value("available").toBool() ||
          !codeAllowed(item))
        continue;
      m_wallpaperSlots.insert(
          item.value("id").toString(),
          QVariantMap{{"id", item.value("id").toString() + "/wallpaper"},
                      {"slot", "wallpaper"},
                      {"source",
                       QDir(item.value("directory").toString()).filePath(source)},
                      {"extensionId", item.value("extensionId")},
                      {"generation", item.value("generation")},
                      {"title", item.value("title")}});
    }
```

描述中的 `id`、`source`、`extensionId`、`generation` 与 `uiSlots.wallpaper` 对同一主题生成的值完全相同，所以在“跟随”和“指定同一个来源”之间切换时，壁纸视图不会重建。

### 7.3 壁纸窗口按来源取入口

文件：`frontend/qt-qml/qml/wallpaper/Wallpaper.qml`

找到：

```qml
    readonly property var themeDescriptor: app.theme ? app.theme.uiSlots.wallpaper || ({}) : ({})
    readonly property string themeStatus: style !== "theme" ? ""
        : app.theme && app.theme.safeMode ? "safe-mode"
        : !themeDescriptor.source ? "missing"
```

替换为：

```qml
    // An explicit source wins; an empty value follows the current layout, then appearance.
    readonly property string themeSource: preferences.themeWallpaper || ""
    readonly property var themeDescriptor: !app.theme ? ({})
        : themeSource !== "" ? app.theme.wallpaperSlots[themeSource] || ({})
        : app.theme.uiSlots.wallpaper || ({})
    readonly property string themeStatus: style !== "theme" ? ""
        : app.theme && app.theme.safeMode ? "safe-mode"
        : !themeDescriptor.source ? (themeSource !== "" ? "source-missing" : "missing")
```

`themeStatus` 后面剩余的行（`error`、`active`、`loading` 的判断）保持不变。

然后在同一文件中搜索 `"missing"`，确认没有其他地方需要同步处理 `source-missing`。如果某处把 `"missing"` 当作“使用经典卡片回退”的条件，例如 `themeStatus === "missing"`，就把它改为同时匹配 `"source-missing"`。如果找到这类代码，在报告中列出改动位置。

### 7.4 壁纸设置页增加来源选择

文件：`frontend/qt-qml/qml/wallpaper/WallpaperSettings.qml`

1. 把根对象中的 `themeHint` 属性：

```qml
    readonly property string themeHint: preferences.style !== "theme" || !themeController ? ""
        : themeController.safeMode ? "safe-mode"
        : !((themeController.uiSlots || {}).wallpaper || {}).source ? "missing" : ""
```

替换为：

```qml
    readonly property string themeSource: preferences.themeWallpaper || ""
    readonly property string themeHint: preferences.style !== "theme" || !themeController ? ""
        : themeController.safeMode ? "safe-mode"
        : themeSource !== "" ? (((themeController.wallpaperSlots || {})[themeSource] || {}).source ? "" : "source-missing")
        : !((themeController.uiSlots || {}).wallpaper || {}).source ? "missing" : ""
    readonly property var themeSources: {
        const slots = themeController ? themeController.wallpaperSlots || {} : {}
        const list = [{id: "", label: t("wallpaper_theme_follow")}]
        for (const id of Object.keys(slots))
            list.push({id: id, label: label(slots[id].title) || id})
        if (themeSource !== "" && !slots[themeSource])
            list.push({id: themeSource, label: t("wallpaper_theme_source_unavailable")})
        return list
    }
```

2. 在 `function t(key) { ... }` 之后加入：

```qml
    function label(value) {
        return typeof value === "string" ? value : value ? value[translator.language] || value.en || value.zh || "" : ""
    }
```

3. 在 `themeMessage` 函数的 `keys` 对象中加入一项 `"source-missing": "wallpaper_theme_source_missing"`。修改后：

```qml
        const keys = {missing: "wallpaper_theme_missing", error: "wallpaper_theme_error",
                      "safe-mode": "wallpaper_theme_safe_mode", loading: "wallpaper_theme_loading",
                      "source-missing": "wallpaper_theme_source_missing"}
```

4. 找到 `objectName: "wallpaperStyle"` 的 `ChoiceField`，在它的右花括号**之后**、`objectName: "wallpaperThemeHint"` 的 `Label` **之前**加入：

```qml
        Label {
            visible: themeSourcePicker.visible
            text: root.t("wallpaper_theme_source"); color: Theme.textSecondary
        }
        ChoiceField {
            id: themeSourcePicker
            objectName: "wallpaperThemeSource"
            Layout.fillWidth: true
            visible: root.preferences.style === "theme" && root.themeController !== null
            model: root.themeSources
            textRole: "label"; valueRole: "id"
            currentIndex: Math.max(0, model.findIndex(item => item.id === root.themeSource))
            onActivated: root.manager.setOption("themeWallpaper", currentValue)
        }
```

### 7.5 新增 i18n 键

在两个 i18n 文件中，紧跟 `"wallpaper_theme_loading"` 一行之后加入：

| 键 | zh | en |
| --- | --- | --- |
| `wallpaper_theme_source` | `主题壁纸来源` | `Theme wallpaper source` |
| `wallpaper_theme_follow` | `跟随当前主题` | `Follow current theme` |
| `wallpaper_theme_source_unavailable` | `已选来源（不可用）` | `Selected source (unavailable)` |
| `wallpaper_theme_source_missing` | `所选壁纸来源暂不可用，暂用经典卡片；来源恢复可用后会自动接回。` | `The selected wallpaper source is unavailable. Showing the classic card until it is available again.` |

同时把已有的 `wallpaper_theme_missing` 改为更准确的说法：

| 键 | zh | en |
| --- | --- | --- |
| `wallpaper_theme_missing` | `当前界面和配色都未提供壁纸入口，暂用经典卡片；主题壁纸可用后会自动跟随。` | `Neither the current layout nor the current palette provides a wallpaper. Using the classic card until one is available.` |

完成后运行 Phase 4 中的 JSON 合法性检查命令。

### 7.6 C++ 测试

文件：`tests/wallpaper_test.cpp`，在 `themeWallpaperPreferenceRestoresAndSafeModeNeverLoadsCode` 之后加入：

```cpp
    void themeWallpaperSourceCanDifferFromTheme() {
        qputenv("NEKOTUNE_SAFE_MODE", "0");
        setup();
        send(snapshot());
        QTRY_VERIFY(m_manager->dataReady());
        QTRY_VERIFY(m_controllers->extensions->appearanceReady());
        const auto example = QFINDTESTDATA("../examples/extensions/theme-qml/Wallpaper.qml");
        QVERIFY(!example.isEmpty());
        // The wallpaper package is available, but a built-in palette is the current theme.
        send({{"event", "extensions.changed"},
              {"selections", QJsonObject{{"theme", "builtin/ocean"}}},
              {"extensions", QJsonArray{QJsonObject{
                  {"id", "test.wallpaper"}, {"type", "theme"}, {"state", "running"},
                  {"directory", QFileInfo(example).absolutePath()}, {"generation", 1}, {"trusted", true},
                  {"themes", QJsonArray{QJsonObject{
                      {"id", "test.wallpaper/default"}, {"schemaVersion", 1},
                      {"variants", QJsonObject{{"light", QJsonObject{}}, {"dark", QJsonObject{}}}},
                      {"ui", QJsonObject{{"wallpaper", "Wallpaper.qml"}}}}}}}}}});
        QTRY_VERIFY(m_controllers->theme->wallpaperSlots().contains("test.wallpaper/default"));
        QVERIFY(!m_controllers->theme->uiSlots().contains("wallpaper"));

        QVERIFY(m_manager->setOption("style", "theme"));
        QVERIFY(m_manager->setOption("themeWallpaper", "test.wallpaper/default"));
        QVERIFY(m_manager->setOption("enabled", true));
        QTRY_COMPARE(m_manager->runningCount(), 1);
        QTRY_COMPARE(window()->property("themeStatus").toString(), QString("active"));

        QVERIFY(m_manager->setOption("themeWallpaper", "test.wallpaper/missing"));
        QTRY_COMPARE(window()->property("themeStatus").toString(), QString("source-missing"));
        QVERIFY(m_manager->setOption("themeWallpaper", ""));
        QTRY_COMPARE(window()->property("themeStatus").toString(), QString("missing"));

        QVERIFY(!m_manager->setOption("themeWallpaper", 1));
        QVERIFY(!m_manager->setOption("themeWallpaper", QString(201, QLatin1Char('x'))));
    }
```

如果 `snapshot()`、`send()`、`setup()`、`window()` 的名字与文件中的辅助函数不同，以 `themeWallpaperIsOptInAndReloadsWithoutRecreatingWindow` 中的实际用法为准。

### 7.7 QML 测试

文件：`tests/qml/tst_wallpaper_settings.qml`

1. 在 `mockTheme` 中，`property var uiSlots: ({})` 之后加入：

```qml
        property var wallpaperSlots: ({})
```

2. 在 `function init() { ... }` 中，`mockTheme.uiSlots = ({})` 这一行之后加入：

```qml
        mockTheme.wallpaperSlots = ({})
```

3. 在文件中最后一个 `function test_...` 函数之后加入：

```qml
    function test_themeWallpaperSource() {
        const panel = createTemporaryObject(panelComponent, testCase, {width: 360, themeController: mockTheme})
        verify(panel)
        mockManager.settings = Object.assign({}, mockManager.settings, {style: "classic", themeWallpaper: ""})
        const picker = findChild(panel, "wallpaperThemeSource")
        const hint = findChild(panel, "wallpaperThemeHint")
        verify(picker)
        verify(!picker.visible)

        mockManager.settings = Object.assign({}, mockManager.settings, {style: "theme"})
        mockTheme.wallpaperSlots = {"pkg/wall": {source: "file:///pkg/Wallpaper.qml", title: {en: "Wall", zh: "壁纸"}}}
        verify(picker.visible)
        compare(picker.count, 2)
        compare(picker.currentIndex, 0)

        picker.currentIndex = 1
        picker.activated(1)
        compare(mockManager.lastKey, "themeWallpaper")
        compare(mockManager.settings.themeWallpaper, "pkg/wall")
        verify(!hint.visible)

        mockTheme.wallpaperSlots = ({})
        compare(picker.count, 2)
        compare(hint.text, "wallpaper_theme_source_missing")
    }
```

说明：

- `ChoiceField` 继承自 `ComboBox`，`count`、`currentValue` 和 `activated(index)` 信号都来自 `ComboBox`。`picker.activated(1)` 用来模拟用户选择。
- 来源被选中后又从 `wallpaperSlots` 消失时，列表中保留一项“已选来源（不可用）”，所以数量仍然是 2。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_wallpaper_test
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Fusion ./build/tests/nekotune_slider_quick_test -input tests/qml/tst_wallpaper_settings.qml
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themeCodeLoadsOnlyAfterTrust
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
git diff --check
```

如果 Qt6 的 `qmllint` 可用：

```sh
qmllint frontend/qt-qml/qml/wallpaper/Wallpaper.qml frontend/qt-qml/qml/wallpaper/WallpaperSettings.qml
```

## 完成标准

- [ ] 外观是内置主题时，也能指定某个主题包的壁纸并正常显示。
- [ ] 指定的来源不可用时显示 `source-missing` 提示并回退到经典卡片，选择保留。
- [ ] 来源为空时行为与 Phase 6 完成后一致：所有原有的壁纸测试通过。
- [ ] 安全模式下不加载任何主题壁纸（`themeWallpaperPreferenceRestoresAndSafeModeNeverLoadsCode` 通过）。

## 提交

```sh
git add frontend/qt-qml/src/wallpaper/wallpaper_manager.cpp \
  frontend/qt-qml/src/controllers/theme_controller.h frontend/qt-qml/src/controllers/theme_controller.cpp \
  frontend/qt-qml/qml/wallpaper/Wallpaper.qml frontend/qt-qml/qml/wallpaper/WallpaperSettings.qml \
  frontend/qt-qml/i18n/zh.json frontend/qt-qml/i18n/en.json \
  tests/wallpaper_test.cpp tests/qml/tst_wallpaper_settings.qml
git commit -m "feat(wallpaper): 主题壁纸来源可单独选择"
```
