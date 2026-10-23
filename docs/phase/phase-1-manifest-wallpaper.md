# Phase 1：清单新增顶层 `wallpaper`

## 目标

1. 主题贡献支持顶层字段 `wallpaper`（W1），校验规则与其他 QML 入口一致。
2. `ui.wallpaper` 不再被接受，并给出明确的报错（W2）。
3. `providesWallpaper()` 改为读取新字段（W3），导出结果的 `includesQml` 把壁纸也算进去。
4. 示例主题改用新写法。

本阶段结束后，前端仍然读取 `ui.wallpaper`，所以主题壁纸**暂时不会显示**。Phase 2 会修复，两个阶段之间不要发布。

## 前置条件

- Phase 0 已完成。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `backend/infrastructure/extensions/theme_package.cpp` | 校验顶层 `wallpaper`，拒绝 `ui.wallpaper`，修正 `includesQml` |
| `shared/themes/theme_resolution.{h,cpp}` | `providesWallpaper()` 读取顶层字段 |
| `examples/extensions/theme-qml/extension.json` | 示例改用顶层 `wallpaper` |
| `tests/native_extensions_test.cpp` | 更新 `themeUiCapabilities`、`themeLayoutSelection`，新增 `themeWallpaperField` |

## 步骤

### 1.1 校验

文件：`backend/infrastructure/extensions/theme_package.cpp`

1. 在匿名命名空间里，把

```cpp
const QStringList uiSlots{"background", "shell", "bottomPlayer", "wallpaper"};
```

改为

```cpp
const QStringList uiSlots{"background", "shell", "bottomPlayer"};
```

2. 在 `validate()` 中找到

```cpp
        const auto ui = theme.value("ui").toMap();
        QVariantList sources;
```

在这两行之间插入：

```cpp
        if (ui.contains("wallpaper"))
            return failure("Theme wallpaper moved from ui.wallpaper to wallpaper");
```

这条检查必须放在遍历 `ui` 的循环之前，否则会先报出通用的 `Unknown theme UI slot: wallpaper`。

3. 在 `validate()` 中找到 `if (theme.contains("ui")) { ... }` 整个代码块，在它的右花括号**之后**、`qint64 pixels = 0;` **之前**加入：

```cpp
    if (theme.contains("wallpaper")) {
        if (!modern)
            return failure("Theme wallpaper requires schemaVersion 1");
        const auto wallpaper = theme.value("wallpaper");
        if (wallpaper.typeId() != QMetaType::QString || !wallpaper.toString().endsWith(".qml"))
            return failure("Theme wallpaper source must be QML");
        if (auto file = themeFile(directory, wallpaper.toString()); !file)
            return file.error();
    }
```

`themeFile()` 已经负责拒绝越界路径和不存在的文件，这里不要再写一遍。

4. 在 `exportTheme()` 末尾，把

```cpp
    return QVariantMap{{"path", destination}, {"includesQml", full && theme.contains("ui")}};
```

改为

```cpp
    return QVariantMap{{"path", destination},
                       {"includesQml", full && (theme.contains("ui") || theme.contains("wallpaper"))}};
```

### 1.2 共享判断

文件：`shared/themes/theme_resolution.h`

把

```cpp
/// True when the theme declares ui.wallpaper.
```

改为

```cpp
/// True when the theme declares a top-level wallpaper entry.
```

文件：`shared/themes/theme_resolution.cpp`

把 `providesWallpaper()` 的函数体改为：

```cpp
    return !theme.value("wallpaper").toString().isEmpty();
```

`providesLayout()` 不改。

### 1.3 示例主题

文件：`examples/extensions/theme-qml/extension.json`

1. 从 `"ui"` 对象中删除 `"wallpaper": "Wallpaper.qml"` 这一行，并删掉 `"bottomPlayer": "Player.qml"` 行尾的逗号。
2. 在 `"ui"` 对象之前加入 `"wallpaper": "Wallpaper.qml",`，与 `"ui"` 同级、同缩进。

修改后应该是：

```json
        "wallpaper": "Wallpaper.qml",
        "ui": {
          "background": "Background.qml",
          "bottomPlayer": "Player.qml"
        }
```

用 `python3 -m json.tool examples/extensions/theme-qml/extension.json > /dev/null` 确认 JSON 合法。

### 1.4 测试

文件：`tests/native_extensions_test.cpp`

1. `themeUiCapabilities()`：

   - 把 `QVERIFY(!themes::providesLayout({{"ui", QVariantMap{{"wallpaper", "Wallpaper.qml"}}}}));` 改为 `QVERIFY(!themes::providesLayout({{"wallpaper", "Wallpaper.qml"}}));`
   - 把最后四条 `providesWallpaper` 断言改为：

```cpp
        QVERIFY(themes::providesWallpaper({{"wallpaper", "Wallpaper.qml"}}));
        QVERIFY(!themes::providesWallpaper({}));
        QVERIFY(!themes::providesWallpaper({{"wallpaper", ""}}));
        QVERIFY(!themes::providesWallpaper({{"ui", QVariantMap{{"wallpaper", "Wallpaper.qml"}}}}));
```

最后一条用来确认旧写法不再被识别。

2. `themeLayoutSelection()`：找到

```cpp
                                                              theme("wall", {{"wallpaper", "Wallpaper.qml"}})}}}}};
```

在构造 `manifest` 之前加入：

```cpp
        auto wall = theme("wall", {});
        wall.insert("wallpaper", "Wallpaper.qml");
```

再把上面那一行中的 `theme("wall", {{"wallpaper", "Wallpaper.qml"}})` 换成 `wall`。断言部分不变（`test.layout/wall` 仍然不能被选为界面）。

3. 在 `themeUiCapabilities()` 之后新增测试函数：

```cpp
    void themeWallpaperField() {
        const QDir folder(m_home->filePath("wallpaper-field"));
        QVERIFY(QDir().mkpath(folder.path()));
        QFile file(folder.filePath("Wallpaper.qml"));
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write("import QtQuick\nItem {}\n") > 0);
        file.close();
        const QVariantMap variants{{"light", QVariantMap{}}, {"dark", QVariantMap{}}};
        auto theme = [&](const QVariantMap &extra) {
            QVariantMap value{{"id", "default"}, {"schemaVersion", 1}, {"variants", variants}};
            value.insert(extra);
            return value;
        };
        auto message = [&](const QVariantMap &value) {
            const auto result = themes::validate(value, folder.path(), true);
            return result ? QString() : result.error().message;
        };

        QCOMPARE(message(theme({{"wallpaper", "Wallpaper.qml"}})), QString());
        QCOMPARE(message(theme({{"ui", QVariantMap{{"wallpaper", "Wallpaper.qml"}}}})),
                 QString("Theme wallpaper moved from ui.wallpaper to wallpaper"));
        // Legacy themes only pass the non-strict path (ordinary extensions), so check it there.
        const auto legacy = themes::validate({{"id", "legacy"}, {"wallpaper", "Wallpaper.qml"}}, folder.path(), false);
        QVERIFY(!legacy);
        QCOMPARE(legacy.error().message, QString("Theme wallpaper requires schemaVersion 1"));
        for (const auto &invalid : {QVariant(""), QVariant(1), QVariant("Wallpaper.js")})
            QCOMPARE(message(theme({{"wallpaper", invalid}})), QString("Theme wallpaper source must be QML"));
        QVERIFY(!message(theme({{"wallpaper", "Missing.qml"}})).isEmpty());
        QVERIFY(!message(theme({{"wallpaper", "../Wallpaper.qml"}})).isEmpty());
    }
```

`strict = true` 会先要求 `schemaVersion` 与明暗变体，所以旧格式主题那一条单独用 `strict = false` 调用。

4. 在文件中搜索 `"ui", QJsonObject{{"wallpaper"` 和 `"ui", QVariantMap{{"wallpaper"`，确认除了第 1、3 步故意保留的旧写法断言之外，没有其他用法。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
./build/tests/nekotune_native_extensions_test themeUiCapabilities themeWallpaperField themeLayoutSelection zipPackagesAndThemes
python3 -m json.tool examples/extensions/theme-qml/extension.json > /dev/null
git diff --check
```

不要求本阶段全量 CTest 通过：`tests/extensions_test.cpp` 和 `tests/wallpaper_test.cpp` 仍然在用 `ui.wallpaper`，会在 Phase 2 修复。运行一次全量 CTest，**只记录**新增的失败，并确认它们都属于这两个文件中与壁纸有关的测试；有其他失败时停下来报告。

## 完成标准

- [ ] 1.1–1.4 全部完成。
- [ ] 上面列出的四个后端测试通过。
- [ ] 新增的全量失败只来自 `extensions_test`、`wallpaper_test` 中与主题壁纸有关的测试，并已记录。

## 提交

```sh
git add backend/infrastructure/extensions/theme_package.cpp shared/themes/theme_resolution.h \
  shared/themes/theme_resolution.cpp examples/extensions/theme-qml/extension.json tests/native_extensions_test.cpp
git commit -m "feat(theme)!: 主题壁纸改用顶层 wallpaper 字段

BREAKING CHANGE: 不再接受 ui.wallpaper，主题包需改为顶层 wallpaper。"
```
