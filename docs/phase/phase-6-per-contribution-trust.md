# Phase 6：主题包按用途授权

## 目标

现在含 QML 的主题包**不授权就不能启用**，所以用户“只想用它的配色”也必须先授权它的 QML。本阶段实现设计决定 D8：

1. 后端：`type: "theme"` 的主题包启用时不再需要信任。普通扩展的规则**不变**。
2. 前端：含 QML 的主题包在被信任之前，**不加载**它的任何 QML 入口（界面插槽和壁纸）。
3. 只选择配色时不弹授权；选择界面、一键应用含 QML 的整套主题时才弹授权。

## 风险说明（执行前必读）

- 这是**安全相关改动**。改动之后，阻止未授权主题 QML 被加载的唯一一道关卡在前端的 `ThemeController::rebuild()`。
- 主题包本身没有后台代码，启用它只会让它的配色和图片可用，不会执行任何代码；这是本次改动安全的前提。**不得**把这个放宽扩展到普通扩展（`type` 不是 `"theme"` 的包）。
- 主题包不能贡献 `slots`、`pages` 等其他界面入口（清单校验已经保证），所以它的 QML 只能通过 `ThemeController` 生成的描述进入 `ExtensionView`。执行者要在 4 中再确认一次这一点。

## 前置条件

- Phase 5 已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `backend/infrastructure/extensions/extension_manifest.h` | 新增 `needsTrustToRun()` |
| `backend/infrastructure/extensions/plugin_manager.cpp` | 启用检查改用 `needsTrustToRun()`；更新包时不再因新增代码而停用主题 |
| `frontend/qt-qml/src/controllers/theme_controller.cpp` | 未信任主题的 QML 不生成插槽；`ensureAvailable` 按用途决定是否请求信任 |
| `frontend/qt-qml/qml/components/ExtensionsPanel.qml` | 启用主题包时不弹信任对话框 |
| `tests/native_extensions_test.cpp` | 新增 `themePackagesRunWithoutTrust` |
| `tests/extensions_test.cpp` | 新增 `themeCodeLoadsOnlyAfterTrust` |

## 步骤

### 6.1 后端：区分“含代码”和“运行需要信任”

文件：`backend/infrastructure/extensions/extension_manifest.h`

找到：

```cpp
    bool needsTrust() const { return !isTheme() || themeHasCode; }
```

在它下面加入：

```cpp
    // Theme packages have no backend code; their QML is gated by the frontend until trusted.
    bool needsTrustToRun() const { return !isTheme(); }
```

**保留** `needsTrust()`，它仍用于对外报告 `hasCode`。

### 6.2 后端：启用检查改用 `needsTrustToRun()`

文件：`backend/infrastructure/extensions/plugin_manager.cpp`

先运行：

```sh
grep -n "needsTrust()" backend/infrastructure/extensions/plugin_manager.cpp
```

应当看到 5 处，按下表处理：

| 位置 | 代码 | 处理 |
| --- | --- | --- |
| `publicEntry` 中 | `{"hasCode", entry.manifest.needsTrust()},` | **不改** |
| `enable` 中 | `if (m_entries[id].manifest.needsTrust() && !m_entries[id].trusted) {` | 改为 `needsTrustToRun()` |
| `enable` 中，遍历依赖 | `if (m_entries[dependency].manifest.needsTrust() && !m_entries[dependency].trusted) {` | 改为 `needsTrustToRun()` |
| `startOne` 中 | `if (entry.manifest.needsTrust() && !entry.trusted) {` | 改为 `needsTrustToRun()` |
| `install` 中 | `const bool trustRetained = hadOld && old.trusted && (old.manifest.needsTrust() \|\| !manifest.themeHasCode);` | **不改** |

数量或代码与表格不一致时，停止并报告。

### 6.3 后端：更新主题包时不再停用

仍在 `plugin_manager.cpp` 的 `install` 中，找到：

```cpp
    const bool trustRetained = hadOld && old.trusted && (old.manifest.needsTrust() || !manifest.themeHasCode);
    const bool codeNeedsTrust = manifest.isTheme() && manifest.themeHasCode && !trustRetained;
    Entry entry;
    entry.directory = directory;
    entry.development = development;
    entry.manifest = manifest;
    entry.trusted = trustRetained;
    // Importing code into a formerly data-only theme waits for an explicit trust decision.
    entry.enabled = hadOld && old.enabled && !codeNeedsTrust;
    if (hadOld && (!old.enabled || codeNeedsTrust))
```

替换为：

```cpp
    const bool trustRetained = hadOld && old.trusted && (old.manifest.needsTrust() || !manifest.themeHasCode);
    Entry entry;
    entry.directory = directory;
    entry.development = development;
    entry.manifest = manifest;
    // QML newly added to a theme stays unloaded until the user trusts it; the package keeps running.
    entry.trusted = trustRetained;
    entry.enabled = hadOld && old.enabled;
    if (hadOld && !old.enabled)
```

`if` 后面的 `entry.previous = ...` 语句保持不变。替换后运行 `grep -n codeNeedsTrust backend -r`，**应当没有输出**。

### 6.4 确认主题包无法通过其他途径加载 QML

依次运行，并核对结论：

```sh
grep -n "themeOnly\|isTheme()" backend/infrastructure/extensions/extension_manifest.cpp
grep -rn "activeSlots\|contributions(" frontend/qt-qml/qml --include='*.qml' | head -20
```

需要确认：`extension_manifest.cpp` 对 `isTheme()` 的包只允许 `contributes.themes`。如果主题包还能贡献 `slots`、`pages`、`settings`、`toolbars` 中的任何一种，**停止并报告**：这种情况下需要额外的前端关卡。

### 6.5 前端：未信任的 QML 不生成插槽

文件：`frontend/qt-qml/src/controllers/theme_controller.cpp`

1. 在文件顶部匿名命名空间内（`themeCatalog()` 函数之后）加入：

```cpp
// Theme packages run without trust (their data is harmless), so QML entries
// are only exposed once the package itself is trusted.
bool codeAllowed(const QVariantMap &theme) {
  return !theme.value("hasCode").toBool() || theme.value("trusted").toBool();
}
```

2. 在 `rebuild()` 中找到 Phase 4 写的：

```cpp
    if (candidate.value("available").toBool() &&
        candidate.value("hasLayout").toBool())
      m_layout = candidate;
```

改为：

```cpp
    if (candidate.value("available").toBool() &&
        candidate.value("hasLayout").toBool() && codeAllowed(candidate))
      m_layout = candidate;
```

3. 在 `rebuild()` 中找到 Phase 4 写的：

```cpp
    add(wallpaper, "wallpaper",
        wallpaper.value("ui").toMap().value("wallpaper"));
```

改为：

```cpp
    if (codeAllowed(wallpaper))
      add(wallpaper, "wallpaper",
          wallpaper.value("ui").toMap().value("wallpaper"));
```

### 6.6 前端：按用途决定是否请求信任

把 Phase 4 写的 `ThemeController::ensureAvailable` 整体替换为：

```cpp
bool ThemeController::ensureAvailable(const QVariantMap &theme, bool trusted,
                                      const QString &target,
                                      std::function<void()> retry) {
  // Colors alone never need trust for theme packages. Layouts, one-click
  // themes and ordinary extensions (which run code) do.
  const bool codeNeeded = target != QLatin1String("appearance") ||
                          theme.value("origin") == QLatin1String("extension");
  const bool untrusted =
      theme.value("hasCode").toBool() && !theme.value("trusted").toBool();
  if (codeNeeded && untrusted && !trusted) {
    emit trustRequested(theme.value("id").toString(), target);
    return false;
  }
  // A granted trust must reach the backend even when the package already runs.
  if (theme.value("available").toBool() && !(untrusted && trusted))
    return true;
  request("extensions.enable",
          {{"id", theme.value("extensionId")}, {"trusted", trusted}},
          [retry](const QVariantMap &, const QString &error) {
            if (error.isEmpty())
              retry();
          });
  return false;
}
```

说明：

- `extensions.enable` 对**已经运行**的包传 `trusted: true` 时，后端会记录信任并保存，不会重启它（`enable` → `startOne` 发现已在运行就直接返回）。
- `extensions.enable` 成功后会返回完整快照，`ExtensionsController` 在触发完成回调**之前**就会更新条目，所以 `retry()` 读到的是最新的 `trusted` 和 `available`，不会循环请求。

### 6.7 扩展管理页：启用主题包不弹信任

文件：`frontend/qt-qml/qml/components/ExtensionsPanel.qml`

找到：

```qml
                                    else if (!card.modelData.trusted && card.modelData.hasCode !== false) trustDialog.open()
```

改为：

```qml
                                    else if (card.modelData.type !== "theme" && !card.modelData.trusted && card.modelData.hasCode !== false) trustDialog.open()
```

### 6.8 测试

**(a)** 文件：`tests/native_extensions_test.cpp`，在 `layeredThemeTokenResolution` 之后加入：

```cpp
    void themePackagesRunWithoutTrust() {
        auto manager = startManager();
        const QDir folder(m_home->filePath("trust-theme"));
        QVERIFY(QDir().mkpath(folder.path()));
        auto write = [&](const QString &name, const QByteArray &bytes) {
            QFile file(folder.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        auto package = [&](bool withCode, const QString &zipName) {
            QJsonObject theme{{"id", "default"}, {"title", "Trust"}, {"schemaVersion", 1},
                              {"variants", QJsonObject{{"light", QJsonObject{}}, {"dark", QJsonObject{}}}}};
            if (withCode)
                theme.insert("ui", QJsonObject{{"bottomPlayer", "Player.qml"}});
            const QJsonObject manifest{{"id", "test.trust"}, {"name", "Trust"}, {"version", "1.0.0"},
                                       {"apiVersion", 1}, {"type", "theme"},
                                       {"contributes", QJsonObject{{"themes", QJsonArray{theme}}}}};
            QFile::remove(folder.filePath("Player.qml"));
            if (!write("extension.json", QJsonDocument(manifest).toJson()))
                return QString();
            if (withCode && !write("Player.qml", "import QtQuick\nItem {}\n"))
                return QString();
            zip::Writer writer;
            if (!writer.addFile("extension.json", folder.filePath("extension.json")))
                return QString();
            if (withCode && !writer.addFile("Player.qml", folder.filePath("Player.qml")))
                return QString();
            const auto path = m_home->filePath(zipName);
            return writer.save(path) ? path : QString();
        };

        // A data-only package that later gains QML keeps running but is no longer trusted.
        const auto plain = package(false, "plain.nktheme.zip");
        QVERIFY(!plain.isEmpty());
        QVERIFY(call(*manager, "extensions.install", {{"path", plain}, {"themeOnly", true}}));
        QVERIFY(call(*manager, "extensions.enable", {{"id", "test.trust"}}));
        const auto code = package(true, "code.nktheme.zip");
        QVERIFY(!code.isEmpty());
        const auto updated = call(*manager, "extensions.install",
                                  {{"path", code}, {"themeOnly", true}, {"replace", true}, {"expectedId", "test.trust"}});
        QVERIFY2(updated, qPrintable(errorOf(updated)));
        QTRY_COMPARE(entry(*manager, "test.trust").value("state").toString(), "running");
        QVERIFY(entry(*manager, "test.trust").value("hasCode").toBool());
        QVERIFY(!entry(*manager, "test.trust").value("trusted").toBool());

        // Code themes enable without trust, and trust can be granted while running.
        QVERIFY(call(*manager, "extensions.disable", {{"id", "test.trust"}}));
        const auto enabled = call(*manager, "extensions.enable", {{"id", "test.trust"}});
        QVERIFY2(enabled, qPrintable(errorOf(enabled)));
        QCOMPARE(entry(*manager, "test.trust").value("state").toString(), "running");
        QVERIFY(call(*manager, "extensions.enable", {{"id", "test.trust"}, {"trusted", true}}));
        QVERIFY(entry(*manager, "test.trust").value("trusted").toBool());
        manager.reset();
        manager = startManager();
        QVERIFY(entry(*manager, "test.trust").value("trusted").toBool());

        // Ordinary extensions still require trust.
        install(*manager, examples + "/example.lyrics");
        QVERIFY(!call(*manager, "extensions.enable", {{"id", "example.lyrics"}}));
    }
```

如果 `zip::Writer::addFile` 的签名与上面不同，参考 `zipPackagesAndThemes` 中的用法调整。

**(b)** 文件：`tests/extensions_test.cpp`，在 `themeAppearanceAndLayoutCombine` 之后加入 `themeCodeLoadsOnlyAfterTrust`。

搭建方式与 Phase 4 的 `themeAppearanceAndLayoutCombine` 相同：复制它从开头到 `client.connectBackend();` 为止的代码，然后做以下替换：

- socket 和数据库文件名改为 `"trust.sock"`、`"trust.sqlite3"`；
- 目录名 `"combo"` 改为 `"trust"`；
- `Player.qml` 中的 `objectName` 改为 `'trustPlayer'`；另外写一个 `Wallpaper.qml`：`"import QtQuick\nItem {}"`；
- 包 ID 改为 `"test.trust"`，主题列表改为下面两个：

```cpp
      const QJsonObject withLayout{
          {"id", "default"}, {"title", "Trust"}, {"schemaVersion", 1},
          {"variants", variants("#00AA00")},
          {"ui", QJsonObject{{"bottomPlayer", "Player.qml"}}}};
      const QJsonObject withWallpaper{
          {"id", "wall"}, {"title", "Wall"}, {"schemaVersion", 1},
          {"variants", variants("#0000AA")},
          {"ui", QJsonObject{{"wallpaper", "Wallpaper.qml"}}}};
```

- 安装后**不要**调用 `extensions.enable`（删除复制来的那段 enable 代码）。

然后写断言：

```cpp
      QTRY_COMPARE(app.theme->items().size(), 5);
      app.theme->setMode("dark");
      auto idle = [&] { QTRY_VERIFY(!app.theme->busy()); };
      auto trustPlayer = [&] {
        return window->findChild<QObject *>("trustPlayer");
      };
      QSignalSpy trust(app.theme, &ThemeController::trustRequested);

      // Colors only: no trust prompt, the package starts, its QML stays unloaded.
      app.theme->selectAppearance("test.trust/default");
      QTRY_COMPARE(app.theme->activeId(), QString("test.trust/default"));
      QCOMPARE(trust.size(), 0);
      QCOMPARE(app.theme->tokens().value("accentPrimary").toString(),
               QString("#00AA00"));
      QCOMPARE(app.theme->layoutSelectedId(), QString());
      QTest::qWait(50);
      QVERIFY(!trustPlayer());

      // An untrusted wallpaper is not exposed through "follow current theme".
      idle();
      app.theme->selectAppearance("test.trust/wall");
      QTRY_COMPARE(app.theme->activeId(), QString("test.trust/wall"));
      QVERIFY(!app.theme->uiSlots().contains("wallpaper"));

      // Choosing the layout asks for trust and loads nothing until granted.
      idle();
      app.theme->selectLayout("test.trust/default");
      QCOMPARE(trust.size(), 1);
      QCOMPARE(trust.at(0).at(0).toString(), QString("test.trust/default"));
      QCOMPARE(trust.at(0).at(1).toString(), QString("layout"));
      QCOMPARE(app.theme->layoutSelectedId(), QString());
      idle();
      app.theme->selectLayout("test.trust/default", true);
      QTRY_COMPARE(app.theme->activeLayoutId(), QString("test.trust/default"));
      QTRY_VERIFY(trustPlayer());
      QTRY_VERIFY(app.theme->uiSlots().contains("wallpaper"));
```

最后照 Phase 4 的方式复制收尾代码（如果有）。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test themePackagesRunWithoutTrust
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test trustIsRequiredBeforeLoading
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test zipPackagesAndThemes
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themeCodeLoadsOnlyAfterTrust
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themePackagesAndAppearance
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themeAppearanceAndLayoutCombine
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_wallpaper_test
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
git diff --check
```

如果现有测试因为“主题包更新后被停用”的旧行为而失败，说明它断言的正是本阶段要改变的行为：**停下来报告测试名和断言内容**，不要直接删掉断言。

## 完成标准

- [ ] 主题包不授权也能启用；普通扩展仍然必须授权（`trustIsRequiredBeforeLoading` 通过）。
- [ ] 只选配色时不弹授权，主题的 QML 不会被创建。
- [ ] 选择界面时弹出授权；授权前不加载，授权后加载。
- [ ] 未信任主题的壁纸不会出现在 `uiSlots.wallpaper` 中。
- [ ] 全量测试没有新增失败。

## 提交

```sh
git add backend/infrastructure/extensions/extension_manifest.h backend/infrastructure/extensions/plugin_manager.cpp \
  frontend/qt-qml/src/controllers/theme_controller.cpp frontend/qt-qml/qml/components/ExtensionsPanel.qml \
  tests/native_extensions_test.cpp tests/extensions_test.cpp
git commit -m "feat(theme): 主题包按用途授权，配色无需信任代码"
```
