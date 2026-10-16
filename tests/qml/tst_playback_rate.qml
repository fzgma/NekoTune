import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as Components
import "../../frontend/qt-qml/qml/shell" as Shell

Rectangle {
    id: scene
    width: 1100; height: 760
    color: "#0E0D14"
    QtObject {
        id: rateController
        property real playbackRate: 1.0
        property bool playbackRateSupported: true
        property real requested: -1
        function setPlaybackRate(rate) { requested = rate }
    }
    QtObject { id: fakeTransport; property bool connected: true }
    QtObject {
        id: mockShell
        property real width: scene.width
        property bool hasSong: true
        property var song: ({title: "NekoTune", artist: "Playback speed", queue_id: 1})
        property var queue: [song]
        property real position: 12345
        property real duration: 180000
        property real volume: .8
        property bool isPlaying: true
        property var transport: fakeTransport
        property var app: null
        property var windowModes: null
        property bool queueOpen: false
        function openNowPlaying() {}
    }
    QtObject { id: settingsClient }
    Components.SettingsPanel {
        id: settings
        x: 24; y: 16; width: scene.width - 48; height: 600
        client: settingsClient; translator: i18n; playback: rateController
        connected: fakeTransport.connected
    }
    Shell.BottomPlayer {
        id: bar
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 100
        shell: mockShell; controller: rateController; translator: i18n
    }
    Component {
        id: clockFactory
        Components.LyricsClock {
            lines: [
                {time_ms: 1000, words: [{time_ms: 1000, duration_ms: 1000, text: "A"}]},
                {time_ms: 2000, text: "B"},
                {time_ms: 1800000, text: "End"}
            ]
        }
    }
    TestCase {
        name: "PlaybackRate"
        when: windowShown
        function button() { return findChild(bar, "playbackRateButton") }
        function menu() { return findChild(button(), "playbackRateMenu") }
        function init() {
            i18n.language = "zh"
            rateController.playbackRate = 1.0
            rateController.playbackRateSupported = true
            rateController.requested = -1
            fakeTransport.connected = true
            scene.width = 1100
            waitForRendering(scene)
        }
        function cleanup() { menu().close() }
        function test_selectionWaitsForConfirmationAndSettingsReset() {
            const anchor = button()
            compare(anchor.text, "1.0×")
            mouseClick(anchor)
            tryCompare(menu(), "opened", true)
            mouseClick(findChild(menu().contentItem, "playbackRate_1.5"))
            compare(rateController.requested, 1.5)
            compare(anchor.text, "1.0×")
            rateController.playbackRate = 1.5
            compare(anchor.text, "1.5×")
            compare(findChild(settings, "playbackRateButton").text, "1.5×")
            mouseClick(findChild(settings, "resetPlaybackRateButton"))
            compare(rateController.requested, 1.0)
            compare(anchor.text, "1.5×")
        }
        function test_keyboardAndDisconnect() {
            button().forceActiveFocus()
            keyClick(Qt.Key_Space)
            tryCompare(menu(), "opened", true)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(rateController.requested, 1.5)
            mouseClick(button())
            tryCompare(menu(), "opened", true)
            fakeTransport.connected = false
            tryCompare(menu(), "visible", false)
            verify(!button().enabled)
            verify(!findChild(settings, "resetPlaybackRateButton").enabled)
            fakeTransport.connected = true
            rateController.playbackRateSupported = false
            verify(!button().enabled)
            verify(findChild(settings, "resetPlaybackRateButton").enabled)
        }
        function test_layoutAndScreenshot() {
            for (const width of [960, 1100, 1400]) {
                scene.width = width
                waitForRendering(scene)
                const controls = findChild(bar, "transportControls")
                const anchor = button()
                const point = anchor.mapToItem(controls, 0, 0)
                verify(point.x >= 0 && point.x + anchor.width <= controls.width)
            }
            scene.width = 1100
            rateController.playbackRate = 1.5
            mouseClick(button())
            tryCompare(menu(), "opened", true)
            waitForRendering(menu().contentItem)
            verify(menu().y + menu().height <= scene.height - 8)
            const screenshot = testFixtures.screenshotPath("playback-rate")
            if (screenshot) grabImage(scene).save(screenshot)
        }
        function test_lyricsFollowMediaPositionAtEveryRate_data() {
            return [.5, .75, 1.0, 1.25, 1.5, 1.75, 2.0].map(rate => ({tag: String(rate), rate: rate}))
        }
        function test_lyricsFollowMediaPositionAtEveryRate(data) {
            const clock = createTemporaryObject(clockFactory, scene, {playbackRate: data.rate, playing: true})
            verify(clock)
            // Source timestamps stay unchanged at every speed, including a long playback session.
            for (const position of [999, 1000, 1500, 1999, 2000, 1800000, 500]) {
                clock.position = position
                compare(clock.shownPosition, position)
                compare(clock.activeIndex, position < 1000 ? -1 : position < 2000 ? 0 : position < 1800000 ? 1 : 2)
            }
            clock.position = 1900
            wait(180)
            verify(Math.abs(clock.shownPosition - (1900 + 120 * data.rate)) < 1)
            compare(clock.activeIndex, 0) // Interpolation must not select the next line early.
            wait(100)
            verify(Math.abs(clock.shownPosition - (1900 + 120 * data.rate)) < 1)
            clock.position = 1850 // Decoder correction overrides previous visual interpolation.
            compare(clock.shownPosition, 1850)
            clock.playbackRate = data.rate === 2 ? .5 : 2
            compare(clock.shownPosition, 1850)
            clock.playing = false
            compare(clock.shownPosition, 1850)
            verify(!findChild(clock, "lyricsFrameTimer").running)
        }
    }
}
