# Phase 3：外观页「壁纸」分类

## 目标

1. 外观页顶部新增「主题」「壁纸」两个分类按钮（W6）。「主题」就是现有内容，行为不变。
2. 「壁纸」分类列出三个内置样式、“跟随当前主题”和每个提供壁纸的主题；点选后只修改 `style` 与 `themeWallpaper`，不开启壁纸（W7）。
3. 选择未信任的含代码主题时，复用外观页现有的信任对话框（W9）。
4. 没有注入壁纸管理器时（恢复窗口、部分测试）不显示分类按钮（W10）。

## 前置条件

- Phase 2 已完成并提交。

## 涉及文件

| 文件 | 改动 |
| --- | --- |
| `frontend/qt-qml/qml/components/WallpaperGallery.qml` | **新增**：「壁纸」分类内容 |
| `frontend/qt-qml/resources.qrc` | 登记新文件 |
| `frontend/qt-qml/qml/components/AppearancePanel.qml` | 分类按钮；把现有内容包进「主题」分类；信任对话框支持 `"wallpaper"`；QML 标记计入壁纸 |
| `frontend/qt-qml/qml/pages/SettingsPage.qml` | 把 `wallpaperManager` 传给外观页 |
| `frontend/qt-qml/i18n/zh.json`、`en.json` | 新文案 |
| `tests/qml/tst_appearance_panel.qml` | 模拟壁纸管理器，新增测试 |

## 步骤

### 3.1 新组件 `WallpaperGallery.qml`

新建 `frontend/qt-qml/qml/components/WallpaperGallery.qml`，内容如下：

```qml
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Wallpaper category of the appearance page. It picks the style and source only;
// turning the wallpaper on stays in the wallpaper settings.
ColumnLayout {
    id: root
    required property var controller
    required property var manager
    required property var translator
    readonly property var preferences: manager.settings || ({})
    readonly property string style: preferences.style || "classic"
    readonly property string source: preferences.themeWallpaper || ""
    readonly property var entries: {
        const list = ["classic", "vinyl", "lyrics"].map(style => ({
            key: style, style: style, source: "", theme: false, missing: false,
            title: t("wallpaper_style_" + style), detail: t("wallpaper_gallery_builtin")}))
        list.push({key: "follow", style: "theme", source: "", theme: false, missing: false,
                   title: t("wallpaper_theme_follow"), detail: t("wallpaper_gallery_follow_hint")})
        const ids = []
        for (const item of controller.items || []) {
            if (!item.hasWallpaper)
                continue
            ids.push(item.id)
            list.push({key: item.id, style: "theme", source: item.id, theme: true, missing: false,
                       title: label(item.title),
                       detail: t("theme_origin_" + item.origin)
                           + (item.hasCode && !item.trusted ? " · " + t("wallpaper_gallery_needs_trust") : "")})
        }
        // Keep a saved source visible while its package is disabled or removed.
        if (source !== "" && !ids.includes(source))
            list.push({key: source, style: "theme", source: source, theme: true, missing: true,
                       title: t("wallpaper_theme_source_unavailable"), detail: source})
        return list
    }
    function t(key) { return translator.text(key, translator.language) }
    function label(value) {
        return typeof value === "string" ? value : value ? value[translator.language] || value.en || value.zh || "" : ""
    }
    function selected(entry) { return entry.style === style && (style !== "theme" || entry.source === source) }
    function choose(entry) {
        if (entry.theme)
            controller.selectWallpaper(entry.source)
        else if (entry.style === "theme")
            manager.setOptions({style: "theme", themeWallpaper: ""})
        else
            manager.setOption("style", entry.style)
    }
    spacing: Theme.spaceLg

    Label {
        Layout.fillWidth: true
        text: root.t("wallpaper_gallery_description")
        color: Theme.textSecondary; wrapMode: Text.WordWrap
    }
    Label {
        objectName: "wallpaperGalleryOff"
        Layout.fillWidth: true
        visible: !root.preferences.enabled
        text: root.t("wallpaper_gallery_off")
        color: Theme.textMuted; wrapMode: Text.WordWrap
    }
    Label {
        objectName: "wallpaperGalleryError"
        Layout.fillWidth: true
        visible: text !== ""
        text: root.manager.error ? root.t(root.manager.error)
            : root.controller.error === "theme_wallpaper_unavailable" ? root.t(root.controller.error) : ""
        color: Theme.statusError; wrapMode: Text.WordWrap; textFormat: Text.PlainText
    }
    GridLayout {
        Layout.fillWidth: true
        columns: width >= 1000 ? 3 : width >= 610 ? 2 : 1
        columnSpacing: Theme.spaceMd; rowSpacing: Theme.spaceMd
        Repeater {
            model: root.entries
            delegate: Rectangle {
                id: card
                required property var modelData
                readonly property bool current: root.selected(modelData)
                Layout.fillWidth: true
                Layout.minimumWidth: 250
                implicitHeight: body.implicitHeight + Theme.spaceLg * 2
                color: button.activeFocus ? Theme.bgHover : Theme.bgSurface
                radius: Theme.radiusMd
                border.color: current || button.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                border.width: current || button.activeFocus ? 2 : 1
                AbstractButton {
                    id: button
                    objectName: "wallpaperCard_" + card.modelData.key
                    anchors.fill: parent
                    enabled: !card.modelData.theme || (!card.modelData.missing && root.controller.connected
                        && !root.controller.busy && !root.controller.safeMode)
                    Accessible.name: card.modelData.title
                    Accessible.description: card.current ? root.t("wallpaper_gallery_selected") : card.modelData.detail
                    onClicked: root.choose(card.modelData)
                    background: Item {}
                }
                ColumnLayout {
                    id: body
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: Theme.spaceLg
                    spacing: Theme.spaceSm
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: card.modelData.title; font.pixelSize: Theme.fontDialogTitle; font.bold: true; color: button.enabled ? Theme.textPrimary : Theme.textDisabled; textFormat: Text.PlainText; elide: Text.ElideRight }
                        Label { objectName: "wallpaperBadge_" + card.modelData.key; text: card.current ? root.t("wallpaper_gallery_selected") : ""; color: Theme.accentPrimary; textFormat: Text.PlainText }
                    }
                    Label { Layout.fillWidth: true; text: card.modelData.detail; color: Theme.textMuted; font.pixelSize: Theme.fontCaption; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
                }
            }
        }
    }
    Connections {
        target: root.controller
        function onWallpaperReady(id) { root.manager.setOptions({style: "theme", themeWallpaper: id}) }
    }
}
```

要点：

- 内置样式与“跟随当前主题”只是前端设置，不需要连接后端，所以不受 `connected`、`busy`、安全模式限制（W10）。
- 主题卡片的点击交给 `controller.selectWallpaper()`；它可能先请求授权，所以要等 `wallpaperReady` 信号再写设置。
- 不读取 `wallpaperSlots` 判断是否可选：未信任的主题也要能点，点了才会弹授权。

在 `frontend/qt-qml/resources.qrc` 中，`<file>qml/components/AppearancePanel.qml</file>` 的下一行加入：

```xml
        <file>qml/components/WallpaperGallery.qml</file>
```

### 3.2 外观页

文件：`frontend/qt-qml/qml/components/AppearancePanel.qml`

1. 在 `required property var translator` 之后加入：

```qml
    property var wallpaper: null
    property string category: "themes"
```

2. 找到描述文字：

```qml
            Label { Layout.fillWidth: true; text: root.t("theme_description"); color: Theme.textSecondary; wrapMode: Text.WordWrap }
```

在它**之后**加入分类按钮和壁纸分类：

```qml
            Flow {
                objectName: "appearanceCategories"
                Layout.fillWidth: true
                visible: root.wallpaper !== null
                spacing: Theme.spaceSm
                Repeater {
                    model: ["themes", "wallpaper"]
                    TextButton {
                        required property string modelData
                        objectName: "appearanceCategory_" + modelData
                        text: root.t("appearance_category_" + modelData)
                        subtle: root.category !== modelData
                        onClicked: root.category = modelData
                    }
                }
            }
            Loader {
                Layout.fillWidth: true
                active: root.wallpaper !== null && root.category === "wallpaper"
                visible: active
                sourceComponent: WallpaperGallery { controller: root.controller; manager: root.wallpaper; translator: root.translator }
            }
```

3. 把描述文字之后、原有的所有内容（从 `GridLayout`（当前配色／界面）开始，一直到主题卡片的 `GridLayout` 结束）包进一个新的 `ColumnLayout`，整体多缩进 4 个空格：

```qml
            ColumnLayout {
                objectName: "themeCategory"
                Layout.fillWidth: true
                visible: root.category === "themes"
                spacing: Theme.spaceLg
                // 原有内容，顺序和代码都不变
            }
```

只增加缩进，不修改被包住的任何代码。完成后用 `git diff -w frontend/qt-qml/qml/components/AppearancePanel.qml` 检查：忽略空白后，被包住的部分应该没有差异。

4. 主题卡片说明文字中，把

```qml
                                        text: root.t("theme_origin_" + card.modelData.origin) + (card.modelData.ui ? " · QML" : "")
```

改为

```qml
                                        text: root.t("theme_origin_" + card.modelData.origin) + (card.modelData.ui || card.modelData.wallpaper ? " · QML" : "")
```

（缩进以第 3 步之后的实际缩进为准。）只有壁纸的主题同样含有 QML，需要显示这个标记。

5. 信任对话框的 `onAccepted`：在 `if (root.pendingTarget === "layout")` 分支之前加入 `"wallpaper"` 分支，改完后是：

```qml
        onAccepted: {
            if (root.pendingTarget === "wallpaper")
                root.controller.selectWallpaper(root.pendingId, true)
            else if (root.pendingTarget === "layout")
                root.controller.selectLayout(root.pendingId, true)
            else if (root.pendingTarget === "appearance")
                root.controller.selectAppearance(root.pendingId, true)
            else
                root.controller.selectTheme(root.pendingId, true)
        }
```

### 3.3 设置页传入壁纸管理器

文件：`frontend/qt-qml/qml/pages/SettingsPage.qml`

把

```qml
                sourceComponent: AppearancePanel { controller: page.controllers.theme; translator: page.translator }
```

改为

```qml
                sourceComponent: AppearancePanel {
                    controller: page.controllers.theme; translator: page.translator
                    wallpaper: typeof wallpaperManager !== "undefined" ? wallpaperManager : null
                }
```

`ExtensionsWindow.qml`（恢复窗口）**不改**：它固定使用内置样式，不提供壁纸分类。

### 3.4 文案

在 `zh.json` 与 `en.json` 中加入（放在 `appearance` 或 `wallpaper_theme_*` 附近）：

| 键 | zh | en |
| --- | --- | --- |
| `appearance_category_themes` | 主题 | Themes |
| `appearance_category_wallpaper` | 壁纸 | Wallpaper |
| `wallpaper_gallery_description` | 选择桌面音乐壁纸的样式和来源。这里只切换样式，不会开启壁纸。 | Choose the style and source of the music wallpaper. This only changes the style; it does not turn the wallpaper on. |
| `wallpaper_gallery_off` | 动态音乐壁纸未开启，可在「设置 → 动态音乐壁纸」中开启。 | The music wallpaper is off. Turn it on in Settings → Music wallpaper. |
| `wallpaper_gallery_builtin` | 内置样式 | Built-in style |
| `wallpaper_gallery_follow_hint` | 优先使用当前界面的壁纸，没有时使用当前配色的壁纸 | Uses the current layout's wallpaper, or the current colors' wallpaper if it has none |
| `wallpaper_gallery_needs_trust` | 首次使用需要授权 | Needs permission on first use |
| `wallpaper_gallery_selected` | 使用中 | In use |

再运行 Phase 2 的键集合检查脚本，输出必须是 `ok`。

### 3.5 QML 测试

文件：`tests/qml/tst_appearance_panel.qml`

1. 在 `mockController` 中（`error` 属性已存在，不用新增）：
   - 在 `items` 中再加一项只有壁纸、未信任的主题：

```qml
            {id: "pkg/wall", title: "Wall", origin: "imported", available: true, schemaVersion: 1, version: "1.0.0",
             swatches: ["#000000"], wallpaper: "Wallpaper.qml", hasCode: true, trusted: false,
             hasLayout: false, hasWallpaper: true}
```

   - 加入 `signal wallpaperReady(string id)` 和 `function selectWallpaper(id, trusted) { record("selectWallpaper:" + id + (trusted ? ":trusted" : "")) }`。

2. 在 `mockController` 之后新增模拟壁纸管理器：

```qml
    QtObject {
        id: mockWallpaper
        property var settings: ({enabled: false, style: "classic", themeWallpaper: ""})
        property string error: ""
        property var calls: []
        function setOption(key, value) { calls = calls.concat([key + "=" + value]); settings = Object.assign({}, settings, {[key]: value}); return true }
        function setOptions(values) {
            calls = calls.concat([JSON.stringify(values)])
            settings = Object.assign({}, settings, values)
            return true
        }
    }
```

3. 新增一个组件，带壁纸管理器：

```qml
    Component {
        id: wallpaperPanelComponent
        AppearancePanel { anchors.fill: parent; controller: mockController; translator: mockTranslator; wallpaper: mockWallpaper }
    }
```

4. `init()` 中加入：

```qml
            mockWallpaper.calls = []
            mockWallpaper.settings = ({enabled: false, style: "classic", themeWallpaper: ""})
            mockWallpaper.error = ""
```

5. 新增测试函数：

```qml
        function test_wallpaperCategoryHiddenWithoutManager() {
            const panel = createTemporaryObject(panelComponent, testCase)
            verify(!findChild(panel, "appearanceCategories").visible)
            verify(findChild(panel, "themeCategory").visible)
        }
        function test_wallpaperCategoryChoosesStyleAndSource() {
            const panel = createTemporaryObject(wallpaperPanelComponent, testCase)
            verify(findChild(panel, "appearanceCategories").visible)
            findChild(panel, "appearanceCategory_wallpaper").clicked()
            verify(!findChild(panel, "themeCategory").visible)
            tryVerify(() => findChild(panel, "wallpaperCard_classic") !== null)
            compare(findChild(panel, "wallpaperBadge_classic").text, "wallpaper_gallery_selected")
            verify(findChild(panel, "wallpaperGalleryOff").visible)
            // Only themes that provide a wallpaper are listed.
            verify(findChild(panel, "wallpaperCard_pkg/full") !== null)
            verify(findChild(panel, "wallpaperCard_pkg/wall") !== null)
            verify(findChild(panel, "wallpaperCard_builtin/yozakura") === null)

            findChild(panel, "wallpaperCard_vinyl").clicked()
            findChild(panel, "wallpaperCard_follow").clicked()
            compare(mockWallpaper.calls, ["style=vinyl", JSON.stringify({style: "theme", themeWallpaper: ""})])
            compare(findChild(panel, "wallpaperBadge_follow").text, "wallpaper_gallery_selected")

            // Theme sources go through the controller and are saved once it reports ready.
            findChild(panel, "wallpaperCard_pkg/wall").clicked()
            compare(mockController.calls, ["selectWallpaper:pkg/wall"])
            mockController.wallpaperReady("pkg/wall")
            compare(mockWallpaper.settings.themeWallpaper, "pkg/wall")
            compare(mockWallpaper.settings.style, "theme")
            verify(!mockWallpaper.settings.enabled)
            compare(findChild(panel, "wallpaperBadge_pkg/wall").text, "wallpaper_gallery_selected")
        }
        function test_wallpaperCategoryKeepsMissingSourceAndSafeMode() {
            mockWallpaper.settings = ({enabled: true, style: "theme", themeWallpaper: "pkg/gone"})
            const panel = createTemporaryObject(wallpaperPanelComponent, testCase)
            findChild(panel, "appearanceCategory_wallpaper").clicked()
            tryVerify(() => findChild(panel, "wallpaperCard_pkg/gone") !== null)
            verify(!findChild(panel, "wallpaperCard_pkg/gone").enabled)
            compare(findChild(panel, "wallpaperBadge_pkg/gone").text, "wallpaper_gallery_selected")
            verify(!findChild(panel, "wallpaperGalleryOff").visible)
            mockController.safeMode = true
            verify(!findChild(panel, "wallpaperCard_pkg/wall").enabled)
            verify(findChild(panel, "wallpaperCard_classic").enabled)
        }
        function test_trustDialogRetriesWallpaper() {
            const panel = createTemporaryObject(wallpaperPanelComponent, testCase)
            const dialog = findChild(panel, "themeTrustDialog")
            mockController.trustRequested("pkg/wall", "wallpaper")
            tryVerify(() => dialog.opened)
            dialog.accept()
            compare(mockController.calls, ["selectWallpaper:pkg/wall:trusted"])
        }
```

6. 现有测试 `test_layoutButtonsOnlyForLayoutThemes` 不应受影响（新增的 `pkg/wall` 没有界面，`applyLayout_pkg/wall` 不可见）。如果有现有测试因为新增条目而失败，**停下来报告**，不要改断言。

## 验证命令

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Fusion \
./build/tests/nekotune_slider_quick_test -input tests/qml/tst_appearance_panel.qml
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Fusion \
./build/tests/nekotune_slider_quick_test -input tests/qml/tst_wallpaper_settings.qml
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
ctest --test-dir build --output-on-failure
git diff --check
```

如果本机有 `qmllint`，再检查新文件（只要求不新增错误）：

```sh
qmllint frontend/qt-qml/qml/components/WallpaperGallery.qml frontend/qt-qml/qml/components/AppearancePanel.qml
```

全量 CTest 的失败列表必须与 Phase 0 的基线相同。

### 手动检查

启动应用（`./build/frontend/qt-qml/nekotune` 或实际产物路径），打开「设置 → 外观」：

1. 顶部有「主题」「壁纸」两个按钮；「主题」下的内容与改动前一致。
2. 「壁纸」下能看到三个内置样式、“跟随当前主题”和示例主题 `Mint example · QML`（需要先导入 `examples/extensions/theme-qml`）。
3. 点示例主题会先弹出授权对话框；同意后卡片显示“使用中”，「设置 → 动态音乐壁纸」中的样式和来源同步变化，壁纸开关**保持不变**。
4. `Ctrl+Shift+E` 打开的恢复窗口中没有「壁纸」按钮。

无法启动图形界面时写明“手动检查已跳过”。

## 完成标准

- [ ] 3.1–3.5 全部完成。
- [ ] 两个 QML 测试文件通过；全量 CTest 没有新增失败。
- [ ] `git diff -w` 确认「主题」分类内部代码没有改动。
- [ ] 两份翻译文件的键集合一致。
- [ ] 手动检查已完成或已注明跳过。

## 提交

```sh
git add frontend/qt-qml/qml/components/WallpaperGallery.qml frontend/qt-qml/resources.qrc \
  frontend/qt-qml/qml/components/AppearancePanel.qml frontend/qt-qml/qml/pages/SettingsPage.qml \
  frontend/qt-qml/i18n/zh.json frontend/qt-qml/i18n/en.json tests/qml/tst_appearance_panel.qml
git commit -m "feat(theme): 外观页新增壁纸分类"
```
