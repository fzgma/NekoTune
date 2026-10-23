# Phase 2：前端读取新字段并提供壁纸选择接口

## 目标

1. `ThemeController` 从顶层 `wallpaper` 生成“跟随当前主题”和“指定来源”的壁纸入口，规则不变（W5）。
2. `WallpaperManager::setOptions()` 一次保存多个设置键（W8），`setOption()` 改为调用它。
3. 新增 `ThemeController::selectWallpaper(id, trusted)` 和信号 `wallpaperReady(id)`，复用现有信任流程（W9）。
4. 修好 Phase 1 留下的、仍在使用 `ui.wallpaper` 的测试。

本阶段没有界面入口；外观页的「壁纸」分类在 Phase 3 实现。

## 前置条件

- Phase 1 已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `frontend/qt-qml/src/controllers/theme_controller.{h,cpp}` | 读取顶层 `wallpaper`；`selectWallpaper`、`wallpaperReady` |
| `frontend/qt-qml/src/wallpaper/wallpaper_manager.{h,cpp}` | `setOptions` |
| `frontend/qt-qml/i18n/zh.json`、`en.json` | 新文案 `theme_wallpaper_unavailable` |
| `tests/extensions_test.cpp` | 测试主题改用顶层 `wallpaper`；补充 `selectWallpaper` 断言 |
| `tests/wallpaper_test.cpp` | `publishTheme()` 等改用顶层 `wallpaper`；新增 `setOptionsIsAtomic` |

## 步骤

### 2.1 控制器读取顶层字段

文件：`frontend/qt-qml/src/controllers/theme_controller.cpp`

在 `rebuild()` 中做两处替换：

1. “跟随当前主题”入口：

```cpp
      add(wallpaper, "wallpaper",
          wallpaper.value("ui").toMap().value("wallpaper"));
```

改为

```cpp
      add(wallpaper, "wallpaper", wallpaper.value("wallpaper"));
```

2. 指定来源列表：

```cpp
      const auto source =
          item.value("ui").toMap().value("wallpaper").toString();
```

改为

```cpp
      const auto source = item.value("wallpaper").toString();
```

改完后在文件中搜索 `value("wallpaper")`，确认已经没有 `.value("ui").toMap().value("wallpaper")`。

### 2.2 `selectWallpaper` 与 `wallpaperReady`

文件：`frontend/qt-qml/src/controllers/theme_controller.h`

1. 在 `Q_INVOKABLE void selectLayout(...)` 之后加入：

```cpp
  Q_INVOKABLE void selectWallpaper(const QString &id, bool trusted = false);
```

2. 把 `trustRequested` 上方的注释改为：

```cpp
  // target is "theme", "appearance", "layout" or "wallpaper" so the dialog can retry the same action.
```

3. 在 `void trustRequested(...)` 之后加入：

```cpp
  // The wallpaper source is usable; the caller stores it in the wallpaper settings.
  void wallpaperReady(const QString &id);
```

文件：`frontend/qt-qml/src/controllers/theme_controller.cpp`

在 `ThemeController::selectLayout()` 的定义之后加入：

```cpp
void ThemeController::selectWallpaper(const QString &id, bool trusted) {
  if (busy() || safeMode())
    return;
  const auto theme = find(id);
  if (!theme.value("hasWallpaper").toBool()) {
    m_error = "theme_wallpaper_unavailable";
    emit changed();
    return;
  }
  if (!ensureAvailable(theme, trusted, "wallpaper",
                       [this, id] { selectWallpaper(id); }))
    return;
  m_error.clear();
  emit changed();
  emit wallpaperReady(id);
}
```

说明：

- `ensureAvailable()` 对 `"appearance"` 以外的目标都按“需要运行代码”处理，所以未信任的含代码主题会发出 `trustRequested(id, "wallpaper")`；授权后自动重试。**不要修改 `ensureAvailable()`**。
- 本函数不修改 `selections`，也不碰 `WallpaperManager`（W4、W9）。

### 2.3 `WallpaperManager::setOptions`

文件：`frontend/qt-qml/src/wallpaper/wallpaper_manager.h`

在 `Q_INVOKABLE bool setOption(const QString &key, const QVariant &value);` 之后加入：

```cpp
    // Validates every key first and saves them together; one invalid value rejects the whole call.
    Q_INVOKABLE bool setOptions(const QVariantMap &values);
```

文件：`frontend/qt-qml/src/wallpaper/wallpaper_manager.cpp`

把整个 `bool WallpaperManager::setOption(const QString &key, const QVariant &value) { ... }` 替换为下面两个函数。`for (auto &[screen, output] : m_outputs)` 循环体**从原函数原样复制**，下面只写出了开头几行；复制前先核对原函数中这个循环的内容，与下面的开头一致后再替换：

```cpp
bool WallpaperManager::setOption(const QString &key, const QVariant &value) {
    return setOptions({{key, value}});
}

bool WallpaperManager::setOptions(const QVariantMap &values) {
    if (m_stopping || values.isEmpty())
        return false;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        if (!valid(it.key(), it.value()) || (it.key() == "spectrum" && it.value().toBool())) {
            m_error = it.key() == "spectrum" ? "wallpaper_spectrum_unavailable" : "wallpaper_invalid_setting";
            emit changed();
            return false;
        }
    // Always allow immediate shutdown, even when settings storage is unavailable.
    if (values.contains("enabled") && !values.value("enabled").toBool()) {
        m_requested = false;
        reconcile();
    }
    auto next = m_settings;
    next.insert(values);
    if (!nekotune::AppPaths::saveSetting("wallpaper", QJsonObject::fromVariantMap(next))) {
        m_error = "wallpaper_save_failed";
        emit changed();
        return false;
    }
    m_settings = next;
    m_error.clear();
    if (values.contains("enabled"))
        m_requested = values.value("enabled").toBool();
    if (values.contains("enabled") || values.contains("target") || values.contains("screenId")) {
        for (auto &[screen, output] : m_outputs) {
            if (!output.window) {
                output.state = "idle";
                output.error.clear();
                output.attempts = 0;
                output.recoveryDeadline = 0;
            }
        }
        reconcile();
    }
    emit changed();
    return true;
}
```

如果原循环体与上面不同，以原函数为准，并在报告中说明。

### 2.4 文案

`frontend/qt-qml/i18n/zh.json` 与 `en.json` 各加一个键，位置放在 `theme_layout_unavailable` 附近：

| 键 | zh | en |
| --- | --- | --- |
| `theme_wallpaper_unavailable` | 这个主题没有提供壁纸 | This theme does not provide a wallpaper |

用下面的命令确认两边的键集合一致：

```sh
python3 - <<'EOF'
import json
zh = json.load(open("frontend/qt-qml/i18n/zh.json", encoding="utf-8"))
en = json.load(open("frontend/qt-qml/i18n/en.json", encoding="utf-8"))
print(sorted(set(zh) ^ set(en)) or "ok")
EOF
```

### 2.5 测试

文件：`tests/wallpaper_test.cpp`

1. `publishTheme()` 中，把 `{"ui", QJsonObject{{"wallpaper", source}}}` 改为 `{"wallpaper", source}`。
2. `themeWallpaperSourceCanDifferFromTheme()` 中，把 `{"ui", QJsonObject{{"wallpaper", "Wallpaper.qml"}}}` 改为 `{"wallpaper", "Wallpaper.qml"}`。
3. 在 `preferencesValidatePersistAndFailHonestly()` 之后新增：

```cpp
    void setOptionsIsAtomic() {
        setup();
        QVERIFY(m_manager->setOptions({{"style", "theme"}, {"themeWallpaper", "pkg/wall"}}));
        QCOMPARE(m_manager->settings().value("style").toString(), QString("theme"));
        QCOMPARE(m_manager->settings().value("themeWallpaper").toString(), QString("pkg/wall"));

        // One invalid value rejects the whole call and keeps the saved values.
        QVERIFY(!m_manager->setOptions({{"style", "lyrics"}, {"themeWallpaper", 1}}));
        QCOMPARE(m_manager->error(), QString("wallpaper_invalid_setting"));
        QVERIFY(!m_manager->setOptions({{"style", "lyrics"}, {"unknown", true}}));
        QVERIFY(!m_manager->setOptions({}));
        QCOMPARE(m_manager->settings().value("style").toString(), QString("theme"));

        m_manager.reset();
        setup();
        QCOMPARE(m_manager->settings().value("style").toString(), QString("theme"));
        QCOMPARE(m_manager->settings().value("themeWallpaper").toString(), QString("pkg/wall"));
        QVERIFY(!m_manager->requestedEnabled());
    }
```

最后一条确认选择样式不会开启壁纸（W7）。

文件：`tests/extensions_test.cpp`

1. `wallpaperOnlyThemeDoesNotRequestTrust()` 与 `themeCodeLoadsOnlyAfterTrust()` 中，各有一处 `{"ui", QJsonObject{{"wallpaper", "Wallpaper.qml"}}}`，都改为 `{"wallpaper", "Wallpaper.qml"}`。
2. `wallpaperOnlyThemeDoesNotRequestTrust()`：在最后一个 `QCOMPARE(trust.size(), 0);` 之后、`peer.socket.abort();` 之前加入：

```cpp
      // Picking the package as a wallpaper is a code action and asks for trust.
      QSignalSpy ready(app.theme, &ThemeController::wallpaperReady);
      const auto selected = app.theme->selectedId();
      app.theme->selectWallpaper("builtin/yozakura");
      QCOMPARE(app.theme->error(), QString("theme_wallpaper_unavailable"));
      app.theme->selectWallpaper("test.wallpaper/default");
      QCOMPARE(trust.size(), 1);
      QCOMPARE(trust.at(0).at(0).toString(), QString("test.wallpaper/default"));
      QCOMPARE(trust.at(0).at(1).toString(), QString("wallpaper"));
      QCOMPARE(ready.size(), 0);
      app.theme->selectWallpaper("test.wallpaper/default", true);
      QTRY_COMPARE(ready.size(), 1);
      QCOMPARE(ready.at(0).at(0).toString(), QString("test.wallpaper/default"));
      QTRY_VERIFY(app.theme->wallpaperSlots().contains("test.wallpaper/default"));
      QVERIFY(app.extensions->items().first().toMap().value("trusted").toBool());
      // Wallpaper choices never touch the palette or layout selections.
      QCOMPARE(app.theme->selectedId(), selected);
      QCOMPARE(app.theme->layoutSelectedId(), QString());
      QCOMPARE(trust.size(), 1);
```

3. `themeCodeLoadsOnlyAfterTrust()`：在末尾的 `QTRY_VERIFY(app.theme->uiSlots().contains("wallpaper"));` 之后加入：

```cpp
      // Once the package is trusted, picking its wallpaper needs no prompt.
      QSignalSpy ready(app.theme, &ThemeController::wallpaperReady);
      trust.clear();
      idle();
      app.theme->selectWallpaper("test.trust/wall");
      QCOMPARE(trust.size(), 0);
      QCOMPARE(ready.size(), 1);
      QVERIFY(app.theme->wallpaperSlots().contains("test.trust/wall"));
```

4. 在 `tests/` 下再搜索一次，确认没有遗留的旧写法：

```sh
grep -rn '"ui", Q[A-Za-z]*{{"wallpaper"' tests
```

只允许出现 Phase 1 中 `native_extensions_test.cpp` 故意保留的两处旧写法断言。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
./build/tests/nekotune_wallpaper_test
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
./build/tests/nekotune_extensions_test wallpaperOnlyThemeDoesNotRequestTrust themeCodeLoadsOnlyAfterTrust themePackagesAndAppearance
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
ctest --test-dir build --output-on-failure
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
```

全量 CTest 的失败列表必须与 Phase 0 的基线相同。

## 完成标准

- [ ] 2.1–2.5 全部完成。
- [ ] 壁纸、扩展测试通过；全量 CTest 没有新增失败。
- [ ] 两份翻译文件的键集合一致。

## 提交

```sh
git add frontend/qt-qml/src/controllers/theme_controller.h frontend/qt-qml/src/controllers/theme_controller.cpp \
  frontend/qt-qml/src/wallpaper/wallpaper_manager.h frontend/qt-qml/src/wallpaper/wallpaper_manager.cpp \
  frontend/qt-qml/i18n/zh.json frontend/qt-qml/i18n/en.json tests/extensions_test.cpp tests/wallpaper_test.cpp
git commit -m "feat(wallpaper): 前端读取顶层壁纸字段并支持按主题选择壁纸"
```
