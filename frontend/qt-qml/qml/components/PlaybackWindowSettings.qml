pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    required property var manager
    required property var translator
    function t(key) { return translator.text(key, translator.language) }
    implicitHeight: content.implicitHeight + 36
    radius: Theme.radiusMd; color: Theme.bgSurface; border.color: Theme.borderSubtle
    ColumnLayout {
        id: content
        anchors.fill: parent; anchors.margins: 18; spacing: Theme.spaceMd
        Label { text: panel.t("window_modes"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
        TextButton {
            objectName: "settingsMiniModeButton"
            text: panel.t(panel.manager && panel.manager.miniMode ? "show_main_window" : "mini_mode")
            onClicked: if (panel.manager) panel.manager.miniMode = !panel.manager.miniMode
        }
        CheckBox {
            objectName: "miniAlwaysOnTopCheckBox"
            text: panel.t("mini_always_on_top"); palette.windowText: Theme.textPrimary
            checked: panel.manager ? panel.manager.miniAlwaysOnTop : false
            onClicked: if (panel.manager) panel.manager.miniAlwaysOnTop = checked
        }
        CheckBox {
            objectName: "desktopLyricsCheckBox"
            text: panel.t("desktop_lyrics"); palette.windowText: Theme.textPrimary
            enabled: panel.manager !== null && panel.manager.desktopLyricsSupported
            checked: panel.manager ? panel.manager.desktopLyricsVisible : false
            onClicked: if (panel.manager) panel.manager.desktopLyricsVisible = checked
        }
        CheckBox {
            objectName: "desktopLyricsLockCheckBox"
            text: panel.t("desktop_lyrics_lock"); palette.windowText: Theme.textPrimary
            enabled: panel.manager !== null && panel.manager.desktopLyricsVisible && panel.manager.canLockDesktopLyrics
            checked: panel.manager ? panel.manager.desktopLyricsLocked : false
            onClicked: if (panel.manager) panel.manager.desktopLyricsLocked = checked
        }
        Label {
            objectName: "desktopLyricsPlatformNotice"
            Layout.fillWidth: true
            visible: panel.manager !== null && !panel.manager.desktopLyricsSupported
            text: panel.manager ? panel.t(panel.manager.desktopLyricsUnavailableReason) : ""
            textFormat: Text.PlainText; wrapMode: Text.WordWrap
            color: Theme.textMuted; font.pixelSize: Theme.fontCaption
        }
        Label {
            Layout.fillWidth: true
            visible: panel.manager !== null && panel.manager.wayland
            text: panel.t("mini_wayland_notice"); wrapMode: Text.WordWrap
            color: Theme.textMuted; font.pixelSize: Theme.fontCaption
        }
        Label {
            Layout.fillWidth: true
            visible: panel.manager !== null && panel.manager.desktopLyricsSupported && !panel.manager.canLockDesktopLyrics
            text: panel.t("desktop_lyrics_lock_unavailable"); wrapMode: Text.WordWrap
            color: Theme.textMuted; font.pixelSize: Theme.fontCaption
        }
    }
}
