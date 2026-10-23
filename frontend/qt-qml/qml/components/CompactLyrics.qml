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
    property bool wrap: false
    property bool outlined: false
    property bool reducedMotion: Theme.reducedMotion
    property bool animateLineChanges: true
    property int updateInterval: 16
    property real textLineHeight: Theme.lineHeightLyrics
    property real lineSpacing: 12
    property color activeColor: Theme.accentPrimary
    property color inactiveColor: outlined ? "#f5f1fa" : Theme.textSecondary
    property color previewColor: outlined ? "#f5f1fa" : Theme.textSecondary
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
    readonly property bool motionAllowed: animateLineChanges && running && visible && connected && !reducedMotion
    property var displayedFrame: null
    property var outgoingFrame: null
    property real transitionProgress: 1
    property int transitionDirection: 1
    implicitHeight: wrap ? currentFrame.implicitHeight : 0
    function t(key) { return translator ? translator.text(key, translator.language) : key }

    function finishTransition() {
        lineTransition.stop()
        transitionProgress = 1
        outgoingFrame = null
    }
    function synchronizeLine() {
        const previous = displayedFrame
        displayedFrame = {song: songIdentity, document: document, index: activeIndex,
                          lyric: currentLine, nextText: nextText}
        if (previous && previous.song === songIdentity && previous.document === document
                && previous.index === activeIndex) return
        finishTransition()
        if (!motionAllowed || !previous || previous.song !== songIdentity
                || previous.document !== document || activeIndex < 0) return
        outgoingFrame = {lyric: previous.lyric, nextText: previous.nextText,
                         index: previous.index, position: shownPosition}
        transitionDirection = activeIndex > previous.index ? 1 : -1
        lineTransition.restart()
    }
    // Coalesce dependent bindings so a frame always contains a matching lyric and preview.
    onActiveIndexChanged: Qt.callLater(synchronizeLine)
    onCurrentLineChanged: Qt.callLater(synchronizeLine)
    onNextTextChanged: Qt.callLater(synchronizeLine)
    onSongIdentityChanged: finishTransition()
    onDocumentChanged: finishTransition()
    onConnectedChanged: if (!connected) finishTransition()
    onMotionAllowedChanged: if (!motionAllowed) finishTransition()
    Component.onCompleted: synchronizeLine()

    LyricsClock {
        id: clock
        lines: root.lines
        position: root.position
        playbackRate: root.playbackRate
        playing: root.playing && root.connected
        running: root.running
        reducedMotion: root.reducedMotion
        updateInterval: root.updateInterval
    }
    component LyricFrame: Item {
        id: frame
        required property var lyric
        required property string nextText
        required property int activeIndex
        required property real position
        property bool outgoing: false
        readonly property string fullText: line.fullText
        implicitHeight: line.implicitHeight + (nextLabel.visible ? nextLabel.implicitHeight + root.lineSpacing : 0)
        LyricLine {
            id: line
            objectName: frame.outgoing ? "compactOutgoingLine" : "compactCurrentLine"
            lineIndex: frame.outgoing ? -1 : 0
            width: parent.width
            height: root.wrap ? implicitHeight : root.showNext ? parent.height * 0.57 : parent.height
            textLineHeight: root.textLineHeight
            lyric: frame.lyric
            position: frame.position
            wrap: root.wrap
            horizontalPadding: 2
            pixelSize: root.pixelSize
            outlined: root.outlined
            reducedMotion: root.reducedMotion || frame.outgoing
            activeColor: frame.activeIndex >= 0 ? root.activeColor : root.inactiveColor
            inactiveColor: root.inactiveColor
        }
        Text {
            id: nextLabel
            objectName: frame.outgoing ? "compactOutgoingNextLine" : "compactNextLine"
            anchors.top: line.bottom
            anchors.topMargin: root.wrap ? root.lineSpacing : 0
            width: parent.width
            height: root.wrap ? implicitHeight : root.showNext ? parent.height * 0.43 : 0
            visible: root.showNext && frame.nextText.length > 0
            text: frame.nextText
            textFormat: Text.PlainText
            font.family: Theme.fontFamily
            font.pixelSize: root.previewPixelSize
            wrapMode: root.wrap ? Text.Wrap : Text.NoWrap
            elide: root.wrap ? Text.ElideNone : Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: root.previewColor
            style: root.outlined ? Text.Outline : Text.Normal
            styleColor: "#18121f"
        }
    }
    LyricFrame {
        id: currentFrame
        objectName: "compactCurrentFrame"
        width: root.width; height: root.height
        lyric: root.currentLine
        nextText: root.nextText
        activeIndex: root.activeIndex
        position: clock.shownPosition
        opacity: root.transitionProgress
        transform: Translate { y: root.transitionDirection * 14 * (1 - root.transitionProgress) }
    }
    Loader {
        objectName: "compactOutgoingFrame"
        width: root.width; height: root.height
        active: root.outgoingFrame !== null
        opacity: 1 - root.transitionProgress
        transform: Translate { y: -root.transitionDirection * 14 * root.transitionProgress }
        sourceComponent: LyricFrame {
            outgoing: true
            lyric: root.outgoingFrame ? root.outgoingFrame.lyric : ({})
            nextText: root.outgoingFrame ? root.outgoingFrame.nextText : ""
            activeIndex: root.outgoingFrame ? root.outgoingFrame.index : -1
            position: root.outgoingFrame ? root.outgoingFrame.position : 0
        }
    }
    NumberAnimation {
        id: lineTransition
        objectName: "compactLineTransition"
        target: root; property: "transitionProgress"
        from: 0; to: 1
        duration: Theme.durationSmooth
        easing.type: Easing.OutCubic
        onFinished: root.outgoingFrame = null
    }
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered
    ToolTip.delay: 600
    ToolTip.text: currentFrame.fullText + (root.showNext && root.nextText ? "\n" + root.nextText : "")
}
