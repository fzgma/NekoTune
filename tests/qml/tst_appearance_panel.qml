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
             swatches: ["#000000"], ui: {bottomPlayer: "Player.qml"}, hasLayout: true, hasWallpaper: true},
            {id: "pkg/wall", title: "Wall", origin: "imported", available: true, schemaVersion: 1, version: "1.0.0",
             swatches: ["#000000"], wallpaper: "Wallpaper.qml", hasCode: true, trusted: false,
             hasLayout: false, hasWallpaper: true}
        ]
        property var calls: []
        signal trustRequested(string id, string target)
        signal wallpaperReady(string id)
        signal replaceRequested(string path, string themeId)
        function record(value) { calls = calls.concat([value]) }
        function setMode(value) { record("setMode:" + value) }
        function setReducedMotion(value) { record("setReducedMotion:" + value) }
        function selectTheme(id, trusted) { record("selectTheme:" + id + (trusted ? ":trusted" : "")) }
        function selectAppearance(id, trusted) { record("selectAppearance:" + id + (trusted ? ":trusted" : "")) }
        function selectWallpaper(id, trusted) { record("selectWallpaper:" + id + (trusted ? ":trusted" : "")) }
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
    Component {
        id: panelComponent
        AppearancePanel { anchors.fill: parent; controller: mockController; translator: mockTranslator }
    }

    Component {
        id: wallpaperPanelComponent
        AppearancePanel { anchors.fill: parent; controller: mockController; translator: mockTranslator; wallpaper: mockWallpaper }
    }

    TestCase {
        name: "AppearancePanel"
        when: windowShown
        function init() {
            mockWallpaper.calls = []
            mockWallpaper.settings = ({enabled: false, style: "classic", themeWallpaper: ""})
            mockWallpaper.error = ""
            mockController.calls = []
            mockController.safeMode = false
            mockController.selectedId = "builtin/yozakura"
            mockController.layoutSelectedId = ""
            mockController.layoutUnavailable = false
        }
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
