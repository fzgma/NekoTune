# Phase 5：减少动态效果改为用户设置

## 目标

`reducedMotion` 现在是一个主题变量，任何主题都能替用户打开或关闭“减少动态效果”，而用户自己没有入口。本阶段：

1. 主题中的 `reducedMotion` 一律被忽略（`host` 组）。
2. 新增用户设置 `reduced_motion`（D7），在外观页提供开关。
3. 所有读取 `Theme.reducedMotion` 的界面（歌词、壁纸、主题 QML）自动遵守用户设置，**不需要逐个修改**。

## 前置条件

- Phase 4 已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `shared/themes/theme_resolution.cpp` | 合成时跳过 `host` 组 |
| `frontend/qt-qml/src/controllers/theme_controller.{h,cpp}` | 新属性 `reducedMotion` 与 `setReducedMotion` |
| `frontend/qt-qml/qml/components/Theme.qml` | `reducedMotion` 直接读取控制器 |
| `frontend/qt-qml/qml/components/AppearancePanel.qml` | 新增开关 |
| `frontend/qt-qml/i18n/zh.json`、`en.json` | 新文案 |
| `tests/native_extensions_test.cpp` | 扩充 `layeredThemeTokenResolution` |
| `tests/extensions_test.cpp` | 新增 `themeReducedMotionPreference` |

## 步骤

### 5.1 合成时忽略 `host` 组

文件：`shared/themes/theme_resolution.cpp`，函数 `resolveThemeTokens`。

把外观主题的过滤条件：

```cpp
        const bool hasCard = merge(theme, [&](const QString &group) {
            return layout.isEmpty() || group != QLatin1String("layout");
        });
```

改为：

```cpp
        // Host tokens (reducedMotion) are user preferences and never come from a theme.
        const bool hasCard = merge(theme, [&](const QString &group) {
            return group != QLatin1String("host") && (layout.isEmpty() || group != QLatin1String("layout"));
        });
```

界面主题的过滤条件只接受 `layout` 和 `shared`，本来就不包含 `host`，**不需要修改**。

同时更新头文件 `shared/themes/theme_resolution.h` 中 `resolveThemeTokens` 的注释，在末尾补一句：

```cpp
/// Tokens in the "host" group keep their catalog defaults; the host applies user preferences.
```

### 5.2 控制器新增用户设置

文件：`frontend/qt-qml/src/controllers/theme_controller.h`

1. 在 `Q_PROPERTY(bool layoutUnavailable ...)` 之后加入：

```cpp
  Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY changed)
```

2. 在 `public:` 区域，`bool layoutUnavailable() const;` 之后加入：

```cpp
  bool reducedMotion() const { return m_reducedMotion; }
```

3. 在 `Q_INVOKABLE void setMode(const QString &mode);` 之后加入：

```cpp
  Q_INVOKABLE void setReducedMotion(bool enabled);
```

4. 在私有成员 `int m_pending = 0;` 之前加入：

```cpp
  bool m_reducedMotion = false;
```

文件：`frontend/qt-qml/src/controllers/theme_controller.cpp`

1. 在构造函数中，找到读取 `theme_mode` 的代码：

```cpp
  m_mode = nekotune::AppPaths::setting("theme_mode").toString("dark");
```

在它下面加入：

```cpp
  m_reducedMotion =
      nekotune::AppPaths::setting("reduced_motion").toBool(false);
```

2. 在 `rebuild()` 中，紧跟 Phase 4 写的 `m_tokens = nekotune::themes::resolveThemeTokens(...);` 之后加入：

```cpp
  m_tokens.insert("reducedMotion", m_reducedMotion);
```

3. 在 `ThemeController::setMode` 函数之后加入：

```cpp
void ThemeController::setReducedMotion(bool enabled) {
  if (enabled == m_reducedMotion)
    return;
  if (nekotune::AppPaths::saveSetting("reduced_motion", enabled)) {
    m_reducedMotion = enabled;
    m_error.clear();
    rebuild();
  } else {
    m_error = "theme_save_failed";
    emit changed();
  }
}
```

### 5.3 `Theme.qml` 直接读取用户设置

文件：`frontend/qt-qml/qml/components/Theme.qml`

找到：

```qml
    readonly property bool reducedMotion: overrides.reducedMotion !== undefined ? overrides.reducedMotion : baseTokens.reducedMotion
```

替换为：

```qml
    // A user preference: recovery windows ignore theme overrides but still honor it.
    readonly property bool reducedMotion: typeof controllers !== "undefined" && controllers.theme ? controllers.theme.reducedMotion : baseTokens.reducedMotion
```

原因：恢复窗口（`recovery` 为真）会把 `controller` 设为 `null` 并忽略所有主题覆盖值，但用户的无障碍偏好仍然应当生效。

### 5.4 外观页新增开关

文件：`frontend/qt-qml/qml/components/AppearancePanel.qml`

找到“显示模式”这一行的 `RowLayout`（包含 `objectName: "themeModePicker"` 的 `ChoiceField`），在这个 `RowLayout` 的右花括号 `}` **之后**加入：

```qml
            CheckBox {
                objectName: "reducedMotionCheck"
                text: root.t("theme_reduced_motion")
                checked: root.controller.reducedMotion
                onToggled: root.controller.setReducedMotion(checked)
            }
            Label {
                Layout.fillWidth: true
                text: root.t("theme_reduced_motion_hint")
                color: Theme.textMuted; wrapMode: Text.WordWrap
            }
```

这个开关在安全模式下**仍然可用**，不要加 `enabled` 条件。

### 5.5 新增 i18n 键

在两个 i18n 文件中，紧跟 `"theme_system"` 一行之后加入：

| 键 | zh | en |
| --- | --- | --- |
| `theme_reduced_motion` | `减少动态效果` | `Reduce motion` |
| `theme_reduced_motion_hint` | `关闭歌词滚动、逐字插值和装饰动画。此设置对所有主题生效，主题无法更改。` | `Turns off lyric scrolling, per-word interpolation and decorative animation. Applies to every theme; themes cannot change it.` |

完成后运行 Phase 4 中的 JSON 合法性检查命令。

### 5.6 测试

**(a)** 文件：`tests/native_extensions_test.cpp`，在 `layeredThemeTokenResolution` 函数末尾的 `}` 之前加入：

```cpp
        // Themes cannot change the user's motion preference.
        const QVariantMap motion{{"schemaVersion", 1}, {"tokens", QVariantMap{{"reducedMotion", true}}}};
        QCOMPARE(themes::resolveThemeTokens(catalog, motion, true).value("reducedMotion").toBool(), false);
        QCOMPARE(themes::resolveThemeTokens(catalog, appearance, true, motion).value("reducedMotion").toBool(), false);
```

**(b)** 文件：`tests/extensions_test.cpp`，在 `themeModesAndPersistenceFailures` 函数之后加入：

```cpp
    void themeReducedMotionPreference() {
      QTemporaryDir profile;
      const auto oldHome = qgetenv("NEKOTUNE_HOME");
      auto restore = qScopeGuard([&] { qputenv("NEKOTUNE_HOME", oldHome); });
      qputenv("NEKOTUNE_HOME", profile.path().toUtf8());
      IpcClient client;
      AppControllers app(client);
      auto *theme = app.theme;
      QVERIFY(!theme->reducedMotion());
      QCOMPARE(theme->tokens().value("reducedMotion").toBool(), false);
      theme->setReducedMotion(true);
      QVERIFY(theme->reducedMotion());
      QCOMPARE(theme->tokens().value("reducedMotion").toBool(), true);
      QCOMPARE(AppPaths::setting("reduced_motion").toBool(), true);
      {
        AppControllers restarted(client);
        QVERIFY(restarted.theme->reducedMotion());
      }
      QFile broken(AppPaths::configFile("settings.json"));
      QVERIFY(broken.open(QIODevice::WriteOnly));
      broken.write("not json");
      broken.close();
      theme->setReducedMotion(false);
      QVERIFY(theme->reducedMotion());
      QCOMPARE(theme->error(), "theme_save_failed");
    }
```

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test layeredThemeTokenResolution
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themeReducedMotionPreference
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themeModesAndPersistenceFailures
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_wallpaper_test
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Fusion ./build/tests/nekotune_slider_quick_test -input tests/qml/tst_compact_lyrics.qml
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
git diff --check
```

## 完成标准

- [ ] 主题里写 `reducedMotion: true` 不再生效。
- [ ] 开关可以保存，重启后保留；保存失败时提示 `theme_save_failed` 并保留原值。
- [ ] 歌词和壁纸相关测试没有新增失败。

## 提交

```sh
git add shared/themes/theme_resolution.h shared/themes/theme_resolution.cpp \
  frontend/qt-qml/src/controllers/theme_controller.h frontend/qt-qml/src/controllers/theme_controller.cpp \
  frontend/qt-qml/qml/components/Theme.qml frontend/qt-qml/qml/components/AppearancePanel.qml \
  frontend/qt-qml/i18n/zh.json frontend/qt-qml/i18n/en.json \
  tests/native_extensions_test.cpp tests/extensions_test.cpp
git commit -m "feat(theme): 减少动态效果改为用户设置"
```
