# Phase 8：外观页支持分开选择

## 目标

在外观页（`AppearancePanel.qml`，恢复窗口也复用它）中提供 Phase 4 实现的能力：

1. 页面顶部显示当前的配色和界面。
2. 主题卡片：点击卡片仍是“一键应用”（`selectTheme`）；含界面的卡片额外提供“只用配色”“只用界面”两个按钮。
3. 卡片标记分别显示“配色”“界面”是否正在使用。
4. 重置按钮拆成三个：恢复默认配色、恢复内置界面、全部恢复默认。
5. 界面不可用时显示提示。

## 前置条件

- Phase 7 已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `frontend/qt-qml/qml/components/AppearancePanel.qml` | 界面改动 |
| `frontend/qt-qml/i18n/zh.json`、`en.json` | 新文案与修改的文案 |
| `tests/qml/tst_appearance_panel.qml` | 新建 |

## 界面规格

### 顶部区域（按从上到下的顺序）

| 位置 | 内容 | objectName |
| --- | --- | --- |
| 原有 | 标题“外观”、说明文字（文案更新） | — |
| **新增** | 一行：`当前配色` + 配色主题标题 | `currentPaletteLabel`（标题那个 Label） |
| **新增** | 一行：`当前界面` + 界面主题标题，或“内置界面” | `currentLayoutLabel`（标题那个 Label） |
| 原有 | 显示模式选择 | `themeModePicker` |
| 原有（Phase 5） | 减少动态效果 | `reducedMotionCheck` |
| 原有，修改 | 按钮行：导入主题、**恢复默认配色**、**恢复内置界面**、全部恢复默认 | 新：`resetAppearanceButton`、`resetLayoutButton`；原有 `resetThemeButton` 改为“全部恢复默认” |
| 原有 | 更新提示、错误/通知、警告 | — |
| **新增** | 界面不可用提示（`layoutUnavailable` 为真且不在安全模式时显示） | `layoutUnavailableLabel` |

### 主题卡片

| 元素 | 规格 |
| --- | --- |
| 边框高亮 | 配色**或**界面正在使用这个主题时高亮（原来只看配色） |
| 标记（原来的 `✓`） | 两项都在用：`配色 · 界面`；只有配色：`配色`；只有界面：`界面`；都不是：空。objectName：`themeBadge_<id>` |
| 信息行 | 在原有内容后，`hasLayout` 为真时追加 ` · 含界面`，`hasWallpaper` 为真时追加 ` · 含壁纸`。原有的 ` · QML` 保留 |
| 点击卡片 | 不变：`selectTheme(id)` |
| **新按钮**“只用配色” | 只在 `hasLayout` 为真时显示。objectName：`applyPalette_<id>`。点击调用 `selectAppearance(id)` |
| **新按钮**“只用界面” | 只在 `hasLayout` 为真时显示。objectName：`applyLayout_<id>`。点击调用 `selectLayout(id)`；安全模式下禁用 |
| 两个新按钮的位置 | 放在卡片底部按钮行（`Flow`）的最前面，在“更新主题包”之前 |
| 两个新按钮的启用条件 | 与卡片本身相同：`root.controller.connected && !root.controller.busy`；“只用界面”另加 `&& !root.controller.safeMode` |

## 步骤

### 8.1 新增辅助函数

文件：`frontend/qt-qml/qml/components/AppearancePanel.qml`

在根 `Item` 的 `function label(value) { ... }` 之后加入：

```qml
    function themeTitle(id) {
        const item = controller.items.find(entry => entry.id === id)
        return item ? label(item.title) : id
    }
```

### 8.2 顶部显示当前选择

在 `Label { Layout.fillWidth: true; text: root.t("theme_description"); ... }` 之后加入：

```qml
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.spaceMd; rowSpacing: Theme.spaceXs
                Label { text: root.t("theme_current_palette"); color: Theme.textSecondary }
                Label {
                    objectName: "currentPaletteLabel"
                    Layout.fillWidth: true
                    text: root.themeTitle(root.controller.selectedId)
                    color: Theme.textPrimary; textFormat: Text.PlainText; elide: Text.ElideRight
                }
                Label { text: root.t("theme_current_layout"); color: Theme.textSecondary }
                Label {
                    objectName: "currentLayoutLabel"
                    Layout.fillWidth: true
                    text: root.controller.layoutSelectedId ? root.themeTitle(root.controller.layoutSelectedId) : root.t("theme_layout_builtin")
                    color: Theme.textPrimary; textFormat: Text.PlainText; elide: Text.ElideRight
                }
            }
```

### 8.3 拆分重置按钮

找到按钮行：

```qml
                TextButton { objectName: "resetThemeButton"; text: root.t("theme_reset"); subtle: true; enabled: root.controller.connected && !root.controller.busy; onClicked: root.controller.reset() }
```

替换为：

```qml
                TextButton { objectName: "resetAppearanceButton"; text: root.t("theme_reset_appearance"); subtle: true; enabled: root.controller.connected && !root.controller.busy; onClicked: root.controller.resetAppearance() }
                TextButton { objectName: "resetLayoutButton"; text: root.t("theme_reset_layout"); subtle: true; enabled: root.controller.connected && !root.controller.busy; onClicked: root.controller.resetLayout() }
                TextButton { objectName: "resetThemeButton"; text: root.t("theme_reset"); subtle: true; enabled: root.controller.connected && !root.controller.busy; onClicked: root.controller.reset() }
```

### 8.4 界面不可用提示

找到显示 `theme_safe_mode`、`theme_offline` 等警告的那个 `Label`（`color: Theme.statusWarning`），在它**之后**加入：

```qml
            Label {
                objectName: "layoutUnavailableLabel"
                Layout.fillWidth: true
                visible: root.controller.layoutUnavailable && !root.controller.safeMode
                text: root.t("theme_layout_unavailable")
                color: Theme.statusWarning; wrapMode: Text.WordWrap
            }
```

### 8.5 卡片：选中状态与标记

在卡片 `delegate: Rectangle { id: card ...` 中：

1. 把：

```qml
                        readonly property bool selected: root.controller.selectedId === modelData.id
```

替换为：

```qml
                        readonly property bool paletteSelected: root.controller.selectedId === modelData.id
                        readonly property bool layoutSelected: root.controller.layoutSelectedId === modelData.id
                        readonly property bool selected: paletteSelected || layoutSelected
                        readonly property string badge: paletteSelected && layoutSelected ? root.t("theme_badge_both")
                            : paletteSelected ? root.t("theme_badge_palette")
                            : layoutSelected ? root.t("theme_badge_layout") : ""
```

`selected` 仍被边框颜色和 `Accessible.description` 使用，这两处**不需要修改**。

2. 把标题行中的：

```qml
                                        Label { text: card.selected ? "✓" : ""; color: Theme.accentPrimary }
```

替换为：

```qml
                                        Label { objectName: "themeBadge_" + card.modelData.id; text: card.badge; color: Theme.accentPrimary; textFormat: Text.PlainText }
```

3. 把信息行的 `text:`：

```qml
                                        text: root.t("theme_origin_" + card.modelData.origin) + (card.modelData.ui ? " · QML" : "")
                                            + (card.modelData.version ? " · v" + card.modelData.version : "")
                                            + (card.modelData.schemaVersion ? "" : " · " + root.t("theme_dark_only"))
```

替换为：

```qml
                                        text: root.t("theme_origin_" + card.modelData.origin) + (card.modelData.ui ? " · QML" : "")
                                            + (card.modelData.hasLayout ? " · " + root.t("theme_has_layout") : "")
                                            + (card.modelData.hasWallpaper ? " · " + root.t("theme_has_wallpaper") : "")
                                            + (card.modelData.version ? " · v" + card.modelData.version : "")
                                            + (card.modelData.schemaVersion ? "" : " · " + root.t("theme_dark_only"))
```

### 8.6 卡片：新按钮

在卡片底部的 `Flow { ... }` 中，`objectName: "updateTheme_" + card.modelData.id` 那个 `TextButton` **之前**加入：

```qml
                                TextButton {
                                    objectName: "applyPalette_" + card.modelData.id
                                    text: root.t("theme_use_palette")
                                    visible: card.modelData.hasLayout === true
                                    subtle: true
                                    enabled: root.controller.connected && !root.controller.busy
                                        && (!root.controller.safeMode || card.modelData.origin === "builtin")
                                    onClicked: root.controller.selectAppearance(card.modelData.id)
                                }
                                TextButton {
                                    objectName: "applyLayout_" + card.modelData.id
                                    text: root.t("theme_use_layout")
                                    visible: card.modelData.hasLayout === true
                                    subtle: true
                                    enabled: root.controller.connected && !root.controller.busy && !root.controller.safeMode
                                    onClicked: root.controller.selectLayout(card.modelData.id)
                                }
```

### 8.7 信任对话框加 objectName

找到 `Dialog { id: trustDialog`，在 `id: trustDialog` 之后加一行 `objectName: "themeTrustDialog"`。

### 8.8 i18n

两个文件中，以下**新增键**放在 `"theme_selected"` 一行之后：

| 键 | zh | en |
| --- | --- | --- |
| `theme_current_palette` | `当前配色` | `Colors` |
| `theme_current_layout` | `当前界面` | `Layout` |
| `theme_layout_builtin` | `内置界面` | `Built-in layout` |
| `theme_use_palette` | `只用配色` | `Use colors only` |
| `theme_use_layout` | `只用界面` | `Use layout only` |
| `theme_badge_palette` | `配色` | `Colors` |
| `theme_badge_layout` | `界面` | `Layout` |
| `theme_badge_both` | `配色 · 界面` | `Colors · Layout` |
| `theme_has_layout` | `含界面` | `Layout` |
| `theme_has_wallpaper` | `含壁纸` | `Wallpaper` |
| `theme_reset_appearance` | `恢复默认配色` | `Reset colors` |
| `theme_reset_layout` | `恢复内置界面` | `Use built-in layout` |

以下**已有键**改为新文案：

| 键 | zh | en |
| --- | --- | --- |
| `theme_description` | `点击卡片应用整套主题。含界面的主题可以只用它的配色，或只用它的界面；配色和界面会分别保存。` | `Click a card to apply the whole theme. For themes with a layout, you can use only its colors or only its layout; colors and layout are saved separately.` |
| `theme_reset` | `全部恢复默认` | `Reset all` |
| `theme_trust_body` | `此主题的界面包含可执行的 QML 代码，拥有与扩展相同的访问能力，并在应用的界面线程运行。只使用配色不需要授权；仅在信任时加载它的界面。可通过 Ctrl+Shift+E 打开恢复窗口，或使用 --safe-mode 启动。` | `This theme's layout contains executable QML with the same access as extensions and runs on the GUI thread. Using only its colors needs no trust; load its layout only if you trust it. Use Ctrl+Shift+E for recovery or start with --safe-mode.` |

完成后运行 Phase 4 中的 JSON 合法性检查命令，并确认两个文件的键集合一致：

```sh
uv run --no-project python -I -c "import json;a=json.load(open('frontend/qt-qml/i18n/zh.json',encoding='utf-8'));b=json.load(open('frontend/qt-qml/i18n/en.json',encoding='utf-8'));print(sorted(set(a)^set(b)))"
```

输出必须是 `[]`。

### 8.9 新建 QML 测试 `tests/qml/tst_appearance_panel.qml`

`tests/qml/` 下所有 `tst_*.qml` 都会被 `nekotune_slider_quick_test` 自动执行，**不需要修改 CMake**。

```qml
import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components"

Item {
    id: testCase
    width: 900; height: 1200
    QtObject {
        id: mockTranslator
        property string language: "en"
        function text(key, language) { return key }
    }
    QtObject {
        id: mockController
        property bool safeMode: false
        property bool connected: true
        property bool busy: false
        property string mode: "dark"
        property string error: ""
        property string notice: ""
        property bool unavailable: false
        property bool legacyLight: false
        property bool reducedMotion: false
        property bool layoutUnavailable: false
        property string selectedId: "builtin/yozakura"
        property string layoutSelectedId: ""
        property var items: [
            {id: "builtin/yozakura", title: "Yozakura", origin: "builtin", available: true, schemaVersion: 1,
             swatches: ["#000000"], hasLayout: false, hasWallpaper: false},
            {id: "pkg/full", title: "Full", origin: "imported", available: true, schemaVersion: 1, version: "1.0.0",
             swatches: ["#000000"], ui: {bottomPlayer: "Player.qml"}, hasLayout: true, hasWallpaper: true}
        ]
        property var calls: []
        signal trustRequested(string id, string target)
        signal replaceRequested(string path, string themeId)
        function record(value) { calls = calls.concat([value]) }
        function setMode(value) { record("setMode:" + value) }
        function setReducedMotion(value) { record("setReducedMotion:" + value) }
        function selectTheme(id, trusted) { record("selectTheme:" + id + (trusted ? ":trusted" : "")) }
        function selectAppearance(id, trusted) { record("selectAppearance:" + id + (trusted ? ":trusted" : "")) }
        function selectLayout(id, trusted) { record("selectLayout:" + id + (trusted ? ":trusted" : "")) }
        function resetAppearance() { record("resetAppearance") }
        function resetLayout() { record("resetLayout") }
        function reset() { record("reset") }
        function importTheme(path, replace) {}
        function updateTheme(id, path, replace) {}
        function reloadTheme(id) {}
        function exportTheme(id, path) {}
        function removeTheme(id) {}
    }
    Component {
        id: panelComponent
        AppearancePanel { anchors.fill: parent; controller: mockController; translator: mockTranslator }
    }

    TestCase {
        name: "AppearancePanel"
        when: windowShown
        function init() {
            mockController.calls = []
            mockController.safeMode = false
            mockController.selectedId = "builtin/yozakura"
            mockController.layoutSelectedId = ""
            mockController.layoutUnavailable = false
        }
        function test_layoutButtonsOnlyForLayoutThemes() {
            const panel = createTemporaryObject(panelComponent, testCase)
            verify(panel)
            const builtinPalette = findChild(panel, "applyPalette_builtin/yozakura")
            verify(builtinPalette)
            verify(!builtinPalette.visible)
            const palette = findChild(panel, "applyPalette_pkg/full")
            const layout = findChild(panel, "applyLayout_pkg/full")
            verify(palette.visible)
            verify(layout.visible)
            palette.clicked()
            layout.clicked()
            findChild(panel, "themeCard_pkg/full").clicked()
            compare(mockController.calls, ["selectAppearance:pkg/full", "selectLayout:pkg/full", "selectTheme:pkg/full"])
        }
        function test_badgesAndSummary() {
            const panel = createTemporaryObject(panelComponent, testCase)
            mockController.layoutSelectedId = "pkg/full"
            compare(findChild(panel, "themeBadge_builtin/yozakura").text, "theme_badge_palette")
            compare(findChild(panel, "themeBadge_pkg/full").text, "theme_badge_layout")
            compare(findChild(panel, "currentPaletteLabel").text, "Yozakura")
            compare(findChild(panel, "currentLayoutLabel").text, "Full")
            mockController.selectedId = "pkg/full"
            compare(findChild(panel, "themeBadge_pkg/full").text, "theme_badge_both")
            compare(findChild(panel, "themeBadge_builtin/yozakura").text, "")
            mockController.layoutSelectedId = ""
            compare(findChild(panel, "currentLayoutLabel").text, "theme_layout_builtin")
        }
        function test_resetButtons() {
            const panel = createTemporaryObject(panelComponent, testCase)
            findChild(panel, "resetAppearanceButton").clicked()
            findChild(panel, "resetLayoutButton").clicked()
            findChild(panel, "resetThemeButton").clicked()
            compare(mockController.calls, ["resetAppearance", "resetLayout", "reset"])
        }
        function test_layoutUnavailableHint() {
            const panel = createTemporaryObject(panelComponent, testCase)
            const hint = findChild(panel, "layoutUnavailableLabel")
            verify(!hint.visible)
            mockController.layoutUnavailable = true
            verify(hint.visible)
            mockController.safeMode = true
            verify(!hint.visible)
        }
        function test_safeModeDisablesLayoutButton() {
            const panel = createTemporaryObject(panelComponent, testCase)
            mockController.safeMode = true
            verify(!findChild(panel, "applyLayout_pkg/full").enabled)
        }
        function test_trustDialogRetriesSameAction() {
            const panel = createTemporaryObject(panelComponent, testCase)
            const dialog = findChild(panel, "themeTrustDialog")
            verify(dialog)
            mockController.trustRequested("pkg/full", "layout")
            tryVerify(() => dialog.opened)
            dialog.accept()
            compare(mockController.calls, ["selectLayout:pkg/full:trusted"])
        }
        function test_reducedMotionToggle() {
            const panel = createTemporaryObject(panelComponent, testCase)
            const check = findChild(panel, "reducedMotionCheck")
            verify(check)
            check.toggle()
            check.toggled()
            compare(mockController.calls, ["setReducedMotion:true"])
        }
    }
}
```

注意事项：

- `themeCard_<id>` 是卡片里的 `AbstractButton`，所以可以直接调用 `clicked()`。
- 卡片在 `Repeater` 里，`findChild` 能找到它们；如果某个 `findChild` 返回 `null`，先加 `waitForRendering(panel)`，再检查 objectName 是否拼写一致。
- `Dialog.opened` 是 Qt Quick Controls 的属性。如果当前 Qt 版本不支持，改为 `tryVerify(() => dialog.visible)`。
- `CheckBox.toggle()` 只改状态、不发信号，所以测试里再手动调用一次 `toggled()`。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Fusion ./build/tests/nekotune_slider_quick_test -input tests/qml/tst_appearance_panel.qml
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test themePackagesAndAppearance
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test managementThemeAndShellInRealFrontend
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
git diff --check
```

如果 Qt6 的 `qmllint` 可用：

```sh
qmllint frontend/qt-qml/qml/components/AppearancePanel.qml
```

### 视觉检查（必须）

offscreen 测试不能代替视觉检查。`themePackagesAndAppearance` 支持截图：

```sh
mkdir -p /tmp/nekotune-shots
NEKOTUNE_TEST_SCREENSHOT_DIR=/tmp/nekotune-shots \
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
./build/tests/nekotune_extensions_test themePackagesAndAppearance
ls /tmp/nekotune-shots
```

打开 `theme-appearance-light-zh.png`、`theme-appearance-dark-en.png`、`theme-appearance-narrow-large-en.png`、`theme-recovery-appearance-narrow.png`，检查：

- [ ] 当前配色、当前界面两行显示正确，没有被截断成看不懂的文字。
- [ ] 卡片按钮在窄窗口下能换行，没有重叠或溢出卡片。
- [ ] 中英文下按钮文字完整。
- [ ] 深色和浅色下标记文字清晰可读。

如果无法查看图片，在报告中写明“视觉检查未完成”，**不要写成已通过**。

## 完成标准

- [ ] `tst_appearance_panel.qml` 全部通过。
- [ ] 原有外观相关测试通过。
- [ ] i18n 两个文件键集合一致。
- [ ] 完成视觉检查，或明确报告未完成。

## 提交

```sh
git add frontend/qt-qml/qml/components/AppearancePanel.qml \
  frontend/qt-qml/i18n/zh.json frontend/qt-qml/i18n/en.json \
  tests/qml/tst_appearance_panel.qml
git commit -m "feat(theme): 外观页支持分别选择配色与界面"
```
