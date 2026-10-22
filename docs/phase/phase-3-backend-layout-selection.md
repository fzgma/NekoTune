# Phase 3：后端支持界面选择与批量选择

## 目标

1. `extensions.select` 支持新的选择键 `themeLayout`（设计决定 D2）。
2. `extensions.select` 支持批量写法 `{selections: {...}}`，全部校验通过才一次性保存（D4）。
3. 删除主题包时，同时清理指向它的 `themeLayout`。
4. 共享库新增 `providesLayout()`、`providesWallpaper()`，供前后端判断主题提供了哪些界面能力。

**本阶段前端还没有使用新键，用户可见行为不变。**

## 前置条件

- Phase 2 已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `shared/themes/theme_resolution.{h,cpp}` | 新增 `providesLayout`、`providesWallpaper` |
| `backend/infrastructure/extensions/plugin_manager.h` | 声明 `validateSelection` |
| `backend/infrastructure/extensions/plugin_manager.cpp` | 重写 `select()`；修改 `uninstall()` |
| `tests/native_extensions_test.cpp` | 新增 `themeUiCapabilities`、`themeLayoutSelection` |

## 步骤

### 3.1 共享库：界面能力判断

在 `shared/themes/theme_resolution.h` 的 `resolveThemeTokens` 声明之后、命名空间结束之前加入：

```cpp
/// True when the theme replaces any part of the main window: shell, bottomPlayer, background or pages.
/// Wallpaper alone does not count as a layout.
bool providesLayout(const QVariantMap &theme);
/// True when the theme declares ui.wallpaper.
bool providesWallpaper(const QVariantMap &theme);
```

在 `shared/themes/theme_resolution.cpp` 的 `resolveThemeTokens` 函数之后、命名空间结束之前加入：

```cpp
bool providesLayout(const QVariantMap &theme) {
    const auto ui = theme.value("ui").toMap();
    for (const auto *key : {"shell", "bottomPlayer", "background"})
        if (!ui.value(QLatin1String(key)).toString().isEmpty())
            return true;
    return !ui.value("pages").toMap().isEmpty();
}

bool providesWallpaper(const QVariantMap &theme) {
    return !theme.value("ui").toMap().value("wallpaper").toString().isEmpty();
}
```

### 3.2 声明 `validateSelection`

文件：`backend/infrastructure/extensions/plugin_manager.h`

找到私有区中的：

```cpp
    Result<QVariantMap> select(const QVariantMap &params);
```

在它**下面**加入：

```cpp
    Result<void> validateSelection(const QString &slot, const QString &contribution) const;
```

### 3.3 重写 `select()`

文件：`backend/infrastructure/extensions/plugin_manager.cpp`

1. 在 `#include` 区域加入 `#include "themes/theme_resolution.h"`（如果还没有）。
2. 用下面的代码**整体替换**现有的 `Result<QVariantMap> PluginManager::select(const QVariantMap &params) { ... }` 函数：

```cpp
Result<void> PluginManager::validateSelection(const QString &slot, const QString &contribution) const {
    if (contribution.isEmpty())
        return {};
    if (slot == "theme")
        for (const auto &theme : themes::catalog().value("themes").toList())
            if (theme.toMap().value("id") == contribution)
                return {};
    const bool themeSlot = slot == "theme" || slot == "themeLayout";
    for (const auto &value : snapshot().value("extensions").toList())
        for (const auto &item : value.toMap().value("contributes").toMap().value(themeSlot ? "themes" : "slots").toList()) {
            const auto map = item.toMap();
            if (map.value("id") != contribution)
                continue;
            if (slot == "theme" || (slot == "themeLayout" && themes::providesLayout(map)) ||
                (!themeSlot && map.value("slot") == slot))
                return {};
        }
    return failure("Contribution is unavailable for this slot");
}
Result<QVariantMap> PluginManager::select(const QVariantMap &params) {
    // {selections: {slot: contribution}} updates several slots atomically; {slot, contribution} is the legacy form.
    QVariantMap changes;
    if (params.contains("selections")) {
        if (params.value("selections").typeId() != QMetaType::QVariantMap)
            return failure("Invalid selection");
        changes = params.value("selections").toMap();
    } else if (params.value("slot").typeId() == QMetaType::QString) {
        changes.insert(params.value("slot").toString(), params.value("contribution"));
    }
    if (changes.isEmpty())
        return failure("Invalid selection");
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        if (it.key().isEmpty() || it.value().typeId() != QMetaType::QString)
            return failure("Invalid selection");
        // Unchanged entries keep a selection whose package is temporarily disabled.
        if (m_selections.value(it.key()) == it.value())
            continue;
        if (auto valid = validateSelection(it.key(), it.value().toString()); !valid)
            return valid.error();
    }
    const auto previous = m_selections;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it)
        m_selections.insert(it.key(), it.value());
    if (auto saved = save(); !saved) {
        m_selections = previous;
        return saved.error();
    }
    notifyChanged();
    return snapshot();
}
```

要点说明：

- 旧写法 `{slot, contribution}` 的校验逻辑与原来完全一致：`theme` 接受内置主题和运行中的主题贡献，其他插槽接受运行中扩展的 `slots` 贡献。
- `themeLayout` 只接受**运行中**、并且 `providesLayout()` 为真的主题贡献。内置主题没有 `ui`，因此不能作为界面选择。
- 空字符串表示恢复默认，对任何插槽都合法。
- **值没有变化的键不重新校验。** 前端每次都会同时写入 `theme` 和 `themeLayout`（D3）；如果其中一项指向的包暂时被禁用，只改另一项时也必须成功，原选择保持不变。
- 任何一项不合法，整个请求失败，`m_selections` 不会改变。

### 3.4 删除主题包时清理界面选择

在同一文件中找到 `PluginManager::uninstall`，其中有这段代码：

```cpp
    if (m_entries[id].manifest.isTheme() && m_selections.value("theme").toString().startsWith(id + "/")) {
        const auto previous = m_selections;
        m_selections.insert("theme", QString());
        if (auto saved = save(); !saved) {
            m_selections = previous;
            done(saved.error());
            return;
        }
    }
```

整体替换为：

```cpp
    if (m_entries[id].manifest.isTheme()) {
        const auto previous = m_selections;
        for (const auto *key : {"theme", "themeLayout"})
            if (m_selections.value(QLatin1String(key)).toString().startsWith(id + "/"))
                m_selections.insert(QLatin1String(key), QString());
        if (m_selections != previous)
            if (auto saved = save(); !saved) {
                m_selections = previous;
                done(saved.error());
                return;
            }
    }
```

注意：不要在 `themeLayout` 键原本不存在时创建它。上面的代码只在值以 `id + "/"` 开头时才写入，键不存在时 `value()` 返回空字符串，不会命中，满足这个要求。

### 3.5 确认 IPC 层不需要修改

打开 `backend/ipc/api/extensions_api.cpp`，确认 `extensions.select` 只是把 `params.toVariantMap()` 原样转发给 `api.extensions->request`，没有额外的参数校验。**不需要修改这个文件。** 如果发现有校验，停止并报告。

### 3.6 新增测试

文件：`tests/native_extensions_test.cpp`

1. 检查是否已包含 `<QJsonArray>`，没有就在 `#include <QJsonDocument>` 旁边加上。
2. 在 `sharedThemeTokenResolution` 之后加入两个测试函数：

```cpp
    void themeUiCapabilities() {
        QVERIFY(!themes::providesLayout({}));
        QVERIFY(!themes::providesLayout({{"ui", QVariantMap{{"wallpaper", "Wallpaper.qml"}}}}));
        QVERIFY(!themes::providesLayout({{"ui", QVariantMap{{"pages", QVariantMap{}}}}}));
        QVERIFY(themes::providesLayout({{"ui", QVariantMap{{"bottomPlayer", "Player.qml"}}}}));
        QVERIFY(themes::providesLayout({{"ui", QVariantMap{{"pages", QVariantMap{{"home", "Home.qml"}}}}}}));
        QVERIFY(themes::providesWallpaper({{"ui", QVariantMap{{"wallpaper", "Wallpaper.qml"}}}}));
        QVERIFY(!themes::providesWallpaper({{"ui", QVariantMap{{"shell", "Shell.qml"}}}}));
    }

    void themeLayoutSelection() {
        auto manager = startManager();
        const QDir folder(m_home->filePath("layout-theme"));
        QVERIFY(QDir().mkpath(folder.path()));
        auto write = [&](const QString &name, const QByteArray &bytes) {
            QFile file(folder.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        QVERIFY(write("Player.qml", "import QtQuick\nItem {}\n"));
        QVERIFY(write("Wallpaper.qml", "import QtQuick\nItem {}\n"));
        const QJsonObject variants{{"light", QJsonObject{}}, {"dark", QJsonObject{}}};
        auto theme = [&](const QString &id, const QJsonObject &ui) {
            QJsonObject value{{"id", id}, {"title", id}, {"schemaVersion", 1}, {"variants", variants}};
            if (!ui.isEmpty())
                value.insert("ui", ui);
            return value;
        };
        const QJsonObject manifest{
            {"id", "test.layout"}, {"name", "Layout test"}, {"version", "1.0.0"}, {"apiVersion", 1}, {"type", "theme"},
            {"contributes", QJsonObject{{"themes", QJsonArray{theme("palette", {}),
                                                              theme("layout", {{"bottomPlayer", "Player.qml"}}),
                                                              theme("wall", {{"wallpaper", "Wallpaper.qml"}})}}}}};
        QVERIFY(write("extension.json", QJsonDocument(manifest).toJson()));
        install(*manager, folder.path());
        enable(*manager, "test.layout");
        const auto selections = [&] { return manager->snapshot().value("selections").toMap(); };

        // The legacy single-slot form keeps working and does not create themeLayout.
        QVERIFY(call(*manager, "extensions.select", {{"slot", "theme"}, {"contribution", "test.layout/palette"}}));
        QVERIFY(!selections().contains("themeLayout"));

        const auto both = call(*manager, "extensions.select",
                               {{"selections", QVariantMap{{"theme", "builtin/ocean"}, {"themeLayout", "test.layout/layout"}}}});
        QVERIFY2(both, qPrintable(errorOf(both)));
        QCOMPARE(selections().value("theme").toString(), "builtin/ocean");
        QCOMPARE(selections().value("themeLayout").toString(), "test.layout/layout");

        // Only running themes with layout entries qualify; wallpaper-only themes do not.
        for (const auto *invalid : {"builtin/ocean", "test.layout/palette", "test.layout/wall", "test.layout/missing"})
            QVERIFY(!call(*manager, "extensions.select", {{"slot", "themeLayout"}, {"contribution", QString(invalid)}}));
        QCOMPARE(selections().value("themeLayout").toString(), "test.layout/layout");

        QVERIFY(call(*manager, "extensions.select", {{"slot", "themeLayout"}, {"contribution", ""}}));
        QCOMPARE(selections().value("themeLayout").toString(), QString());

        // One invalid entry rejects the whole batch.
        QVERIFY(!call(*manager, "extensions.select",
                      {{"selections", QVariantMap{{"theme", "test.layout/palette"}, {"themeLayout", "builtin/ocean"}}}}));
        QCOMPARE(selections().value("theme").toString(), "builtin/ocean");
        QVERIFY(!call(*manager, "extensions.select", {{"selections", QVariantMap{}}}));
        QVERIFY(!call(*manager, "extensions.select", {{"selections", QVariantMap{{"themeLayout", 1}}}}));
        QVERIFY(!call(*manager, "extensions.select", {{"selections", "theme"}}));

        QVERIFY(call(*manager, "extensions.select",
                     {{"selections", QVariantMap{{"theme", "test.layout/palette"}, {"themeLayout", "test.layout/layout"}}}}));
        manager.reset();
        manager = startManager();
        QCOMPARE(selections().value("themeLayout").toString(), "test.layout/layout");

        // A disabled package keeps its selections; unchanged entries are not revalidated.
        QVERIFY(call(*manager, "extensions.disable", {{"id", "test.layout"}}));
        const auto keep = call(*manager, "extensions.select",
                               {{"selections", QVariantMap{{"theme", "test.layout/palette"}, {"themeLayout", ""}}}});
        QVERIFY2(keep, qPrintable(errorOf(keep)));
        QCOMPARE(selections().value("theme").toString(), "test.layout/palette");
        // Changing to a contribution of the disabled package is still rejected.
        QVERIFY(!call(*manager, "extensions.select", {{"slot", "themeLayout"}, {"contribution", "test.layout/layout"}}));

        // Uninstalling the package resets both selections to the defaults.
        QVERIFY(call(*manager, "extensions.uninstall", {{"id", "test.layout"}}));
        QCOMPARE(selections().value("theme").toString(), QString());
        QCOMPARE(selections().value("themeLayout").toString(), QString());
    }
```

如果 `theme("layout", {{"bottomPlayer", "Player.qml"}})` 因为 `QJsonObject` 的初始化列表推导出现编译错误，改成显式写法 `theme("layout", QJsonObject{{"bottomPlayer", "Player.qml"}})`，其他调用同理。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test themeUiCapabilities
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test themeLayoutSelection
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test zipPackagesAndThemes
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test uninstallClearsData
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test managementThemeAndShellInRealFrontend
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
git diff --check
```

## 完成标准

- [ ] 旧写法 `{slot, contribution}` 的行为不变：现有使用 `extensions.select` 的测试全部通过。
- [ ] `themeLayoutSelection`、`themeUiCapabilities` 通过。
- [ ] 全量测试没有新增失败。

## 提交

```sh
git add shared/themes/theme_resolution.h shared/themes/theme_resolution.cpp \
  backend/infrastructure/extensions/plugin_manager.h backend/infrastructure/extensions/plugin_manager.cpp \
  tests/native_extensions_test.cpp
git commit -m "feat(extensions): 支持主题界面选择与批量原子选择"
```
