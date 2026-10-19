pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "../components"

Item {
    id: root
    objectName: "vinylWallpaper"
    required property var wallpaper
    readonly property bool stacked: width < height && height > 800
    readonly property real discSize: Math.max(280, Math.min(stacked ? width - 112 : width * 0.4, height * (stacked ? 0.4 : 0.64), 520))
    readonly property bool motionAllowed: visible && wallpaper.updating && !Theme.reducedMotion && wallpaper.preferences.intensity > 0

    GridLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 96, 1320)
        columns: root.stacked ? 1 : 2
        rowSpacing: 44; columnSpacing: 64

        Item {
            Layout.preferredWidth: root.discSize
            Layout.preferredHeight: root.discSize
            Layout.alignment: Qt.AlignHCenter
            Rectangle {
                anchors.centerIn: parent
                width: parent.width + 32; height: width; radius: width / 2
                color: Theme.accentPrimary; opacity: 0.08
            }
            Rectangle {
                id: record
                objectName: "wallpaperRecord"
                anchors.fill: parent
                radius: width / 2
                color: "#16141c"
                border.color: "#393440"; border.width: 2

                // Groove geometry is static; only the record transform animates.
                Repeater {
                    model: 18
                    delegate: Rectangle {
                        required property int index
                        anchors.centerIn: parent
                        width: record.width * (0.53 + index * 0.023)
                        height: width; radius: width / 2
                        color: "transparent"
                        border.color: index % 3 === 0 ? "#393440" : "#27232e"
                    }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * 0.53; height: width; radius: width / 2
                    gradient: Gradient {
                        GradientStop { position: 0; color: Theme.accentPrimary }
                        GradientStop { position: 1; color: Theme.accentSecondary }
                    }
                }
                AlbumArt {
                    anchors.centerIn: parent
                    width: Math.max(140, parent.width * 0.34); height: width
                    maxDimension: width
                    decodeScale: root.wallpaper.screen.devicePixelRatio
                    hasSong: root.wallpaper.hasSong
                    coverUrl: root.wallpaper.song.cover_url || ""
                    playbackState: root.wallpaper.playback.state
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 10; height: width; radius: 5
                    color: "#16141c"; border.color: "#a39aaf"
                }
                NumberAnimation on rotation {
                    objectName: "wallpaperRecordAnimation"
                    from: 0; to: 360
                    duration: 60000 - Math.round(root.wallpaper.preferences.intensity * 30000)
                    loops: Animation.Infinite
                    running: root.motionAllowed
                    paused: running && !root.wallpaper.playing
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: information.implicitHeight + 64
            radius: Theme.radiusLg
            color: Theme.bgSurface; border.color: Theme.borderSubtle
            ColumnLayout {
                id: information
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: 32
                spacing: 32
                WallpaperTrackInfo {
                    Layout.fillWidth: true
                    wallpaper: root.wallpaper
                    titleSize: root.width >= 1200 ? 32 : 26
                }
                CompactLyrics {
                    objectName: "wallpaperLyrics"
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.wallpaper.preferences.nextLine ? 112 : 64
                    visible: root.wallpaper.preferences.lyrics
                    song: root.wallpaper.song; lyrics: root.wallpaper.app.lyrics.current
                    translator: root.wallpaper.translator
                    position: root.wallpaper.playback.position; playbackRate: root.wallpaper.playback.playbackRate
                    playing: root.wallpaper.playing; connected: root.wallpaper.connected
                    running: root.wallpaper.updating && visible
                    animateLineChanges: root.wallpaper.preferences.intensity > 0
                    showNext: root.wallpaper.preferences.nextLine
                    updateInterval: root.wallpaper.preferences.updateHz === 60 ? 16 : 33
                    pixelSize: 26; previewPixelSize: 18
                }
            }
        }
    }
}
