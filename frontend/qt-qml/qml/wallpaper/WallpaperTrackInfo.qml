pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "../components"

ColumnLayout {
    id: root
    required property var wallpaper
    property int titleSize: 28
    spacing: 12

    Text {
        text: "NEKOTUNE"; color: Theme.textMuted
        font.family: Theme.fontFamily; font.pixelSize: 12; font.letterSpacing: 3
    }
    Text {
        objectName: "wallpaperTitle"
        Layout.fillWidth: true
        text: root.wallpaper.hasSong ? (root.wallpaper.song.title || root.wallpaper.song.file_name || root.wallpaper.t("untitled"))
                                    : root.wallpaper.t("no_track_selected")
        textFormat: Text.PlainText
        color: Theme.textPrimary; font.family: Theme.fontFamily
        font.pixelSize: root.titleSize; font.weight: Font.DemiBold
        wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
    }
    Text {
        Layout.fillWidth: true
        text: [root.wallpaper.song.artist || "", root.wallpaper.song.album || ""].filter(Boolean).join(" · ")
        textFormat: Text.PlainText; color: Theme.textSecondary
        font.family: Theme.fontFamily; font.pixelSize: 16
        wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
    }
    Text {
        objectName: "wallpaperConnection"
        Layout.fillWidth: true
        text: !root.wallpaper.connected ? root.wallpaper.t("tray_disconnected")
                                       : root.wallpaper.t("wallpaper_state_" + root.wallpaper.playback.state)
        textFormat: Text.PlainText; color: Theme.textMuted
        font.family: Theme.fontFamily; font.pixelSize: 13
    }
    Rectangle {
        Layout.fillWidth: true; Layout.preferredHeight: 3
        color: Theme.borderSubtle; radius: 1.5
        Rectangle {
            width: parent.width * Math.max(0, Math.min(1, root.wallpaper.playback.duration > 0
                ? root.wallpaper.playback.position / root.wallpaper.playback.duration : 0))
            height: parent.height; radius: parent.radius; color: Theme.accentPrimary
        }
    }
    Text {
        text: root.wallpaper.timeLabel(root.wallpaper.playback.position) + " / " + root.wallpaper.timeLabel(root.wallpaper.playback.duration)
        color: Theme.textMuted; font.pixelSize: 12; font.family: Theme.fontFamilyMonospace
    }
}
