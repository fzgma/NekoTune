pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "../components"

Item {
    id: root
    objectName: "lyricsWallpaper"
    required property var wallpaper

    Rectangle {
        id: stage
        anchors.fill: parent
        anchors.margins: 32
        radius: Theme.radiusLg
        color: Theme.bgSurface; border.color: Theme.borderSubtle

        RowLayout {
            id: header
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: 40
            spacing: 32
            AlbumArt {
                Layout.preferredWidth: 140; Layout.preferredHeight: 140
                maxDimension: 140
                decodeScale: root.wallpaper.screen.devicePixelRatio
                hasSong: root.wallpaper.hasSong
                coverUrl: root.wallpaper.song.cover_url || ""
                playbackState: root.wallpaper.playback.state
            }
            WallpaperTrackInfo {
                Layout.fillWidth: true
                wallpaper: root.wallpaper
                titleSize: 24
            }
        }

        Item {
            anchors.top: header.bottom; anchors.bottom: footer.top
            anchors.left: parent.left; anchors.right: parent.right
            anchors.margins: 40
            CompactLyrics {
                id: lyrics
                objectName: "wallpaperLyrics"
                anchors.centerIn: parent
                width: Math.min(parent.width, 1400)
                height: Math.min(implicitHeight, parent.height)
                clip: true
                visible: root.wallpaper.preferences.lyrics
                song: root.wallpaper.song; lyrics: root.wallpaper.app.lyrics.current
                translator: root.wallpaper.translator
                position: root.wallpaper.playback.position; playbackRate: root.wallpaper.playback.playbackRate
                playing: root.wallpaper.playing; connected: root.wallpaper.connected
                running: root.wallpaper.updating && visible
                animateLineChanges: root.wallpaper.preferences.intensity > 0
                showNext: root.wallpaper.preferences.nextLine
                updateInterval: root.wallpaper.preferences.updateHz === 60 ? 16 : 33
                wrap: true
                pixelSize: root.width >= 1200 ? 56 : 36
                previewPixelSize: root.width >= 1200 ? 28 : 22
            }
        }
        Text {
            id: footer
            anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: 32
            text: "NEKOTUNE  /  " + root.wallpaper.t("wallpaper_style_lyrics")
            textFormat: Text.PlainText
            font.family: Theme.fontFamily; font.pixelSize: 12; font.letterSpacing: 2
            color: Theme.textMuted
        }
    }
}
