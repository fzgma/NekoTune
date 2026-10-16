pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Window {
    id: window
    objectName: "desktopLyricsWindow"
    width: 900; height: 150
    minimumWidth: 360; minimumHeight: 120
    visible: false
    color: "transparent"
    title: "NekoTune · " + t("desktop_lyrics")
    required property var manager
    required property var app
    required property var translator
    required property var transport
    function t(key) { return translator.text(key, translator.language) }

    Item {
        anchors.fill: parent
        HoverHandler { id: windowHover }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            color: "#b318121f"
            visible: !window.manager.desktopLyricsLocked && windowHover.hovered
        }
        WindowDragArea {
            anchors.fill: parent
            enabled: !window.manager.desktopLyricsLocked
            controller: window.manager; hostWindow: window
        }
        CompactLyrics {
            anchors.fill: parent; anchors.margins: 12; anchors.topMargin: 30
            song: window.app.playback.song; lyrics: window.app.lyrics.current
            translator: window.translator; connected: window.transport.connected
            position: window.app.playback.position; playing: window.app.playback.state === "playing"
            playbackRate: window.app.playback.playbackRate
            running: window.visible && window.visibility !== Window.Minimized
            showNext: true; outlined: true; pixelSize: 32; previewPixelSize: 24
        }
        RowLayout {
            anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 3
            visible: !window.manager.desktopLyricsLocked && windowHover.hovered
            spacing: 2
            Label { text: window.t("desktop_lyrics_drag"); color: "#f5f1fa"; font.pixelSize: 12 }
            IconButton {
                objectName: "desktopLyricsLockButton"
                kind: "lock"; implicitWidth: 28; implicitHeight: 28; glyphColor: "#f5f1fa"
                enabled: window.manager.canLockDesktopLyrics
                tooltipText: window.t(enabled ? "desktop_lyrics_lock" : "desktop_lyrics_lock_unavailable")
                onClicked: window.manager.desktopLyricsLocked = true
            }
            IconButton {
                objectName: "desktopLyricsCloseButton"
                kind: "close"; implicitWidth: 28; implicitHeight: 28; glyphColor: "#f5f1fa"
                tooltipText: window.t("close")
                onClicked: window.close()
            }
        }
        WindowDragArea {
            anchors.right: parent.right; anchors.bottom: parent.bottom
            width: 16; height: 16
            visible: !window.manager.desktopLyricsLocked
            controller: window.manager; hostWindow: window
            resizeEdges: Qt.RightEdge | Qt.BottomEdge
        }
    }
}
