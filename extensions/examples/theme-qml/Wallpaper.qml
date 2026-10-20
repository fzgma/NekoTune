pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import NekoTune 1.0
import "qrc:/qml/components" as Builtin

// The host owns the common background; this entry supplies the foreground layout.
Item {
    id: root
    objectName: "themeExampleWallpaper"
    anchors.fill: parent
    clip: true
    ExtensionApi { id: api }
    readonly property var wallpaper: api.wallpaper
    readonly property var preferences: wallpaper.preferences
    readonly property bool compact: width < 640 || height < 500
    readonly property bool stacked: width < 640 && height >= Math.max(480, width * 1.2)
    readonly property real inset: compact ? 12 : 32
    readonly property real padding: compact ? 12 : 32
    readonly property real intensity: Math.max(0, Math.min(1, preferences.intensity ?? 0.35))
    readonly property bool motionAllowed: visible && wallpaper.updating && wallpaper.playing
        && !api.theme.reducedMotion && intensity > 0
    readonly property real coverSize: Math.max(0, Math.min(280, card.width - padding * 2,
        width * (stacked ? 0.52 : 0.25), height * (stacked ? 0.32 : 0.42)))
    function t(key) { return i18n.text(key, i18n.language) }
    function timeLabel(milliseconds) {
        const seconds = Math.floor(Math.max(0, milliseconds) / 1000)
        return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
    }

    Rectangle {
        id: halo
        anchors.centerIn: card
        width: Math.max(card.width, card.height) * 1.15
        height: width
        radius: width / 2
        color: "transparent"
        border.width: root.compact ? 12 : 24
        border.color: api.theme.accentPrimary
        opacity: 0.025 + root.intensity * 0.055
        scale: 1 + breath * 0.06 * root.intensity
        property real breath: 0
        SequentialAnimation on breath {
            running: root.motionAllowed
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: 1; duration: 3600; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1; to: 0; duration: 3600; easing.type: Easing.InOutSine }
        }
    }

    Rectangle {
        id: card
        objectName: "themeWallpaperCard"
        anchors.centerIn: parent
        width: Math.max(0, Math.min(1040, parent.width - root.inset * 2))
        height: Math.min(parent.height - root.inset * 2, content.implicitHeight + root.padding * 2)
        radius: api.theme.radiusLg
        color: api.theme.bgSurface
        border.color: api.theme.borderSubtle
        clip: true

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: root.padding
            spacing: root.compact ? 12 : 28

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: root.stacked ? 1 : 2
                rowSpacing: root.compact ? 12 : 28
                columnSpacing: root.compact ? 16 : 32

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                    Layout.preferredWidth: root.coverSize
                    Layout.preferredHeight: root.coverSize
                    color: api.theme.bgRaised
                    clip: true
                    Text {
                        anchors.centerIn: parent
                        text: "♪"
                        textFormat: Text.PlainText
                        font.pixelSize: Math.max(24, root.coverSize * 0.35)
                        color: api.theme.accentPrimary
                    }
                    Image {
                        anchors.fill: parent
                        source: root.wallpaper.hasSong ? root.wallpaper.song.cover_url || "" : ""
                        sourceSize: Qt.size(Math.ceil(root.coverSize * (root.wallpaper.screen.devicePixelRatio || 1)),
                                            Math.ceil(root.coverSize * (root.wallpaper.screen.devicePixelRatio || 1)))
                        asynchronous: true
                        fillMode: Image.PreserveAspectCrop
                        visible: status === Image.Ready
                    }
                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.color: api.theme.borderSubtle
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: root.compact ? 4 : 12
                    Text {
                        Layout.fillWidth: true
                        visible: root.height >= 360
                        text: "NEKOTUNE / MINT"
                        textFormat: Text.PlainText
                        color: api.theme.textMuted
                        font.family: api.theme.fontFamily
                        font.pixelSize: 11; font.letterSpacing: 2
                        elide: Text.ElideRight
                    }
                    Text {
                        objectName: "themeWallpaperTitle"
                        Layout.fillWidth: true
                        text: root.wallpaper.hasSong
                            ? root.wallpaper.song.title || root.wallpaper.song.file_name || root.t("untitled")
                            : root.t("no_track_selected")
                        textFormat: Text.PlainText
                        color: api.theme.textPrimary
                        font.family: api.theme.fontFamily
                        font.pixelSize: root.compact ? 20 : 32; font.weight: Font.DemiBold
                        wrapMode: Text.Wrap; maximumLineCount: root.height < 360 ? 1 : 2; elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: [root.wallpaper.song.artist || "", root.wallpaper.song.album || ""].filter(Boolean).join(" · ")
                        textFormat: Text.PlainText
                        color: api.theme.textSecondary
                        font.family: api.theme.fontFamily
                        font.pixelSize: root.compact ? 12 : 16
                        maximumLineCount: root.compact ? 1 : 2; wrapMode: Text.Wrap; elide: Text.ElideRight
                        visible: text.length > 0
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.wallpaper.connected ? root.t("wallpaper_state_" + api.app.playback.state)
                                                       : root.t("tray_disconnected")
                        textFormat: Text.PlainText
                        color: api.theme.textMuted
                        font.family: api.theme.fontFamily; font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 3
                        radius: 1.5; color: api.theme.borderSubtle
                        Rectangle {
                            height: parent.height; radius: parent.radius
                            width: parent.width * Math.max(0, Math.min(1, root.wallpaper.duration > 0
                                ? root.wallpaper.position / root.wallpaper.duration : 0))
                            color: api.theme.accentPrimary
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.timeLabel(root.wallpaper.position) + " / " + root.timeLabel(root.wallpaper.duration)
                        textFormat: Text.PlainText
                        color: api.theme.textMuted
                        font.family: api.theme.fontFamilyMonospace; font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
            }

            Builtin.CompactLyrics {
                id: lyricView
                objectName: "themeWallpaperLyrics"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(root.height * 0.22,
                    root.preferences.nextLine ? (root.compact ? 84 : 116) : (root.compact ? 48 : 72))
                visible: Boolean(root.preferences.lyrics)
                clip: true
                song: root.wallpaper.song; lyrics: root.wallpaper.lyrics
                translator: i18n
                position: root.wallpaper.position; playbackRate: root.wallpaper.playbackRate
                playing: root.wallpaper.playing && root.wallpaper.updating
                connected: root.wallpaper.connected
                running: root.wallpaper.updating && lyricView.visible && root.visible
                reducedMotion: api.theme.reducedMotion
                animateLineChanges: root.intensity > 0
                showNext: Boolean(root.preferences.nextLine)
                updateInterval: root.preferences.updateHz === 60 ? 16 : 33
                pixelSize: root.compact ? 20 : 30
                previewPixelSize: root.compact ? 14 : 20
            }
        }
    }
}
