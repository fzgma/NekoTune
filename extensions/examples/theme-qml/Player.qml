import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import NekoTune 1.0
Rectangle {
    objectName: "themeExamplePlayer"
    ExtensionApi { id: api }
    color: api.theme.bgSidebar
    RowLayout {
        anchors.fill: parent; anchors.margins: api.theme.spaceLg
        Label {
            Layout.fillWidth: true
            text: api.app.playback.song.title || "NekoTune"
            textFormat: Text.PlainText
            color: api.theme.textPrimary
            font.family: api.theme.fontFamily; font.pixelSize: api.theme.fontBody
            elide: Text.ElideRight
        }
        Button {
            text: api.app.playback.state === "playing" ? "Ⅱ" : "▶"
            palette.button: api.theme.accentPrimary; palette.buttonText: api.theme.textOnAccent
            onClicked: api.app.playback.togglePlayPause()
        }
    }
}
