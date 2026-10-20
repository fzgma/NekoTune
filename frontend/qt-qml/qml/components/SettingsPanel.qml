pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../wallpaper"

// Settings displays account capability flags rather than stored credentials.
// The password input is write-only and cleared on hiding/submission.
// Save/clear failures settle their own busy state for explicit retry.
// Language changes use the shared translator and merged settings persistence.
// The AI panel uses its own endpoint-scoped key configuration lifecycle.
Item {
    id: root
    required property var client
    required property var translator
    property var ai: null
    property var audioOutput: null
    property var playback: null
    property var desktop: null
    property var windowModes: null
    property var wallpaper: null
    property var storage: null
    property bool saving: false
    property string message: ""
    property bool failed: false
    property var account: ({})
    property bool connected: true
    property string databasePath: ""
    property bool showDataLocation: false
    property var extensionSettings: []
    property bool appearanceAvailable: false
    signal appearanceRequested()
    signal extensionSettingsRequested(string id)

    function t(key) { return translator.text(key, translator.language) }

    ScrollView {
        id: settingsScroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
    ColumnLayout {
        width: settingsScroll.availableWidth
        spacing: 14

        Label {
            text: root.t("settings")
            color: Theme.textPrimary
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: generalSettings.implicitHeight + 36
            radius: Theme.radiusMd; color: Theme.bgSurface; border.color: Theme.borderSubtle
            ColumnLayout {
                id: generalSettings
                anchors.fill: parent; anchors.margins: 18; spacing: Theme.spaceMd
                Label { text: root.t("general_settings"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
                RowLayout {
                    Label { Layout.fillWidth: true; text: root.t("language"); color: Theme.textSecondary; font.pixelSize: Theme.fontBody }
                    TextButton { text: "中文"; subtle: root.translator.language !== "zh"; onClicked: root.translator.language = "zh" }
                    TextButton { text: "English"; subtle: root.translator.language !== "en"; onClicked: root.translator.language = "en" }
                }
                CheckBox {
                    objectName: "closeToTrayCheckBox"
                    Layout.fillWidth: true
                    visible: root.desktop !== null
                    text: root.t("close_to_tray")
                    font.pixelSize: Theme.fontBody
                    palette.windowText: Theme.textPrimary
                    palette.highlight: Theme.accentPrimary
                    checked: root.desktop ? root.desktop.closeToTray : false
                    onClicked: if (root.desktop) root.desktop.closeToTray = checked
                    Accessible.name: text
                }
                Label {
                    objectName: "trayUnavailableNotice"
                    Layout.fillWidth: true
                    visible: root.desktop !== null && !root.desktop.trayAvailable
                    text: root.t("tray_unavailable")
                    color: Theme.textMuted; font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }
            }
        }

        AudioEngineSettings {
            Layout.fillWidth: true
            visible: root.playback !== null && Boolean(root.playback.audioSettings) && Object.keys(root.playback.audioSettings).length > 0
            controller: root.playback
            translator: root.translator
            enabled: root.connected
        }
        LibraryStorageSettings {
            Layout.fillWidth: true
            visible: root.storage !== null
            controller: root.storage
            translator: root.translator
            connected: root.connected
        }

        PlaybackWindowSettings {
            Layout.fillWidth: true
            visible: root.windowModes !== null
            manager: root.windowModes
            translator: root.translator
        }

        WallpaperSettings {
            Layout.fillWidth: true
            visible: root.wallpaper !== null
            manager: root.wallpaper
            translator: root.translator
        }

        Rectangle {
            Layout.fillWidth: true
            visible: root.playback !== null
            implicitHeight: rateSettings.implicitHeight + 36
            radius: Theme.radiusMd; color: Theme.bgSurface; border.color: Theme.borderSubtle
            ColumnLayout {
                id: rateSettings
                anchors.fill: parent; anchors.margins: 18; spacing: Theme.spaceMd
                RowLayout {
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; text: root.t("playback_rate"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
                    PlaybackRateButton {
                        translator: root.translator
                        playbackRate: root.playback ? root.playback.playbackRate : 1.0
                        enabled: root.connected && root.playback !== null && root.playback.playbackRateSupported
                        onRateRequested: rate => root.playback.setPlaybackRate(rate)
                    }
                    TextButton {
                        objectName: "resetPlaybackRateButton"
                        text: root.t("playback_rate_reset")
                        subtle: true
                        enabled: root.connected && root.playback !== null
                        onClicked: root.playback.setPlaybackRate(1.0)
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: root.t(root.playback && root.playback.playbackRateSupported ? "playback_rate_hint" : "playback_rate_unavailable")
                    color: Theme.textMuted; font.pixelSize: Theme.fontCaption; wrapMode: Text.WordWrap
                }
            }
        }

        TextButton {
            objectName: "appearanceSettingsButton"
            Layout.fillWidth: true
            visible: root.appearanceAvailable
            text: root.t("appearance") + "  ›"
            subtle: true
            onClicked: root.appearanceRequested()
        }

        Rectangle {
            Layout.fillWidth: true
            visible: root.audioOutput !== null
            implicitHeight: outputSettings.implicitHeight + 36
            radius: Theme.radiusMd; color: Theme.bgSurface; border.color: Theme.borderSubtle
            ColumnLayout {
                id: outputSettings
                anchors.fill: parent; anchors.margins: 18; spacing: Theme.spaceMd
                Label { text: root.t("audio_output"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
                AudioOutputPicker {
                    objectName: "settingsAudioOutputPicker"
                    Layout.fillWidth: true
                    controller: root.audioOutput; translator: root.translator; connected: root.connected
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.extensionSettings.length > 0
            spacing: Theme.spaceSm
            Label { text: root.t("extension_settings"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
            Repeater {
                model: root.extensionSettings
                delegate: TextButton {
                    id: entry
                    required property var modelData
                    objectName: "settings_entry_" + modelData.id
                    Layout.fillWidth: true
                    text: modelData.title
                    subtle: true
                    contentItem: RowLayout {
                        Label { Layout.fillWidth: true; text: entry.text; color: Theme.textSecondary; textFormat: Text.PlainText; elide: Text.ElideRight }
                        Label { text: "›"; color: Theme.textMuted }
                    }
                    onClicked: root.extensionSettingsRequested(modelData.id)
                }
            }
        }

        AiSettingsPanel {
            Layout.fillWidth: true
            visible: root.ai !== null
            ai: root.ai
            translator: root.translator
            connected: root.connected
        }

        TextButton { visible: Boolean(root.databasePath); text: root.t("database"); subtle: true; onClicked: root.showDataLocation = !root.showDataLocation }
        TextArea {
            Layout.fillWidth: true
            visible: root.showDataLocation && Boolean(root.databasePath)
            text: root.databasePath
            readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
            color: Theme.textMuted; font.pixelSize: Theme.fontCaption
            background: Rectangle { color: Theme.bgSurface; radius: Theme.radiusSm }
            Accessible.name: root.t("database")
        }
    }
    }
}
