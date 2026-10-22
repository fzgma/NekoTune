# Phase 2：前后端共用一份变量合成实现

## 目标

现在后端的 `resolveTokens`（`theme_package.cpp`，用于导出）和前端的 `ThemeController::resolve`（用于显示）各有一份几乎相同的合成逻辑。本阶段把它们合并成一个共享库，后续阶段只需要修改一处。

**这是纯重构，用户可见行为不变。** 唯一的语义统一是：旧版只含 `tokens` 的主题在浅色模式下，`bgCard` 不再被改写成 `bgSurface`（采用后端的现有行为）。当前浅色配色中两者都是 `#FFFFFF`，所以显示结果相同。

## 前置条件

- Phase 1 已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `shared/themes/theme_resolution.h` | 新建 |
| `shared/themes/theme_resolution.cpp` | 新建 |
| `CMakeLists.txt`（根目录） | 新增静态库 `nekotune_theme_resolution` |
| `backend/CMakeLists.txt` | `nekotune_infrastructure` 链接新库 |
| `frontend/qt-qml/CMakeLists.txt` | `nekotune_frontend` 链接新库 |
| `backend/infrastructure/extensions/theme_package.cpp` | 删除本地的 `resolveTokens`，改用共享函数 |
| `frontend/qt-qml/src/controllers/theme_controller.cpp` | `ThemeDefaults::defaults` 和 `ThemeController::resolve` 改用共享函数 |
| `tests/native_extensions_test.cpp` | 新增测试函数 `sharedThemeTokenResolution` |

## 步骤

### 2.1 新建头文件 `shared/themes/theme_resolution.h`

```cpp
#pragma once
#include <QVariantMap>

// Token resolution shared by the backend (theme export) and the frontend (display).
// The catalog is passed in because each side loads shared/themes/catalog.json from its own resources.
namespace nekotune::themes {
/// Catalog defaults for one color scheme: every token's default, plus the "light" overrides when !dark.
QVariantMap baseTokens(const QVariantMap &catalog, bool dark);
/// Final token values for one theme descriptor. Order: base values, common "tokens", then the
/// variant "tokens" of the requested scheme. Unknown token names are ignored.
QVariantMap resolveThemeTokens(const QVariantMap &catalog, const QVariantMap &theme, bool dark);
} // namespace nekotune::themes
```

### 2.2 新建实现文件 `shared/themes/theme_resolution.cpp`

```cpp
#include "themes/theme_resolution.h"

namespace nekotune::themes {
QVariantMap baseTokens(const QVariantMap &catalog, bool dark) {
    const auto rules = catalog.value("tokens").toMap();
    QVariantMap result;
    for (auto it = rules.cbegin(); it != rules.cend(); ++it)
        result.insert(it.key(), it.value().toMap().value("default"));
    if (!dark) {
        const auto light = catalog.value("light").toMap();
        for (auto it = light.cbegin(); it != light.cend(); ++it)
            if (rules.contains(it.key()))
                result.insert(it.key(), it.value());
    }
    return result;
}

QVariantMap resolveThemeTokens(const QVariantMap &catalog, const QVariantMap &theme, bool dark) {
    auto result = baseTokens(catalog, dark);
    // Legacy token-only themes describe a dark palette; light mode keeps the built-in light values.
    if (!dark && !theme.contains("schemaVersion"))
        return result;
    const auto rules = catalog.value("tokens").toMap();
    const auto common = theme.value("tokens").toMap();
    const auto variant =
        theme.value("variants").toMap().value(dark ? "dark" : "light").toMap().value("tokens").toMap();
    for (const auto &values : {common, variant})
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            if (rules.contains(it.key()))
                result.insert(it.key(), it.value());
    if (!common.contains("bgCard") && !variant.contains("bgCard"))
        result.insert("bgCard", result.value("bgSurface"));
    return result;
}
} // namespace nekotune::themes
```

### 2.3 根目录 `CMakeLists.txt`

找到以下三行：

```cmake
add_library(nekotune_paths STATIC shared/app_paths.cpp)
target_include_directories(nekotune_paths PUBLIC ${CMAKE_SOURCE_DIR}/shared)
target_link_libraries(nekotune_paths PUBLIC Qt6::Core)
```

紧接在它们**下面**加入：

```cmake

add_library(nekotune_theme_resolution STATIC shared/themes/theme_resolution.cpp shared/themes/theme_resolution.h)
target_include_directories(nekotune_theme_resolution PUBLIC ${CMAKE_SOURCE_DIR}/shared)
target_link_libraries(nekotune_theme_resolution PUBLIC Qt6::Core)
```

### 2.4 `backend/CMakeLists.txt`

找到：

```cmake
target_link_libraries(nekotune_infrastructure PUBLIC nekotune_domain nekotune_storage nekotune_paths Qt6::Network Qt6::Gui Qt6::Multimedia)
```

改为（在 `nekotune_paths` 后面加 `nekotune_theme_resolution`）：

```cmake
target_link_libraries(nekotune_infrastructure PUBLIC nekotune_domain nekotune_storage nekotune_paths nekotune_theme_resolution Qt6::Network Qt6::Gui Qt6::Multimedia)
```

### 2.5 `frontend/qt-qml/CMakeLists.txt`

找到：

```cmake
target_link_libraries(nekotune_frontend PUBLIC nekotune_paths Qt6::Core Qt6::Network Qt6::Quick Qt6::Qml Qt6::Widgets)
```

改为：

```cmake
target_link_libraries(nekotune_frontend PUBLIC nekotune_paths nekotune_theme_resolution Qt6::Core Qt6::Network Qt6::Quick Qt6::Qml Qt6::Widgets)
```

### 2.6 后端改用共享函数

文件：`backend/infrastructure/extensions/theme_package.cpp`

1. 在文件顶部的 `#include` 区域加入 `#include "themes/theme_resolution.h"`。
2. **删除**匿名命名空间中的整个 `QVariantMap resolveTokens(const QVariantMap &theme, const QString &mode) { ... }` 函数（以 `QVariantMap resolveTokens(` 开头、到对应的 `}` 结束，约 20 行）。
3. 找到 `exportTheme` 中唯一的调用：

   ```cpp
   QVariantMap variant{{"tokens", resolveTokens(theme, mode)}};
   ```

   改为：

   ```cpp
   QVariantMap variant{{"tokens", resolveThemeTokens(catalog(), theme, mode == QLatin1String("dark"))}};
   ```

   `theme_package.cpp` 本身就在 `namespace nekotune::themes` 中，所以不需要写命名空间前缀。
4. 运行 `grep -n "resolveTokens" backend -r`，**应当没有任何输出**。

### 2.7 前端改用共享函数

文件：`frontend/qt-qml/src/controllers/theme_controller.cpp`

1. 在 `#include "ipc_client.h"` 下方加入 `#include "themes/theme_resolution.h"`。
2. 把 `ThemeDefaults::defaults` 的函数体整体替换为：

   ```cpp
   QVariantMap ThemeDefaults::defaults(bool dark) const {
     return nekotune::themes::baseTokens(themeCatalog(), dark);
   }
   ```

3. 把 `ThemeController::resolve` 的函数体整体替换为：

   ```cpp
   QVariantMap ThemeController::resolve(const QVariantMap &theme,
                                        bool dark) const {
     return nekotune::themes::resolveThemeTokens(m_catalog, theme, dark);
   }
   ```

4. 头文件 `theme_controller.h` **不需要修改**，`resolve` 的声明保持不变。

### 2.8 新增测试

文件：`tests/native_extensions_test.cpp`

1. 在 `#include` 区域加入 `#include "themes/theme_resolution.h"`。如果 Phase 1 没有加 `#include "infrastructure/extensions/theme_package.h"`，这里也一并加上。
2. 在 `themeCatalogTokenGroups` 函数**之后**加入：

```cpp
    void sharedThemeTokenResolution() {
        const auto &catalog = themes::catalog();
        const auto rules = catalog.value("tokens").toMap();
        const auto dark = themes::baseTokens(catalog, true);
        QCOMPARE(dark.size(), rules.size());
        QCOMPARE(dark.value("bgCanvas").toString(), rules.value("bgCanvas").toMap().value("default").toString());
        const auto light = themes::baseTokens(catalog, false);
        QCOMPARE(light.value("bgCanvas").toString(), catalog.value("light").toMap().value("bgCanvas").toString());

        const QVariantMap theme{
            {"schemaVersion", 1},
            {"tokens", QVariantMap{{"accentPrimary", "#111111"}, {"radiusMd", 20}, {"notAToken", 1}}},
            {"variants",
             QVariantMap{{"dark", QVariantMap{{"tokens", QVariantMap{{"accentPrimary", "#222222"},
                                                                     {"bgSurface", "#333333"}}}}},
                         {"light", QVariantMap{{"tokens", QVariantMap{}}}}}}};
        const auto resolved = themes::resolveThemeTokens(catalog, theme, true);
        QCOMPARE(resolved.value("accentPrimary").toString(), "#222222");
        QCOMPARE(resolved.value("radiusMd").toInt(), 20);
        QVERIFY(!resolved.contains("notAToken"));
        QCOMPARE(resolved.value("bgCard").toString(), "#333333");
        QCOMPARE(themes::resolveThemeTokens(catalog, theme, false).value("accentPrimary").toString(), "#111111");

        auto explicitCard = theme;
        explicitCard["tokens"] = QVariantMap{{"bgCard", "#444444"}};
        QCOMPARE(themes::resolveThemeTokens(catalog, explicitCard, true).value("bgCard").toString(), "#444444");

        const QVariantMap legacy{{"tokens", QVariantMap{{"accentPrimary", "#555555"}}}};
        QVERIFY(themes::resolveThemeTokens(catalog, legacy, false) == light);
        QCOMPARE(themes::resolveThemeTokens(catalog, legacy, true).value("accentPrimary").toString(), "#555555");
    }
```

## 验证命令

```sh
cmake -S . -B build
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test sharedThemeTokenResolution
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test zipPackagesAndThemes
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themeModesAndPersistenceFailures
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themePackagesAndAppearance
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themeStartup
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
git diff --check
```

全量 `ctest` 的失败列表必须与 Phase 0 的基线相同。

## 完成标准

- [ ] `grep -rn "resolveTokens" backend frontend` 没有输出。
- [ ] `ThemeController::resolve` 和 `ThemeDefaults::defaults` 的函数体都只有一行，调用共享函数。
- [ ] 上面列出的测试全部通过，全量测试没有新增失败。
- [ ] 依赖边界检查通过。

## 提交

```sh
git add CMakeLists.txt backend/CMakeLists.txt frontend/qt-qml/CMakeLists.txt \
  shared/themes/theme_resolution.h shared/themes/theme_resolution.cpp \
  backend/infrastructure/extensions/theme_package.cpp \
  frontend/qt-qml/src/controllers/theme_controller.cpp \
  tests/native_extensions_test.cpp
git commit -m "refactor(theme): 前后端共用主题变量合成实现"
```
