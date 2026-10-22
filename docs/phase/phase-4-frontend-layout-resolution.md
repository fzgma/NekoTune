# Phase 4：前端分开解析外观与界面

## 目标

1. 共享合成函数支持“外观 + 界面”两个来源（§5.3.3、§5.3.4）。
2. `ThemeController` 分别解析外观选择和界面选择：界面插槽来自界面选择，配色和静态背景来自外观选择。
3. 新增选择接口 `selectAppearance`、`selectLayout`、`resetAppearance`、`resetLayout`；`selectTheme` 改为“一键应用整套”。
4. 旧配置（没有 `themeLayout` 键）的显示效果与升级前完全一致（D2）。

外观页的新按钮在 Phase 8 实现。本阶段只把 `AppearancePanel.qml` 的信任对话框适配到新的信号参数。

## 前置条件

- Phase 3 已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `shared/themes/theme_resolution.{h,cpp}` | `resolveThemeTokens` 增加 `layout` 参数 |
| `frontend/qt-qml/src/controllers/theme_controller.h` | 新属性、新方法、`trustRequested` 增加参数 |
| `frontend/qt-qml/src/controllers/theme_controller.cpp` | 解析、插槽生成与选择逻辑 |
| `frontend/qt-qml/qml/components/AppearancePanel.qml` | 信任对话框按 `target` 分派 |
| `tests/native_extensions_test.cpp` | 新增 `layeredThemeTokenResolution` |
| `tests/extensions_test.cpp` | 新增 `themeAppearanceAndLayoutCombine` |

## 解析规则（实现必须完全符合）

设外观主题为 A（`m_active`），界面主题为 L（`m_layout`，可能为空）。

| 情况 | 变量来源 | 界面插槽来源 | 壁纸“跟随当前主题”来源 |
| --- | --- | --- | --- |
| L 为空（内置界面） | A 的全部变量 | 无（使用内置界面） | A 提供壁纸时取 A |
| L 与 A 是同一个主题 | A 的全部变量 | A | A |
| L 与 A 不同 | A 中除 `layout` 组以外的变量，再叠加 L 中 `layout` 组和 `shared` 组的变量 | L | L 提供壁纸时取 L，否则取 A |

- 外观主题中 `layout` 组的变量，只在搭配内置界面或它自己的界面时生效。
- 合成顺序：基础值 → A（公共 `tokens` → 变体 `tokens`）→ L（公共 `tokens` → 变体 `tokens`）。
- `bgCard` 回退规则只看 A：A 的公共和变体 `tokens` 都没有 `bgCard` 时，`bgCard` 取 `bgSurface`。
- 旧版仅含 `tokens` 的 A 在浅色模式下不参与合成（与现在一致），但 L 仍然叠加。

`layoutSelectedId()` 的取值规则：

| `selections` 中的 `themeLayout` | 返回值 |
| --- | --- |
| 键不存在 | 外观选择的主题 `hasLayout` 为真时返回外观选择的 ID，否则返回 `""` |
| `""` | `""`（内置界面） |
| 其他字符串 | 原值 |

`m_layout` 的取值规则：安全模式下为空；`layoutSelectedId()` 为空时为空；对应主题存在、`available` 为真、`hasLayout` 为真时取该主题，否则为空（此时 `layoutUnavailable` 为真）。

## 步骤

### 4.1 共享合成函数增加界面来源

文件：`shared/themes/theme_resolution.h`

把 `resolveThemeTokens` 的声明和注释替换为：

```cpp
/// Final token values. Order: base values, then the appearance theme (common "tokens", then the variant
/// "tokens"), then the layout theme. When layout is non-empty, the appearance theme's "layout" group
/// tokens are ignored and only the layout theme's "layout" and "shared" group tokens apply.
/// Unknown token names are ignored.
QVariantMap resolveThemeTokens(const QVariantMap &catalog, const QVariantMap &theme, bool dark,
                               const QVariantMap &layout = {});
```

文件：`shared/themes/theme_resolution.cpp`

把 `resolveThemeTokens` 整个函数替换为：

```cpp
QVariantMap resolveThemeTokens(const QVariantMap &catalog, const QVariantMap &theme, bool dark,
                               const QVariantMap &layout) {
    auto result = baseTokens(catalog, dark);
    const auto rules = catalog.value("tokens").toMap();
    const QString mode = dark ? QStringLiteral("dark") : QStringLiteral("light");
    auto merge = [&](const QVariantMap &source, const auto &accept) {
        const auto common = source.value("tokens").toMap();
        const auto variant = source.value("variants").toMap().value(mode).toMap().value("tokens").toMap();
        for (const auto &values : {common, variant})
            for (auto it = values.cbegin(); it != values.cend(); ++it)
                if (rules.contains(it.key()) && accept(rules.value(it.key()).toMap().value("group").toString()))
                    result.insert(it.key(), it.value());
        return common.contains("bgCard") || variant.contains("bgCard");
    };
    // Legacy token-only themes describe a dark palette; light mode keeps the built-in light values.
    if (dark || theme.contains("schemaVersion")) {
        const bool hasCard = merge(theme, [&](const QString &group) {
            return layout.isEmpty() || group != QLatin1String("layout");
        });
        if (!hasCard)
            result.insert("bgCard", result.value("bgSurface"));
    }
    if (!layout.isEmpty())
        merge(layout, [](const QString &group) {
            return group == QLatin1String("layout") || group == QLatin1String("shared");
        });
    return result;
}
```

Phase 2 的测试 `sharedThemeTokenResolution` 必须仍然通过（不传 `layout` 时行为不变）。

### 4.2 修改 `theme_controller.h`

1. 在 `Q_PROPERTY(QVariantMap uiSlots READ uiSlots NOTIFY changed)` 之后加入：

```cpp
  Q_PROPERTY(QString layoutSelectedId READ layoutSelectedId NOTIFY changed)
  Q_PROPERTY(QString activeLayoutId READ activeLayoutId NOTIFY changed)
  Q_PROPERTY(bool layoutUnavailable READ layoutUnavailable NOTIFY changed)
```

2. 在 `public:` 区域，`QVariantMap uiSlots() const { return m_slots; }` 之后加入：

```cpp
  QString layoutSelectedId() const;
  QString activeLayoutId() const { return m_layout.value("id").toString(); }
  bool layoutUnavailable() const;
```

3. 在 `Q_INVOKABLE void selectTheme(const QString &id, bool trusted = false);` 之后加入：

```cpp
  Q_INVOKABLE void selectAppearance(const QString &id, bool trusted = false);
  Q_INVOKABLE void selectLayout(const QString &id, bool trusted = false);
  Q_INVOKABLE void resetAppearance();
  Q_INVOKABLE void resetLayout();
```

4. 把信号 `void trustRequested(const QString &id);` 改为：

```cpp
  // target is "theme", "appearance" or "layout" so the dialog can retry the same action.
  void trustRequested(const QString &id, const QString &target);
```

5. 在 `private:` 区域，`void installTheme(...)` 声明之后加入：

```cpp
  bool ensureAvailable(const QVariantMap &theme, bool trusted,
                       const QString &target, std::function<void()> retry);
  void applySelection(const QString &appearance, const QString &layout,
                      Completion done = {});
```

6. 把成员行 `QVariantMap m_catalog, m_active, m_tokens, m_background, m_slots;` 改为：

```cpp
  QVariantMap m_catalog, m_active, m_layout, m_tokens, m_background, m_slots;
```

### 4.3 修改 `theme_controller.cpp`：查询函数

在 `ThemeController::selectedId()` 函数之后加入：

```cpp
QString ThemeController::layoutSelectedId() const {
  const auto selections = m_extensions.selections();
  if (selections.contains("themeLayout"))
    return selections.value("themeLayout").toString();
  // Profiles from before separate selection follow the appearance theme.
  return find(selectedId()).value("hasLayout").toBool() ? selectedId()
                                                         : QString();
}
bool ThemeController::layoutUnavailable() const {
  return !safeMode() && !layoutSelectedId().isEmpty() &&
         activeLayoutId() != layoutSelectedId();
}
```

### 4.4 修改 `rebuild()`

在 `ThemeController::rebuild()` 中按顺序做三处修改。

**(a)** 找到给每个条目加 `swatches` 的循环：

```cpp
  for (auto &value : m_items) {
    auto item = value.toMap();
    const auto palette = resolve(item, isDark());
```

在 `value = item;` 之前加入两行：

```cpp
    item.insert("hasLayout", nekotune::themes::providesLayout(item));
    item.insert("hasWallpaper", nekotune::themes::providesWallpaper(item));
```

**(b)** 找到：

```cpp
  m_tokens = resolve(m_active, isDark());
```

替换为：

```cpp
  m_layout.clear();
  if (const auto layoutId = layoutSelectedId();
      !safeMode() && !layoutId.isEmpty()) {
    const auto candidate = find(layoutId);
    if (candidate.value("available").toBool() &&
        candidate.value("hasLayout").toBool())
      m_layout = candidate;
  }
  const bool separateLayout =
      !m_layout.isEmpty() && m_layout.value("id") != m_active.value("id");
  m_tokens = nekotune::themes::resolveThemeTokens(
      m_catalog, m_active, isDark(), separateLayout ? m_layout : QVariantMap{});
```

**(c)** 找到从 `m_slots.clear();` 开始、到 `emit changed();` 之前结束的整段代码（包含 `auto add = [&](const QString &slot, const QVariant &source) {...}` 和 `if (!safeMode()) {...}`），整体替换为：

```cpp
  m_slots.clear();
  auto add = [&](const QVariantMap &theme, const QString &slot,
                 const QVariant &source) {
    if (source.toString().isEmpty())
      return;
    m_slots.insert(
        slot,
        QVariantMap{{"id", theme.value("id").toString() + "/" + slot},
                    {"slot", slot},
                    {"source", QDir(theme.value("directory").toString())
                                   .filePath(source.toString())},
                    {"extensionId", theme.value("extensionId")},
                    {"generation", theme.value("generation")}});
  };
  if (!safeMode()) {
    const auto ui = m_layout.value("ui").toMap();
    for (const auto &key : {"background", "shell", "bottomPlayer"})
      add(m_layout, QLatin1String(key), ui.value(QLatin1String(key)));
    const auto pages = ui.value("pages").toMap();
    for (auto it = pages.cbegin(); it != pages.cend(); ++it)
      add(m_layout, "page:" + it.key(), it.value());
    // "Follow current theme" wallpaper: the layout's entry first, then the
    // appearance's.
    const auto &wallpaper = nekotune::themes::providesWallpaper(m_layout)
                                ? m_layout
                                : m_active;
    add(wallpaper, "wallpaper",
        wallpaper.value("ui").toMap().value("wallpaper"));
  }
```

注意：

- 静态背景 `m_background` 的计算代码（使用 `m_active` 和 `directory` 变量）**保持不变**，它位于 `m_slots.clear();` 之前。
- 如果替换后 `directory` 变量只剩静态背景在用，保留它；编译器提示未使用时再删除。

### 4.5 修改 `theme_controller.cpp`：选择操作

1. 用下面的代码**整体替换**现有的 `ThemeController::selectTheme` 函数：

```cpp
bool ThemeController::ensureAvailable(const QVariantMap &theme, bool trusted,
                                      const QString &target,
                                      std::function<void()> retry) {
  if (theme.value("available").toBool())
    return true;
  if (theme.value("hasCode").toBool() && !theme.value("trusted").toBool() &&
      !trusted) {
    emit trustRequested(theme.value("id").toString(), target);
    return false;
  }
  request("extensions.enable",
          {{"id", theme.value("extensionId")}, {"trusted", trusted}},
          [retry](const QVariantMap &, const QString &error) {
            if (error.isEmpty())
              retry();
          });
  return false;
}
void ThemeController::applySelection(const QString &appearance,
                                     const QString &layout, Completion done) {
  // Always write both keys so a later single-part change never falls back to
  // the legacy "layout follows appearance" rule.
  const auto stored =
      appearance == QLatin1String("builtin/yozakura") ? QString() : appearance;
  request("extensions.select",
          {{"selections",
            QVariantMap{{"theme", stored}, {"themeLayout", layout}}}},
          std::move(done));
}
void ThemeController::selectTheme(const QString &id, bool trusted) {
  if (busy())
    return;
  const auto theme = find(id);
  if (theme.isEmpty()) {
    m_error = "theme_unavailable";
    emit changed();
    return;
  }
  if (!ensureAvailable(theme, trusted, "theme", [this, id] { selectTheme(id); }))
    return;
  // A theme without layout entries only changes the appearance.
  applySelection(id, theme.value("hasLayout").toBool() ? id
                                                        : layoutSelectedId());
}
void ThemeController::selectAppearance(const QString &id, bool trusted) {
  if (busy())
    return;
  const auto theme = find(id);
  if (theme.isEmpty()) {
    m_error = "theme_unavailable";
    emit changed();
    return;
  }
  if (!ensureAvailable(theme, trusted, "appearance",
                       [this, id] { selectAppearance(id); }))
    return;
  applySelection(id, layoutSelectedId());
}
void ThemeController::selectLayout(const QString &id, bool trusted) {
  if (busy())
    return;
  if (id.isEmpty()) {
    applySelection(selectedId(), {});
    return;
  }
  const auto theme = find(id);
  if (!theme.value("hasLayout").toBool()) {
    m_error = "theme_layout_unavailable";
    emit changed();
    return;
  }
  if (!ensureAvailable(theme, trusted, "layout",
                       [this, id] { selectLayout(id); }))
    return;
  applySelection(selectedId(), id);
}
void ThemeController::resetAppearance() {
  if (busy())
    return;
  applySelection({}, layoutSelectedId());
}
void ThemeController::resetLayout() {
  if (busy())
    return;
  applySelection(selectedId(), {});
}
```

2. 把现有的 `ThemeController::reset()` 整体替换为：

```cpp
void ThemeController::reset() {
  if (busy())
    return;
  applySelection({}, {}, [this](const QVariantMap &, const QString &error) {
    if (error.isEmpty())
      setMode("dark");
  });
}
```

3. 在文件中搜索 `trustRequested(`，确认除了 `ensureAvailable` 以外**没有**其他发出该信号的地方。

### 4.6 适配 `AppearancePanel.qml` 的信任对话框

文件：`frontend/qt-qml/qml/components/AppearancePanel.qml`

1. 在根 `Item` 的属性区，`property string pendingId: ""` 下方加入：

```qml
    property string pendingTarget: "theme"
```

2. 找到：

```qml
        function onTrustRequested(id) { root.pendingId = id; trustDialog.open() }
```

改为：

```qml
        function onTrustRequested(id, target) { root.pendingId = id; root.pendingTarget = target; trustDialog.open() }
```

3. 找到 `trustDialog` 中的：

```qml
        onAccepted: root.controller.selectTheme(root.pendingId, true)
```

改为：

```qml
        onAccepted: {
            if (root.pendingTarget === "layout")
                root.controller.selectLayout(root.pendingId, true)
            else if (root.pendingTarget === "appearance")
                root.controller.selectAppearance(root.pendingId, true)
            else
                root.controller.selectTheme(root.pendingId, true)
        }
```

4. 卡片上的 `✓` 标记暂时仍然使用 `root.controller.selectedId === modelData.id`，**本阶段不修改**。

### 4.7 新增 i18n 键

在 `frontend/qt-qml/i18n/zh.json` 和 `en.json` 中，紧跟 `"theme_unavailable"` 一行之后各加一行：

| 键 | zh | en |
| --- | --- | --- |
| `theme_layout_unavailable` | `选中的界面暂不可用，正在使用内置界面；原选择已保留。` | `The selected layout is unavailable. Using the built-in layout; your choice is kept.` |

两个文件都要保持合法 JSON（注意上一行末尾的逗号）。验证方法：

```sh
uv run --no-project python -I -c "import json,sys;[json.load(open(f,encoding='utf-8')) for f in sys.argv[1:]]" frontend/qt-qml/i18n/zh.json frontend/qt-qml/i18n/en.json
```

### 4.8 新增合成函数测试

文件：`tests/native_extensions_test.cpp`，在 `themeLayoutSelection` 之后加入：

```cpp
    void layeredThemeTokenResolution() {
        const auto &catalog = themes::catalog();
        const QVariantMap appearance{
            {"schemaVersion", 1},
            {"tokens", QVariantMap{{"accentPrimary", "#AA0000"}, {"sidebarWidthWide", 300}, {"radiusMd", 6}}}};
        const QVariantMap layout{
            {"schemaVersion", 1},
            {"tokens", QVariantMap{{"accentPrimary", "#00AA00"}, {"sidebarWidthWide", 260}, {"radiusMd", 20}}}};
        QCOMPARE(themes::resolveThemeTokens(catalog, appearance, true).value("sidebarWidthWide").toInt(), 300);
        const auto combined = themes::resolveThemeTokens(catalog, appearance, true, layout);
        QCOMPARE(combined.value("accentPrimary").toString(), "#AA0000");
        QCOMPARE(combined.value("sidebarWidthWide").toInt(), 260);
        QCOMPARE(combined.value("radiusMd").toInt(), 20);
        // The appearance theme's layout tokens never leak into another layout.
        const QVariantMap plainLayout{{"schemaVersion", 1}};
        const auto plain = themes::resolveThemeTokens(catalog, appearance, true, plainLayout);
        QCOMPARE(plain.value("sidebarWidthWide"), themes::baseTokens(catalog, true).value("sidebarWidthWide"));
        QCOMPARE(plain.value("radiusMd").toInt(), 6);
        // A legacy dark-only appearance still receives the layout's tokens in light mode.
        const QVariantMap legacy{{"tokens", QVariantMap{{"accentPrimary", "#555555"}}}};
        const auto legacyLight = themes::resolveThemeTokens(catalog, legacy, false, layout);
        QCOMPARE(legacyLight.value("sidebarWidthWide").toInt(), 260);
        QCOMPARE(legacyLight.value("accentPrimary"), themes::baseTokens(catalog, false).value("accentPrimary"));
    }
```

### 4.9 新增前后端联合测试

文件：`tests/extensions_test.cpp`

在 `themePackagesAndAppearance` 函数**之后**加入新函数 `themeAppearanceAndLayoutCombine`。

**第一步：复制搭建代码。** 打开 `themePackagesAndAppearance`，复制从函数第一行 `QTemporaryDir profile;` 到 `QTRY_VERIFY_WITH_TIMEOUT(peer.call("extensions.list")...runtimeReady...15000);` 为止的全部代码，粘贴为新函数的开头。把其中的 `"theme.sock"`、`"theme.sqlite3"` 分别改成 `"combo.sock"`、`"combo.sqlite3"`。

**第二步：写入主题包。** 接着加入：

```cpp
      const auto folder = profile.filePath("combo");
      QVERIFY(QDir().mkpath(folder));
      auto write = [&](const QString &name, const QByteArray &bytes) {
        QFile file(folder + '/' + name);
        return file.open(QIODevice::WriteOnly) &&
               file.write(bytes) == bytes.size();
      };
      QVERIFY(write("Player.qml",
                    "import QtQuick\nRectangle { objectName: 'comboPlayer' }"));
      auto variants = [](const QString &accent) {
        const QJsonObject tokens{
            {"tokens", QJsonObject{{"accentPrimary", accent}}}};
        return QJsonObject{{"light", tokens}, {"dark", tokens}};
      };
      const QJsonObject palette{
          {"id", "palette"}, {"title", "Palette"}, {"schemaVersion", 1},
          {"tokens", QJsonObject{{"sidebarWidthWide", 300}}},
          {"variants", variants("#AA0000")}};
      const QJsonObject layout{
          {"id", "layout"}, {"title", "Layout"}, {"schemaVersion", 1},
          {"tokens", QJsonObject{{"sidebarWidthWide", 260}, {"radiusMd", 20}}},
          {"variants", variants("#00AA00")},
          {"ui", QJsonObject{{"bottomPlayer", "Player.qml"}}}};
      const QJsonObject manifest{
          {"id", "test.combo"}, {"name", "Combo test"}, {"version", "1.0.0"},
          {"apiVersion", 1}, {"type", "theme"},
          {"contributes",
           QJsonObject{{"themes", QJsonArray{palette, layout}}}}};
      QVERIFY(write("extension.json", QJsonDocument(manifest).toJson()));
      QCOMPARE(peer.call("extensions.install",
                         {{"path", folder}, {"development", true}})
                   .value("status").toString(),
               "ok");
      QCOMPARE(peer.call("extensions.enable",
                         {{"id", "test.combo"}, {"trusted", true}})
                   .value("status").toString(),
               "ok");
```

**第三步：复制前端搭建代码。** 回到 `themePackagesAndAppearance`，复制从 `IpcClient client;` 到 `client.connectBackend();` 为止的代码，粘贴到上面代码之后。

**第四步：写断言。** 接着加入：

```cpp
      QTRY_COMPARE(app.theme->items().size(), 5);
      app.theme->setMode("dark");
      auto idle = [&] { QTRY_VERIFY(!app.theme->busy()); };
      auto comboPlayer = [&] {
        return window->findChild<QObject *>("comboPlayer");
      };

      // Legacy single-slot selection: the layout follows the appearance.
      QCOMPARE(peer.call("extensions.select",
                         {{"slot", "theme"},
                          {"contribution", "test.combo/layout"}})
                   .value("status").toString(),
               "ok");
      QTRY_COMPARE(app.theme->activeLayoutId(), QString("test.combo/layout"));
      QVERIFY(!app.extensions->selections().contains("themeLayout"));
```

`app.extensions` 是 `AppControllers` 中的 `ExtensionsController *` 成员（见 `frontend/qt-qml/src/controllers/app_controllers.h`）。

继续加入：

```cpp
      QCOMPARE(app.theme->layoutSelectedId(), QString("test.combo/layout"));
      QTRY_VERIFY(comboPlayer());
      QCOMPARE(peer.call("extensions.select",
                         {{"slot", "theme"},
                          {"contribution", "test.combo/palette"}})
                   .value("status").toString(),
               "ok");
      QTRY_COMPARE(app.theme->activeId(), QString("test.combo/palette"));
      QCOMPARE(app.theme->layoutSelectedId(), QString());
      QTRY_VERIFY(!comboPlayer());

      // One-click apply of a theme with layout entries sets both parts.
      idle();
      app.theme->selectTheme("test.combo/layout");
      QTRY_COMPARE(app.theme->activeLayoutId(), QString("test.combo/layout"));
      QCOMPARE(app.theme->activeId(), QString("test.combo/layout"));
      QCOMPARE(app.theme->tokens().value("accentPrimary").toString(),
               QString("#00AA00"));
      QCOMPARE(app.theme->tokens().value("sidebarWidthWide").toInt(), 260);
      QTRY_VERIFY(comboPlayer());
      QPointer<QObject> player = comboPlayer();

      // Appearance only: colors change, layout tokens and view stay.
      idle();
      app.theme->selectAppearance("test.combo/palette");
      QTRY_COMPARE(app.theme->activeId(), QString("test.combo/palette"));
      QCOMPARE(app.theme->activeLayoutId(), QString("test.combo/layout"));
      QCOMPARE(app.theme->tokens().value("accentPrimary").toString(),
               QString("#AA0000"));
      QCOMPARE(app.theme->tokens().value("sidebarWidthWide").toInt(), 260);
      QCOMPARE(app.theme->tokens().value("radiusMd").toInt(), 20);
      QTest::qWait(50);
      QVERIFY(player);
      QCOMPARE(comboPlayer(), player.data());

      // Built-in layout: the palette's layout tokens apply as recommendations.
      idle();
      app.theme->selectLayout("");
      QTRY_COMPARE(app.theme->layoutSelectedId(), QString());
      QTRY_VERIFY(!comboPlayer());
      QCOMPARE(app.theme->tokens().value("sidebarWidthWide").toInt(), 300);
      QCOMPARE(app.theme->activeId(), QString("test.combo/palette"));

      // A theme without layout entries leaves the layout selection alone.
      idle();
      app.theme->selectLayout("test.combo/layout");
      QTRY_COMPARE(app.theme->activeLayoutId(), QString("test.combo/layout"));
      idle();
      app.theme->selectTheme("builtin/ocean");
      QTRY_COMPARE(app.theme->activeId(), QString("builtin/ocean"));
      QCOMPARE(app.theme->activeLayoutId(), QString("test.combo/layout"));

      idle();
      app.theme->selectLayout("builtin/ocean");
      QCOMPARE(app.theme->error(), QString("theme_layout_unavailable"));
      QCOMPARE(app.theme->layoutSelectedId(), QString("test.combo/layout"));

      // Disabling the package falls back per part and keeps both choices.
      idle();
      app.theme->selectAppearance("test.combo/palette");
      QTRY_COMPARE(app.theme->activeId(), QString("test.combo/palette"));
      idle();
      QCOMPARE(peer.call("extensions.disable", {{"id", "test.combo"}})
                   .value("status").toString(),
               "ok");
      QTRY_VERIFY(app.theme->layoutUnavailable());
      QCOMPARE(app.theme->activeLayoutId(), QString());
      QVERIFY(app.theme->unavailable());
      QCOMPARE(app.theme->activeId(), QString("builtin/yozakura"));
      QTRY_VERIFY(!comboPlayer());
      idle();
      app.theme->selectAppearance("builtin/forest");
      QTRY_COMPARE(app.theme->activeId(), QString("builtin/forest"));
      QCOMPARE(app.theme->layoutSelectedId(), QString("test.combo/layout"));
      QCOMPARE(peer.call("extensions.enable",
                         {{"id", "test.combo"}, {"trusted", true}})
                   .value("status").toString(),
               "ok");
      QTRY_COMPARE(app.theme->activeLayoutId(), QString("test.combo/layout"));
      QTRY_VERIFY(comboPlayer());

      idle();
      app.theme->reset();
      QTRY_COMPARE(app.theme->layoutSelectedId(), QString());
      QCOMPARE(app.theme->selectedId(), QString("builtin/yozakura"));
      QTRY_VERIFY(!comboPlayer());
```

**第五步：收尾。** 如果 `themePackagesAndAppearance` 在函数末尾有清理代码（例如关闭窗口、断开连接、检查 `qmlWarnings`），照原样复制到新函数末尾。

编译提示：

- 需要 `#include <QPointer>`，文件中没有就加上。
- `peer.call` 的第二个参数是 `QJsonObject`，与文件中其他调用的写法一致。

### 4.10 更新一条旧测试断言（有意的行为变化）

按设计，点击**不含界面**的主题卡片只改变配色，不再移除当前界面（§5.3.5 “只提供一项的卡片只改变对应选项”）。`tests/extensions_test.cpp` 的 `themePackagesAndAppearance` 中有一条断言针对的正是旧行为。

在该函数中找到点击 `themeCard_builtin/ocean` 之后的两行：

```cpp
      QTRY_COMPARE(app.theme->activeId(), "builtin/ocean");
      QTRY_VERIFY(player.isNull());
```

替换为：

```cpp
      QTRY_COMPARE(app.theme->activeId(), "builtin/ocean");
      // A palette-only card changes colors and keeps the selected layout.
      QCOMPARE(app.theme->activeLayoutId(), QString("test.theme/default"));
      QVERIFY(!player.isNull());
```

**只改这两行。** 后面的 `QCOMPARE(window->color(), QColor("#F3FAFD"));` 保持不变（配色仍然来自深海主题）。之后测试会选择 `test.theme/background`，它带有界面，选择后会替换掉播放栏，后续断言不受影响。

除了这一处，**不得修改任何现有测试的断言**。如果还有其他现有测试失败，停下来报告。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test sharedThemeTokenResolution
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test layeredThemeTokenResolution
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themeAppearanceAndLayoutCombine
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themePackagesAndAppearance
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themeStartup
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test managementThemeAndShellInRealFrontend
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_wallpaper_test
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
git diff --check
```

如果 Qt6 的 `qmllint` 可用，再运行：

```sh
qmllint frontend/qt-qml/qml/components/AppearancePanel.qml
```

如实报告警告；只有在修改前没有、修改后新出现的警告才需要处理。

## 完成标准

- [ ] 现有主题测试（`themePackagesAndAppearance`、`themeStartup`、`managementThemeAndShellInRealFrontend`、壁纸测试）全部通过；其中只有 4.10 指定的那一条断言被修改。
- [ ] `themeAppearanceAndLayoutCombine`、`layeredThemeTokenResolution` 通过。
- [ ] 全量测试没有新增失败。
- [ ] `zh.json` 与 `en.json` 都是合法 JSON，且新增的键两边一致。

## 提交

```sh
git add shared/themes/theme_resolution.h shared/themes/theme_resolution.cpp \
  frontend/qt-qml/src/controllers/theme_controller.h frontend/qt-qml/src/controllers/theme_controller.cpp \
  frontend/qt-qml/qml/components/AppearancePanel.qml \
  frontend/qt-qml/i18n/zh.json frontend/qt-qml/i18n/en.json \
  tests/native_extensions_test.cpp tests/extensions_test.cpp
git commit -m "feat(theme): 分开解析外观与界面选择" -m "不含界面的主题只改变配色，保留当前界面。"
```
