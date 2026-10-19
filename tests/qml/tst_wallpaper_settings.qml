import QtQuick
import QtQuick.Controls
import QtTest
import "../../frontend/qt-qml/qml/wallpaper"

Item {
    id: testCase
    width: 800; height: 1400
    QtObject {
        id: mockManager
        property bool requestedEnabled: false
        property bool supported: false
        property string unavailableReason: "wallpaper_qpa_unavailable"
        property string error: ""
        property int runningCount: 0
        property bool linuxOverlay: true
        property var settings: ({autoRestore: false, target: "primary", updateHz: 30, intensity: 0.35})
        property var outputs: [{id: "edid", name: "DP-1", selected: true, state: "unavailable", error: "wallpaper_qpa_unavailable"}]
        property var audioAnalysis: ({spectrumAvailable: false, reason: "wallpaper_spectrum_unavailable"})
        property string lastKey: ""
        function setOption(key, value) {
            lastKey = key
            if (key === "enabled") requestedEnabled = value
            settings = Object.assign({}, settings, {[key]: value})
            return true
        }
        function retry() {}
    }
    QtObject {
        id: mockTranslator
        property string language: "en"
        function text(key, language) { return key }
    }
    Component { id: panelComponent; WallpaperSettings { manager: mockManager; translator: mockTranslator } }
    TestCase {
    name: "WallpaperSettings"
    when: windowShown
    function test_styleSelectionAndSavedChoice() {
        mockManager.settings = {style: "vinyl", target: "primary", updateHz: 30}
        const panel = createTemporaryObject(panelComponent, testCase, {width: 360})
        verify(panel)
        const style = findChild(panel, "wallpaperStyle")
        compare(style.currentIndex, 1)
        style.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(mockManager.lastKey, "style")
        compare(mockManager.settings.style, "lyrics")
        compare(style.currentIndex, 2)
        mockManager.settings = Object.assign({}, mockManager.settings, {style: "classic"})
        compare(style.currentIndex, 0)
    }
    function test_intentIsNotRunningAndSpectrumIsUnavailable() {
        const panel = createTemporaryObject(panelComponent, testCase, {width: 380})
        verify(panel)
        const enabled = findChild(panel, "wallpaperEnabled")
        verify(enabled)
        waitForRendering(panel)
        mouseClick(enabled)
        compare(mockManager.lastKey, "enabled")
        compare(mockManager.requestedEnabled, true)
        compare(mockManager.runningCount, 0)
        compare(findChild(panel, "wallpaperStatus").text, "wallpaper_qpa_unavailable")
        compare(findChild(panel, "wallpaperSpectrum").enabled, false)
        compare(findChild(panel, "wallpaperSpectrum").checked, false)
    }
    function test_savedControlsAndMissingSelection() {
        mockManager.settings = {target: "selected", screenId: "disconnected", updateHz: 60, intensity: 0.5}
        const panel = createTemporaryObject(panelComponent, testCase, {width: 360})
        verify(panel)
        compare(findChild(panel, "wallpaperTarget").currentIndex, 2)
        compare(findChild(panel, "wallpaperSelectedOutput").currentIndex, -1)
        compare(findChild(panel, "wallpaperUpdateHz").currentIndex, 1)
        verify(panel.implicitHeight > 0)
    }
    function test_translatedNarrowPanel() {
        mockManager.settings = {target: "primary", updateHz: 30, intensity: 0.35, lyrics: true}
        const panel = createTemporaryObject(panelComponent, testCase, {width: 360, translator: testTranslator})
        verify(panel)
        for (const language of ["zh", "en"]) {
            testTranslator.language = language
            waitForRendering(panel)
            const screenshot = testFixtures.screenshotPath("wallpaper-settings-" + language)
            compare(findChild(panel, "wallpaperStatus").text, testTranslator.text("wallpaper_qpa_unavailable", language))
            if (screenshot) grabImage(panel).save(screenshot)
        }
    }
    }
}
