import QtQuick
import QtQuick.Controls
import QtTest
import "../../frontend/qt-qml/qml/wallpaper"
import "../../examples/extensions/theme-qml" as Example

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
        property bool transparentOverlaySupported: true
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
    QtObject {
        id: mockTheme
        property bool safeMode: false
        property bool connected: true
        property bool busy: false
        property bool autoReady: true
        property var items: []
        property var calls: []
        property var uiSlots: ({})
        property var wallpaperSlots: ({})
        signal trustRequested(string id, string target)
        signal wallpaperReady(string id)
        function selectWallpaper(id, trusted) {
            calls = calls.concat([{id: id, trusted: Boolean(trusted)}])
            const item = items.find(entry => entry.id === id)
            if (item && item.hasCode && !item.trusted && !trusted) {
                trustRequested(id, "wallpaper")
                return
            }
            if (autoReady) {
                items = items.map(entry => entry.id === id ? Object.assign({}, entry, {trusted: true}) : entry)
                wallpaperSlots = Object.assign({}, wallpaperSlots, {[id]: {source: "file:///pkg/Wallpaper.qml"}})
                wallpaperReady(id)
            }
        }
    }
    QtObject {
        id: mockControllers
        readonly property QtObject playback: QtObject { readonly property string state: "playing" }
    }
    QtObject {
        id: mockPreferences
        property bool lyrics: true
        property bool nextLine: true
        property int updateHz: 60
        property real intensity: 0.35
    }
    QtObject {
        id: mockWallpaper
        readonly property QtObject song: QtObject {
            readonly property string resource_key: "track"
            readonly property string title: "Mint wallpaper · 薄荷壁纸"
            readonly property string artist: "NekoTune"
            readonly property string album: "Theme example"
        }
        readonly property QtObject lyrics: QtObject {
            readonly property string track_id: "track"
            readonly property string state: "ready"
            readonly property QtObject document: QtObject {
                readonly property list<QtObject> lines: [
                    QtObject { readonly property real time_ms: 0; readonly property string text: "Current lyric" },
                    QtObject { readonly property real time_ms: 4000; readonly property string text: "Next lyric" }
                ]
            }
        }
        readonly property real position: 1500
        readonly property real duration: 20000
        readonly property real playbackRate: 1.5
        readonly property bool connected: true
        readonly property bool hasSong: true
        property bool playing: true
        property bool updating: true
        readonly property QtObject preferences: mockPreferences
        readonly property QtObject screen: QtObject { readonly property real devicePixelRatio: 1 }
    }
    Component { id: panelComponent; WallpaperSettings { manager: mockManager; translator: mockTranslator } }
    Component {
        id: exampleComponent
        Example.Wallpaper {
            anchors.fill: null
            property QtObject controllers: mockControllers
            property QtObject wallpaperContext: mockWallpaper
        }
    }
    TestCase {
    name: "WallpaperSettings"
    when: windowShown
    function init() {
        mockManager.requestedEnabled = false
        mockManager.supported = false
        mockManager.error = ""
        mockManager.runningCount = 0
        mockManager.lastKey = ""
        mockManager.settings = {style: "classic", autoRestore: false, target: "primary", updateHz: 30, intensity: 0.35}
        mockManager.outputs = [{id: "edid", name: "DP-1", selected: true, state: "unavailable", error: "wallpaper_qpa_unavailable"}]
        mockTheme.safeMode = false
        mockTheme.connected = true
        mockTheme.busy = false
        mockTheme.autoReady = true
        mockTheme.items = []
        mockTheme.calls = []
        mockTheme.uiSlots = ({})
        mockTheme.wallpaperSlots = ({})
        testTranslator.language = "en"
        mockWallpaper.playing = true
        mockWallpaper.updating = true
        mockPreferences.lyrics = true
        mockPreferences.nextLine = true
        mockPreferences.updateHz = 60
        mockPreferences.intensity = 0.35
    }
    function test_exampleLayout_data() {
        return [
            {tag: "compact", width: 360, height: 240, stacked: false},
            {tag: "portrait", width: 360, height: 780, stacked: true},
            {tag: "landscape", width: 1024, height: 720, stacked: false}
        ]
    }
    function test_exampleLayout(data) {
        const view = createTemporaryObject(exampleComponent, testCase, {width: data.width, height: data.height})
        verify(view)
        compare(view.stacked, data.stacked)
        const lyrics = findChild(view, "themeWallpaperLyrics")
        const card = findChild(view, "themeWallpaperCard")
        verify(lyrics)
        waitForRendering(view)
        verify(card.width <= view.width && card.height <= view.height)
        verify(lyrics.width > 0 && lyrics.height > 0)
        const lyricBottom = lyrics.mapToItem(card, 0, lyrics.height)
        verify(lyricBottom.y <= card.height,
            "lyric bottom=" + lyricBottom.y + ", card height=" + card.height)
        compare(lyrics.song.resource_key, "track")
        compare(lyrics.activeIndex, 0)
        compare(lyrics.position, 1500)
        compare(lyrics.playbackRate, 1.5)
        compare(lyrics.showNext, true)
        compare(lyrics.updateInterval, 16)
        const screenshot = testFixtures.screenshotPath("theme-wallpaper-" + data.tag)
        if (screenshot) grabImage(view).save(screenshot)
        mockPreferences.nextLine = false
        mockPreferences.updateHz = 30
        mockPreferences.intensity = 0
        compare(lyrics.updateInterval, 33)
        compare(lyrics.showNext, false)
        compare(lyrics.animateLineChanges, false)
        compare(view.motionAllowed, false)
        mockPreferences.nextLine = true
        mockPreferences.updateHz = 60
        mockPreferences.intensity = 0.35
        mockWallpaper.playing = false
        compare(lyrics.playing, false)
        compare(view.motionAllowed, false)
        mockWallpaper.updating = false
        compare(lyrics.running, false)
        mockPreferences.lyrics = false
        compare(lyrics.visible, false)
    }
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
        compare(style.count, 4)
        compare(style.model[3], "wallpaper_style_theme")
        keyClick(Qt.Key_Down)
        compare(mockManager.settings.style, "theme")
        compare(style.currentIndex, 3)
        verify(style.enabled)
        compare(mockManager.requestedEnabled, false)
        const reopened = createTemporaryObject(panelComponent, testCase, {width: 360})
        compare(findChild(reopened, "wallpaperStyle").currentIndex, 3)
        mockManager.settings = Object.assign({}, mockManager.settings, {style: "classic"})
        compare(style.currentIndex, 0)
    }
    function test_missingThemeHintWhileDisabledAndSafeModePriority() {
        mockManager.settings = Object.assign({}, mockManager.settings, {style: "theme"})
        const panel = createTemporaryObject(panelComponent, testCase, {width: 360, themeController: mockTheme})
        verify(panel)
        const hint = findChild(panel, "wallpaperThemeHint")
        const style = findChild(panel, "wallpaperStyle")
        verify(hint.visible)
        compare(hint.text, "wallpaper_theme_missing")
        compare(hint.textFormat, Text.PlainText)
        mockTheme.uiSlots = {wallpaper: {}}
        verify(hint.visible)
        mockTheme.safeMode = true
        compare(hint.text, "wallpaper_theme_safe_mode")
        verify(style.enabled)
        compare(style.currentIndex, 3)
        mockTheme.uiSlots = {wallpaper: {source: "file:///theme/Wallpaper.qml"}}
        compare(hint.text, "wallpaper_theme_safe_mode")
        mockTheme.safeMode = false
        verify(!hint.visible)
        compare(mockManager.settings.style, "theme")
        mockTheme.uiSlots = ({})
        verify(hint.visible)
        style.forceActiveFocus()
        keyClick(Qt.Key_Up)
        compare(mockManager.settings.style, "lyrics")
        verify(!hint.visible)
    }
    function test_optionalThemeController() {
        mockManager.settings = Object.assign({}, mockManager.settings, {style: "theme"})
        const panel = createTemporaryObject(panelComponent, testCase, {width: 360})
        verify(panel)
        verify(!findChild(panel, "wallpaperThemeHint").visible)
        compare(findChild(panel, "wallpaperStyle").currentIndex, 3)
    }
    function test_perOutputThemeStatus_data() {
        return [
            {tag: "loading", status: "loading", key: "wallpaper_theme_loading"},
            {tag: "missing", status: "missing", key: "wallpaper_theme_missing"},
            {tag: "source-missing", status: "source-missing", key: "wallpaper_theme_source_missing"},
            {tag: "error", status: "error", key: "wallpaper_theme_error"},
            {tag: "safe-mode", status: "safe-mode", key: "wallpaper_theme_safe_mode"},
            {tag: "active", status: "active", key: ""},
            {tag: "builtin", status: "", key: ""}
        ]
    }
    function test_perOutputThemeStatus(data) {
        const rawError = "file:///theme/Wallpaper.qml:12: <b>TypeError & details</b>\nSecond line"
        mockManager.outputs = [
            {id: "edid", name: "DP-1", state: "running", error: "wallpaper_surface_lost",
                themeStatus: data.status, themeError: rawError},
            {id: "other", name: "HDMI-1", state: "running", error: "", themeStatus: "active", themeError: ""}
        ]
        const panel = createTemporaryObject(panelComponent, testCase, {width: 360})
        verify(panel)
        const hint = findChild(panel, "wallpaperThemeStatus_edid")
        verify(hint)
        compare(hint.visible, data.key !== "")
        compare(hint.text, data.key + (data.status === "error" ? "\n" + rawError : ""))
        compare(hint.textFormat, Text.PlainText)
        compare(findChild(panel, "wallpaperOutputStatus_edid").text,
            "DP-1 · wallpaper_output_running — wallpaper_surface_lost")
        verify(!findChild(panel, "wallpaperThemeStatus_other").visible)
        mockManager.outputs = [{id: "edid", name: "DP-1", state: "running", error: "", themeStatus: "active", themeError: ""}]
        verify(!findChild(panel, "wallpaperThemeStatus_edid").visible)
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
        verify(findChild(panel, "wallpaperSpectrum") === null)
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
        mockManager.settings = {style: "theme", target: "primary", updateHz: 30, intensity: 0.35, lyrics: true}
        mockManager.outputs = [{id: "edid", name: "DP-1", state: "running", error: "",
            themeStatus: "error", themeError: "Wallpaper.qml:12: <b>Unexpected token</b>"}]
        const panel = createTemporaryObject(panelComponent, testCase,
            {width: 360, translator: testTranslator, themeController: mockTheme})
        verify(panel)
        for (const language of ["zh", "en"]) {
            testTranslator.language = language
            waitForRendering(panel)
            const screenshot = testFixtures.screenshotPath("wallpaper-settings-" + language)
            compare(findChild(panel, "wallpaperStatus").text, testTranslator.text("wallpaper_qpa_unavailable", language))
            compare(findChild(panel, "wallpaperStyle").model[3], language === "zh" ? "跟随当前主题" : "Follow current theme")
            compare(findChild(panel, "wallpaperThemeHint").text, testTranslator.text("wallpaper_theme_missing", language))
            compare(findChild(panel, "wallpaperThemeStatus_edid").text,
                testTranslator.text("wallpaper_theme_error", language) + "\nWallpaper.qml:12: <b>Unexpected token</b>")
            if (screenshot) grabImage(panel).save(screenshot)
        }
    }
    function test_themeWallpaperSource() {
        const panel = createTemporaryObject(panelComponent, testCase, {width: 360, themeController: mockTheme})
        verify(panel)
        mockManager.settings = Object.assign({}, mockManager.settings, {style: "classic", themeWallpaper: ""})
        const picker = findChild(panel, "wallpaperThemeSource")
        const hint = findChild(panel, "wallpaperThemeHint")
        verify(picker)
        verify(!picker.visible)

        mockManager.settings = Object.assign({}, mockManager.settings, {style: "theme"})
        mockTheme.wallpaperSlots = {"pkg/wall": {source: "file:///pkg/Wallpaper.qml", title: {en: "Wall", zh: "壁纸"}}}
        mockTheme.items = [{id: "pkg/wall", title: {en: "Wall", zh: "壁纸"}, hasWallpaper: true}]
        verify(picker.visible)
        compare(picker.count, 2)
        compare(picker.currentIndex, 0)

        picker.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(mockManager.lastKey, "themeWallpaper")
        compare(mockManager.settings.themeWallpaper, "pkg/wall")
        verify(!hint.visible)

        mockTheme.wallpaperSlots = ({})
        mockTheme.items = []
        compare(picker.count, 2)
        compare(hint.text, "wallpaper_theme_source_missing")

        mockTheme.wallpaperSlots = {"pkg/wall": {source: "file:///pkg/Wallpaper.qml", title: "Wall"}}
        mockTheme.items = [{id: "pkg/wall", title: "Wall", hasWallpaper: true}]
        compare(picker.currentIndex, 1)
        verify(!hint.visible)
        mockTheme.safeMode = true
        compare(hint.text, "wallpaper_theme_safe_mode")
    }
    function test_themeWallpaperSourceTranslated_data() {
        return [{tag: "zh", language: "zh"}, {tag: "en", language: "en"}]
    }
    function test_themeWallpaperSourceTranslated(data) {
        testTranslator.language = data.language
        mockManager.settings = Object.assign({}, mockManager.settings, {style: "theme", themeWallpaper: "pkg/wall"})
        mockTheme.wallpaperSlots = {"pkg/wall": {source: "file:///pkg/Wallpaper.qml", title: {en: "Wall", zh: "壁纸"}}}
        mockTheme.items = [{id: "pkg/wall", title: {en: "Wall", zh: "壁纸"}, hasWallpaper: true}]
        const panel = createTemporaryObject(panelComponent, testCase,
            {width: 360, translator: testTranslator, themeController: mockTheme})
        verify(panel)
        const picker = findChild(panel, "wallpaperThemeSource")
        compare(picker.currentIndex, 1)
        compare(picker.displayText, data.language === "zh" ? "壁纸" : "Wall")
        waitForRendering(panel)
        let screenshot = testFixtures.screenshotPath("wallpaper-source-" + data.language)
        if (screenshot) grabImage(panel).save(screenshot)

        mockTheme.wallpaperSlots = ({})
        mockTheme.items = []
        compare(picker.displayText, testTranslator.text("wallpaper_theme_source_unavailable", data.language))
        compare(findChild(panel, "wallpaperThemeHint").text,
            testTranslator.text("wallpaper_theme_source_missing", data.language))
        waitForRendering(panel)
        screenshot = testFixtures.screenshotPath("wallpaper-source-missing-" + data.language)
        if (screenshot) grabImage(panel).save(screenshot)
        testTranslator.language = "en"
    }
    function test_untrustedWallpaperCanBeSelected_data() {
        return [{tag: "zh", language: "zh"}, {tag: "en", language: "en"}]
    }
    function test_untrustedWallpaperCanBeSelected(data) {
        testTranslator.language = data.language
        mockManager.settings = Object.assign({}, mockManager.settings, {style: "theme", themeWallpaper: ""})
        mockTheme.items = [
            {id: "pkg/palette", title: "Palette", hasWallpaper: false},
            {id: "pkg/wall", title: "Lumen", hasWallpaper: true, hasCode: true, trusted: false}
        ]
        const panel = createTemporaryObject(panelComponent, testCase,
            {width: 600, translator: testTranslator, themeController: mockTheme})
        const picker = findChild(panel, "wallpaperThemeSource")
        const dialog = findChild(panel, "wallpaperTrustDialog")
        compare(picker.count, 2)
        compare(picker.model[1].label, "Lumen · " + testTranslator.text("wallpaper_gallery_needs_trust", data.language))
        compare(Object.keys(mockTheme.wallpaperSlots).length, 0)
        const screenshot = testFixtures.screenshotPath("wallpaper-untrusted-source-" + data.language)
        if (screenshot) {
            picker.popup.open()
            tryCompare(picker.popup, "opened", true)
            waitForRendering(testCase)
            grabImage(testCase).save(screenshot)
            picker.popup.close()
        }

        picker.forceActiveFocus()
        keyClick(Qt.Key_Down)
        tryCompare(dialog, "visible", true)
        compare(mockTheme.calls, [{id: "pkg/wall", trusted: false}])
        compare(mockManager.settings.themeWallpaper, "")
        compare(picker.currentIndex, 0)
        dialog.reject()
        compare(mockManager.settings.themeWallpaper, "")
        compare(picker.currentIndex, 0)

        picker.forceActiveFocus()
        keyClick(Qt.Key_Down)
        tryCompare(dialog, "visible", true)
        dialog.accept()
        compare(mockTheme.calls[2], {id: "pkg/wall", trusted: true})
        compare(mockManager.settings.themeWallpaper, "pkg/wall")
        compare(picker.currentIndex, 1)
        compare(picker.displayText, "Lumen")
        verify(!mockManager.requestedEnabled)
    }
    function test_wallpaperSelectionWaitsForReady() {
        mockManager.settings = Object.assign({}, mockManager.settings, {style: "theme", themeWallpaper: ""})
        mockTheme.items = [{id: "pkg/wall", title: "Wall", hasWallpaper: true, trusted: true}]
        mockTheme.autoReady = false
        const panel = createTemporaryObject(panelComponent, testCase, {width: 360, themeController: mockTheme})
        const picker = findChild(panel, "wallpaperThemeSource")
        picker.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(mockManager.settings.themeWallpaper, "")
        mockTheme.wallpaperReady("pkg/other")
        compare(mockManager.settings.themeWallpaper, "")
        mockTheme.wallpaperReady("pkg/wall")
        compare(mockManager.settings.themeWallpaper, "pkg/wall")
        compare(picker.currentIndex, 1)
        picker.forceActiveFocus()
        keyClick(Qt.Key_Up)
        compare(mockManager.settings.themeWallpaper, "")
        mockTheme.wallpaperReady("pkg/wall")
        compare(mockManager.settings.themeWallpaper, "")
    }
    function test_wallpaperSourceRequiresAvailableController() {
        mockManager.settings = Object.assign({}, mockManager.settings, {style: "theme"})
        const panel = createTemporaryObject(panelComponent, testCase, {width: 360, themeController: mockTheme})
        const picker = findChild(panel, "wallpaperThemeSource")
        verify(picker.enabled)
        mockTheme.safeMode = true
        verify(!picker.enabled)
        mockTheme.safeMode = false
        mockTheme.connected = false
        verify(!picker.enabled)
        mockTheme.connected = true
        mockTheme.busy = true
        verify(!picker.enabled)
    }
    }
}
