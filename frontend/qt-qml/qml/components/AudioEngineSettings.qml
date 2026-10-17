pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: root
    required property var controller
    required property var translator
    readonly property var settings: controller ? (controller.audioSettings || {}) : ({})
    readonly property var eq: settings.equalizer || ({})
    readonly property var gains: eq.gains || [0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
    property var pendingGains: null
    readonly property var shownGains: pendingGains || gains
    readonly property var frequencies: settings.frequencies || [31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
    readonly property var presetValues: [[0,0,0,0,0,0,0,0,0,0], [6,5,4,2,0,0,0,0,0,0], [-2,-2,-1,0,2,3,3,2,0,-1], [0,0,0,-1,-1,0,1,3,4,4]]
    readonly property var presetNames: ["flat", "bass", "vocal", "bright"]
    function t(key) { return translator.text(key, translator.language) }
    function presetIndex() {
        for (let i = 0; i < presetValues.length; ++i) {
            if (presetValues[i].every((value, band) => Math.abs(value - root.gains[band]) < 0.01)) return i
        }
        return 4
    }
    function queueGain(index, value) {
        let values = shownGains.slice()
        values[index] = Math.round(value * 10) / 10
        pendingGains = values
        gainTimer.restart()
    }
    objectName: "audioEngineSettings"
    implicitHeight: content.implicitHeight + 36
    radius: Theme.radiusMd; color: Theme.bgSurface; border.color: Theme.borderSubtle
    Timer {
        id: gainTimer
        interval: 50
        onTriggered: if (root.controller && root.pendingGains) root.controller.setEqualizer({gains: root.pendingGains})
    }
    Connections {
        target: root.controller
        ignoreUnknownSignals: true
        function onAudioSettingsChanged() { if (!gainTimer.running) root.pendingGains = null }
    }
    ColumnLayout {
        id: content
        anchors.fill: parent; anchors.margins: 18; spacing: Theme.spaceMd
        Label { text: root.t("audio_engine"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: root.t("audio_backend"); color: Theme.textSecondary }
            ComboBox {
                objectName: "audioBackendChoice"
                model: ["FFmpeg", "Qt Multimedia"]
                currentIndex: root.settings.backend === "qt" ? 1 : 0
                onActivated: if (root.controller) root.controller.setAudioBackend(currentIndex === 1 ? "qt" : "ffmpeg")
                Accessible.name: root.t("audio_backend")
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.t("audio_backend_actual") + ": " + (root.settings.active_backend || "—")
            color: Theme.textMuted
        }
        Label {
            objectName: "audioBackendRestartNotice"
            Layout.fillWidth: true; visible: Boolean(root.settings.restart_required)
            text: root.t("audio_backend_restart"); color: Theme.textSecondary; wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            text: root.t("audio_gapless_hint"); color: Theme.textMuted; wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: root.t("audio_crossfade"); color: Theme.textSecondary }
            Slider {
                id: crossfade
                objectName: "audioCrossfadeSlider"
                Layout.fillWidth: true; from: 0; to: 12; stepSize: 0.5
                enabled: Boolean(root.settings.crossfade_supported)
                value: root.settings.crossfade_seconds || 0
                onMoved: fadeTimer.restart()
                Accessible.name: root.t("audio_crossfade")
            }
            Label {
                Layout.preferredWidth: 56
                text: crossfade.value > 0 ? crossfade.value.toFixed(1) + " s" : root.t("audio_crossfade_off")
                color: Theme.textSecondary
            }
            Timer { id: fadeTimer; interval: 100; onTriggered: if (root.controller) root.controller.setCrossfade(crossfade.value) }
        }
        Label {
            Layout.fillWidth: true; visible: !root.settings.equalizer_supported
            text: root.t("audio_eq_unavailable"); color: Theme.textMuted; wrapMode: Text.WordWrap
        }
        CheckBox {
            objectName: "audioEqualizerEnabled"
            text: root.t("audio_eq_enable"); checked: Boolean(root.eq.enabled)
            enabled: Boolean(root.settings.equalizer_supported)
            onClicked: if (root.controller) root.controller.setEqualizer({enabled: checked})
            palette.windowText: Theme.textPrimary; Accessible.name: text
        }
        ColumnLayout {
            Layout.fillWidth: true
            enabled: Boolean(root.settings.equalizer_supported) && Boolean(root.eq.enabled)
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: root.t("audio_eq_preset"); color: Theme.textSecondary }
                ComboBox {
                    objectName: "audioEqualizerPreset"
                    model: [root.t("audio_eq_flat"), root.t("audio_eq_bass"), root.t("audio_eq_vocal"), root.t("audio_eq_bright"), root.t("audio_eq_custom")]
                    currentIndex: root.presetIndex()
                    onActivated: if (root.controller && currentIndex < 4) {
                        gainTimer.stop(); root.pendingGains = null
                        root.controller.setEqualizer({preset: root.presetNames[currentIndex]})
                    }
                    Accessible.name: root.t("audio_eq_preset")
                }
            }
            CheckBox {
                objectName: "audioEqualizerHeadroom"
                text: root.t("audio_eq_headroom"); checked: root.eq.auto_headroom !== false
                onClicked: if (root.controller) root.controller.setEqualizer({auto_headroom: checked})
                palette.windowText: Theme.textPrimary; Accessible.name: text
            }
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.preferredWidth: 72; text: root.t("audio_eq_preamp"); color: Theme.textSecondary }
                Slider {
                    id: preamp
                    objectName: "audioEqualizerPreamp"
                    Layout.fillWidth: true; from: -12; to: 12; stepSize: 0.5
                    value: root.eq.preamp_db || 0
                    onMoved: preampTimer.restart()
                    Accessible.name: root.t("audio_eq_preamp")
                }
                Label { Layout.preferredWidth: 60; text: preamp.value.toFixed(1) + " dB"; color: Theme.textSecondary }
                Timer { id: preampTimer; interval: 50; onTriggered: if (root.controller) root.controller.setEqualizer({preamp_db: preamp.value}) }
            }
            Repeater {
                model: 10
                delegate: RowLayout {
                    id: band
                    required property int index
                    readonly property int frequency: root.frequencies[index]
                    Layout.fillWidth: true
                    enabled: Boolean((root.settings.available_bands || [])[index])
                    Label {
                        Layout.preferredWidth: 72
                        text: band.frequency < 1000 ? band.frequency + " Hz" : band.frequency / 1000 + " kHz"
                        color: Theme.textSecondary
                    }
                    Slider {
                        objectName: "audioEqualizerBand" + band.index
                        Layout.fillWidth: true; from: -12; to: 12; stepSize: 0.5
                        value: root.shownGains[band.index]
                        onMoved: root.queueGain(band.index, value)
                        Accessible.name: band.frequency + " Hz"
                        ToolTip.visible: hovered && !band.enabled
                        ToolTip.text: root.t("audio_eq_band_unavailable")
                    }
                    Label { Layout.preferredWidth: 60; text: Number(root.shownGains[band.index]).toFixed(1) + " dB"; color: Theme.textSecondary }
                }
            }
        }
    }
}
