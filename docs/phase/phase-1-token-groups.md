# Phase 1：为变量标注归属

## 目标

在 `shared/themes/catalog.json` 中为 68 个变量都加上 `group` 字段，并用测试保证以后新增的变量也必须写明归属。**本阶段不改变任何运行时行为。**

## 前置条件

- Phase 0 已完成。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `shared/themes/catalog.json` | 每个 `tokens.<名字>` 对象增加 `"group"` |
| `tests/native_extensions_test.cpp` | 新增测试函数 `themeCatalogTokenGroups` |

## 归属表（定稿，不得修改）

| group | 变量 |
| --- | --- |
| `appearance` | `bgCanvas` `bgSidebar` `bgSurface` `bgRaised` `bgHover` `bgSelected` `bgCard` `borderSubtle` `borderControl` `textPrimary` `textSecondary` `textMuted` `textDisabled` `textOnAccent` `accentPrimary` `accentHover` `accentPressed` `accentSecondary` `statusSuccess` `statusWarning` `statusError` `statusErrorBg` `statusInfo` `bgCanvasTop` `bgElevated` `accentGlow` `roseGlow` `glowOpacity` `fontFamily` `fontFamilyMonospace` |
| `layout` | `sidebarWidthWide` `sidebarWidthCompact` `bottomBarHeight` `songRowHeight` `songThumbnailSize` `controlMinHeight` `iconButtonSize` `iconButtonDenseSize` `playButtonSize` `artworkCompact` `artworkWide` |
| `shared` | `fontTitle` `fontSongTitle` `fontDialogTitle` `fontBody` `fontBodySecondary` `fontCaption` `fontLyricsWide` `fontLyricsCompact` `lineHeightBody` `lineHeightLyrics` `spaceXs` `spaceSm` `spaceMd` `spaceLg` `spaceXl` `space2Xl` `space3Xl` `radiusSm` `radiusMd` `radiusLg` `radiusFull` `durationFast` `durationNormal` `durationDrawer` `durationSmooth` `durationScroll` |
| `host` | `reducedMotion` |

合计 30 + 11 + 26 + 1 = 68 个。

## 步骤

### 1.1 用脚本写入 `group`

在**仓库之外**创建脚本 `/tmp/nekotune_add_groups.py`，内容如下：

```python
import json
import sys

GROUPS = {
    "appearance": [
        "bgCanvas", "bgSidebar", "bgSurface", "bgRaised", "bgHover", "bgSelected", "bgCard",
        "borderSubtle", "borderControl", "textPrimary", "textSecondary", "textMuted",
        "textDisabled", "textOnAccent", "accentPrimary", "accentHover", "accentPressed",
        "accentSecondary", "statusSuccess", "statusWarning", "statusError", "statusErrorBg",
        "statusInfo", "bgCanvasTop", "bgElevated", "accentGlow", "roseGlow", "glowOpacity",
        "fontFamily", "fontFamilyMonospace",
    ],
    "layout": [
        "sidebarWidthWide", "sidebarWidthCompact", "bottomBarHeight", "songRowHeight",
        "songThumbnailSize", "controlMinHeight", "iconButtonSize", "iconButtonDenseSize",
        "playButtonSize", "artworkCompact", "artworkWide",
    ],
    "shared": [
        "fontTitle", "fontSongTitle", "fontDialogTitle", "fontBody", "fontBodySecondary",
        "fontCaption", "fontLyricsWide", "fontLyricsCompact", "lineHeightBody",
        "lineHeightLyrics", "spaceXs", "spaceSm", "spaceMd", "spaceLg", "spaceXl", "space2Xl",
        "space3Xl", "radiusSm", "radiusMd", "radiusLg", "radiusFull", "durationFast",
        "durationNormal", "durationDrawer", "durationSmooth", "durationScroll",
    ],
    "host": ["reducedMotion"],
}

path = sys.argv[1]
with open(path, encoding="utf-8") as file:
    catalog = json.load(file)

lookup = {name: group for group, names in GROUPS.items() for name in names}
tokens = catalog["tokens"]
missing = sorted(set(tokens) - set(lookup))
unknown = sorted(set(lookup) - set(tokens))
if missing or unknown:
    sys.exit(f"missing={missing} unknown={unknown}")

for name, rule in tokens.items():
    # Keep "type" first and place "group" right after it.
    updated = {"type": rule["type"], "group": lookup[name]}
    updated.update({key: value for key, value in rule.items() if key not in ("type", "group")})
    tokens[name] = updated

with open(path, "w", encoding="utf-8") as file:
    file.write(json.dumps(catalog, indent=2, ensure_ascii=False) + "\n")
```

运行：

```sh
uv run --no-project python -I /tmp/nekotune_add_groups.py shared/themes/catalog.json
```

- 脚本以 `missing=... unknown=...` 退出时，说明 `catalog.json` 中的变量与归属表不一致。**停止并报告**，不要自行补充归属。

### 1.2 检查 diff

```sh
git diff --stat shared/themes/catalog.json
git diff shared/themes/catalog.json | grep '^[-+]' | grep -v '^+++\|^---' | grep -v '"group"' | head
```

- 第一条应显示约 68 行新增、0 行删除。
- 第二条**必须没有输出**；有输出说明格式被改动了，用 `git checkout shared/themes/catalog.json` 还原后排查。

### 1.3 确认校验器不受影响

打开 `backend/infrastructure/extensions/theme_package.cpp`，找到 `validateTokens`。确认它只读取规则的 `type`、`min`、`max`，不会因为规则对象里多了 `group` 而报错。**不需要修改这个文件。**

### 1.4 新增测试

在 `tests/native_extensions_test.cpp` 中：

1. 确认文件已经包含 `infrastructure/extensions/theme_package.h`（用 `grep -n theme_package.h tests/native_extensions_test.cpp` 检查）。没有的话，在其他 `#include "infrastructure/...` 行旁边加上。
2. 在 `private slots:` 区域、`void zipPackagesAndThemes()` 函数**之前**，加入：

```cpp
    void themeCatalogTokenGroups() {
        // Separate appearance and layout selection depends on every token declaring its owner.
        const QStringList groups{"appearance", "layout", "shared", "host"};
        const auto tokens = themes::catalog().value("tokens").toMap();
        QCOMPARE(tokens.size(), 68);
        for (auto it = tokens.cbegin(); it != tokens.cend(); ++it)
            QVERIFY2(groups.contains(it.value().toMap().value("group").toString()), qPrintable(it.key()));
        QCOMPARE(tokens.value("accentPrimary").toMap().value("group").toString(), "appearance");
        QCOMPARE(tokens.value("sidebarWidthWide").toMap().value("group").toString(), "layout");
        QCOMPARE(tokens.value("radiusMd").toMap().value("group").toString(), "shared");
        QCOMPARE(tokens.value("reducedMotion").toMap().value("group").toString(), "host");
    }
```

如果编译时报 `themes` 未声明，检查文件顶部的 `using namespace`；按文件已有写法补全命名空间，例如 `nekotune::themes::catalog()`。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test themeCatalogTokenGroups
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_native_extensions_test zipPackagesAndThemes
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themePackagesAndAppearance
git diff --check
```

## 完成标准

- [ ] `catalog.json` 只新增了 68 行 `"group"`，没有其他改动。
- [ ] `themeCatalogTokenGroups` 通过。
- [ ] `zipPackagesAndThemes`、`themePackagesAndAppearance` 仍然通过。

## 提交

```sh
git add shared/themes/catalog.json tests/native_extensions_test.cpp
git commit -m "feat(theme): 为主题变量标注外观与界面归属"
```
