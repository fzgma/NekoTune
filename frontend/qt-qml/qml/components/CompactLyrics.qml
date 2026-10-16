pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Item {
    id: root
    property var song: ({})
    property var lyrics: ({})
    property var translator: null
    property real position: 0
    property real playbackRate: 1.0
    property bool playing: false
    property bool connected: true
    property bool running: visible
    property bool showNext: false
    property bool outlined: false
    property bool reducedMotion: Theme.reducedMotion
    property int pixelSize: 18
    property int previewPixelSize: 24
    readonly property string songIdentity: String(song.resource_key || song.song_hash || "")
    readonly property var current: songIdentity && lyrics.track_id === songIdentity ? lyrics : ({})
    readonly property var document: connected ? current.document || ({}) : ({})
    readonly property var lines: document.lines || []
    readonly property int activeIndex: clock.activeIndex
    readonly property string firstPlainLine: String(document.plain_text || "").split(/\r?\n/).find(line => line.trim().length > 0) || ""
    readonly property string placeholder: !connected ? t("tray_disconnected")
        : !songIdentity ? t("no_track_selected")
        : lines.length > 0 ? t("lyrics_waiting_line")
        : firstPlainLine ? firstPlainLine + " · " + t("lyrics_unsynced")
        : current.state === "error" ? t("compact_lyrics_error")
        : ["loading", "searching", "waiting_metadata"].indexOf(current.state) >= 0 ? t("lyrics_loading")
        : t("lyrics_not_found")
    readonly property var currentLine: activeIndex >= 0 ? lines[activeIndex] : ({text: placeholder})
    readonly property var nextLine: lines[activeIndex + 1] || null
    readonly property string nextText: nextLine ? String(nextLine.text || (nextLine.words || []).map(word => word.text).join("")) : ""
    readonly property real shownPosition: clock.shownPosition
    function t(key) { return translator ? translator.text(key, translator.language) : key }

    LyricsClock {
        id: clock
        lines: root.lines
        position: root.position
        playbackRate: root.playbackRate
        playing: root.playing && root.connected
        running: root.running
        reducedMotion: root.reducedMotion
    }
    LyricLine {
        id: currentLine
        objectName: "compactCurrentLine"
        width: parent.width
        height: root.showNext ? parent.height * 0.57 : parent.height
        lyric: root.currentLine
        position: clock.shownPosition
        wrap: false
        horizontalPadding: 2
        pixelSize: root.pixelSize
        outlined: root.outlined
        reducedMotion: root.reducedMotion
        activeColor: root.activeIndex >= 0 ? Theme.accentPrimary : root.outlined ? "#f5f1fa" : Theme.textSecondary
        inactiveColor: root.outlined ? "#f5f1fa" : Theme.textSecondary
    }
    Text {
        objectName: "compactNextLine"
        anchors.top: currentLine.bottom
        width: parent.width
        height: root.showNext ? parent.height * 0.43 : 0
        visible: root.showNext && root.nextText.length > 0
        text: root.nextText
        textFormat: Text.PlainText
        font.family: Theme.fontFamily
        font.pixelSize: root.previewPixelSize
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: root.outlined ? "#f5f1fa" : Theme.textSecondary
        style: root.outlined ? Text.Outline : Text.Normal
        styleColor: "#18121f"
    }
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered
    ToolTip.delay: 600
    ToolTip.text: currentLine.fullText + (root.showNext && root.nextText ? "\n" + root.nextText : "")
}
