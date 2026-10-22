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
