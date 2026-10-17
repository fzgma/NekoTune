import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as Components
Rectangle {
    id: scene
    width: 900; height: 1100
    color: "#0E0D14"
    QtObject {
        id: audio
        property var audioSettings: ({})
        property string requestedBackend: ""
        property var requestedEq: ({})
        property real requestedFade: -1
        function setAudioBackend(backend) { requestedBackend = backend }
        function setEqualizer(settings) { requestedEq = settings }
        function setCrossfade(seconds) { requestedFade = seconds }
    }
    Components.AudioEngineSettings {
        id: panel
        x: 20; y: 12; width: scene.width - 40; height: implicitHeight
        controller: audio; translator: i18n
    }
    TestCase {
        name: "AudioEngine"
        when: windowShown
        function init() {
            audio.audioSettings = {backend: "ffmpeg", active_backend: "ffmpeg", crossfade_seconds: 0,
                crossfade_supported: true, equalizer_supported: true, restart_required: false,
                available_bands: [true,true,true,true,true,true,true,true,true,false],
                equalizer: {enabled: true, auto_headroom: true, preamp_db: 0, gains: [0,0,0,0,0,0,0,0,0,0]}}
            audio.requestedBackend = ""; audio.requestedEq = ({}); audio.requestedFade = -1
            waitForRendering(panel)
        }
        function test_bandCommandsAndUnavailableFrequency() {
            const slider = findChild(panel, "audioEqualizerBand0")
            slider.forceActiveFocus()
            keyClick(Qt.Key_Right)
            tryVerify(() => audio.requestedEq.gains !== undefined)
            compare(audio.requestedEq.gains[0], 0.5)
            compare(audio.audioSettings.equalizer.gains[0], 0)
            verify(!findChild(panel, "audioEqualizerBand9").enabled)
        }
        function test_backendPreferenceWaitsForConfirmation() {
            const choice = findChild(panel, "audioBackendChoice")
            choice.forceActiveFocus()
            keyClick(Qt.Key_Space); keyClick(Qt.Key_Down); keyClick(Qt.Key_Return)
            tryCompare(audio, "requestedBackend", "qt")
            compare(audio.audioSettings.active_backend, "ffmpeg")
            let settings = Object.assign({}, audio.audioSettings)
            settings.backend = "qt"; settings.restart_required = true
            audio.audioSettings = settings
            verify(findChild(panel, "audioBackendRestartNotice").visible)
        }
        function test_layout() {
            for (const width of [640, 900]) {
                scene.width = width
                waitForRendering(panel)
                for (let i = 0; i < 10; ++i) {
                    const slider = findChild(panel, "audioEqualizerBand" + i)
                    const point = slider.mapToItem(panel, 0, 0)
                    verify(point.x >= 0 && point.x + slider.width <= panel.width)
                    verify(point.y >= 0 && point.y + slider.height <= panel.height)
                }
            }
            scene.width = 900
            waitForRendering(panel)
            const screenshot = testFixtures.screenshotPath("audio-engine")
            if (screenshot) grabImage(scene).save(screenshot)
        }
        function test_fallbackDisablesDspAndKeepsSettings() {
            let settings = Object.assign({}, audio.audioSettings)
            settings.active_backend = "qt"; settings.equalizer_supported = false; settings.crossfade_supported = false
            audio.audioSettings = settings
            verify(!findChild(panel, "audioEqualizerEnabled").enabled)
            verify(!findChild(panel, "audioCrossfadeSlider").enabled)
            compare(audio.audioSettings.equalizer.gains.length, 10)
        }
    }
}
