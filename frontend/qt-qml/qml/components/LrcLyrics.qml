pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "LyricTiming.js" as Timing

// Consumes already parsed absolute line/word times from backend snapshots.
// LRC lines follow backend position; KRC can interpolate brief visual progress.
// Interpolation is bounded and re-anchored by every playback update.
// Manual scrolling suspends automatic follow so readers can inspect other lines.
// Seek requests use the line's timestamp rather than its list index.
// Plain lyrics use a separate wrapped view without timing selection.
Item {
    id: root

    property var lines: []
    property string plainText: ""
    property real position: 0
    property real playbackRate: 1.0
    property bool playing: false
    readonly property int activeIndex: clock.activeIndex
    readonly property real activeTime: activeIndex >= 0 ? Number(lines[activeIndex].time_ms) : -1
    readonly property bool hasWordTiming: clock.hasWordTiming
    readonly property real shownPosition: clock.shownPosition
    property color activeColor: Theme.textPrimary
    property color nearColor: Theme.textSecondary
    property color inactiveColor: Theme.textMuted
    property bool reducedMotion: Theme.reducedMotion

    property bool userScrolling: false
    property int lastAutoIndex: -1

    signal seekRequested(real positionMs)

    clip: true

    Timer {
        id: resumeAutoScrollTimer
        interval: 3500
        repeat: false
        onTriggered: {
            root.userScrolling = false
            root.scrollToActive(true)
        }
    }

    LyricsClock {
        id: clock
        lines: root.lines || []
        position: root.position
        playbackRate: root.playbackRate
        playing: root.playing
        running: root.visible && Boolean(root.Window.window && root.Window.window.visible)
        reducedMotion: root.reducedMotion
    }
    // Owned timers are cancelled with this view when a theme replaces the lyrics surface.
    Timer { id: recenterTimer; interval: 0; onTriggered: root.recenterActive() }
    Timer { id: followTimer; interval: 0; onTriggered: root.scrollToActive() }
    onLinesChanged: {
        followScroll.stop()
        lastAutoIndex = -1
        if (activeIndex < 0) lyricsList.positionViewAtBeginning()
        recenterTimer.restart()
    }
    onActiveIndexChanged: {
        if (!root.userScrolling) {
            followTimer.restart()
        }
    }

    ListView {
        id: lyricsList
        objectName: "lyricsList"
        anchors.fill: parent
        anchors.topMargin: 20
        anchors.bottomMargin: 20
        clip: true
        spacing: Theme.spaceLg
        // Leave room to center even the first and last lyric.
        topMargin: height / 2
        bottomMargin: height / 2
        cacheBuffer: Math.max(0, height)
        visible: root.lines && root.lines.length > 0
        model: root.lines
        currentIndex: -1
        boundsBehavior: Flickable.DragOverBounds
        onWidthChanged: recenterTimer.restart()
        onHeightChanged: recenterTimer.restart()
        onContentHeightChanged: recenterTimer.restart()

        NumberAnimation {
            id: followScroll
            target: lyricsList
            property: "contentY"
            duration: root.reducedMotion ? 0 : Theme.durationScroll
            easing.type: Easing.OutCubic
        }

        onDraggingChanged: {
            if (dragging) {
                followScroll.stop()
                root.userScrolling = true
                resumeAutoScrollTimer.restart()
            }
        }
        onFlickingChanged: {
            if (flicking) {
                followScroll.stop()
                root.userScrolling = true
                resumeAutoScrollTimer.restart()
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            width: 4
            contentItem: Rectangle { radius: 2; color: Theme.borderSubtle }
        }

        delegate: Item {
            id: lineDelegate
            required property var modelData
            required property int index

            objectName: "lyricLine" + index
            readonly property bool active: index === root.activeIndex
            readonly property int distanceToActive: Math.abs(index - root.activeIndex)
            readonly property real timeMs: Number(modelData.time_ms || 0)
            readonly property var words: modelData.words || []
            readonly property bool hasKrc: words.length > 0
            readonly property int activeWordIndex: active ? root.wordIndex(words, root.shownPosition) : -1

            width: lyricsList.width
            height: renderedLine.implicitHeight

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSm
                color: lineMouse.hovered ? Theme.bgHover : "transparent"
            }

            LyricLine {
                id: renderedLine
                anchors.fill: parent
                lyric: lineDelegate.modelData
                position: root.shownPosition
                active: lineDelegate.active
                distanceToActive: lineDelegate.distanceToActive
                lineIndex: lineDelegate.index
                pixelSize: root.width < 500 ? Theme.fontLyricsCompact : Theme.fontLyricsWide
                activeColor: root.activeColor
                nearColor: root.nearColor
                inactiveColor: root.inactiveColor
                hovered: lineMouse.hovered
                reducedMotion: root.reducedMotion
            }

            HoverHandler {
                id: lineMouse
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: {
                    if (lineDelegate.timeMs >= 0) {
                        root.userScrolling = false
                        resumeAutoScrollTimer.stop()
                        followScroll.stop()
                        // The clicked row is already positioned by this interaction.
                        // Remember its index so the arriving seek acknowledgment does not start another follow animation.
                        // Manual seeking also clears the temporary user-scroll suspension above.
                        root.lastAutoIndex = lineDelegate.index
                        root.seekRequested(lineDelegate.timeMs)
                        lyricsList.positionViewAtIndex(lineDelegate.index, ListView.Center)
                    }
                }
            }
        }
    }

    // Top fade overlay (events pass through!)
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 36
        enabled: false
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.bgCanvas }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Bottom fade overlay (events pass through!)
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 36
        enabled: false
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 1.0; color: Theme.bgCanvas }
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: Theme.spaceLg
        visible: (!root.lines || root.lines.length === 0) && root.plainText.length > 0
        contentWidth: availableWidth

        TextArea {
            text: root.plainText
            textFormat: TextEdit.PlainText
            readOnly: true
            wrapMode: TextEdit.Wrap
            color: Theme.textSecondary
            font.family: Theme.fontFamily
                font.pixelSize: 15
            horizontalAlignment: TextEdit.AlignHCenter
            background: null
        }
    }

    function wordIndex(words, timeMs) { return Timing.wordIndex(words, timeMs) }
    function wordProgress(word, timeMs) { return Timing.wordProgress(word, timeMs) }

    function recenterActive() { scrollToActive(false, true) }

    function scrollToActive(forceAnimation, forcePosition) {
        if (root.userScrolling || root.activeIndex < 0 || root.activeIndex >= lyricsList.count)
            return
        // Track the previous automatic target separately from the active timestamp lookup.
        // Only adjacent changes normally animate; a large seek can require direct delegate positioning.
        // Repeated updates within one lyric line must not restart the same scrolling animation.
        const previous = root.lastAutoIndex
        root.lastAutoIndex = root.activeIndex
        if (previous === root.activeIndex && !forceAnimation && !forcePosition) return
        lyricsList.forceLayout()
        const item = lyricsList.itemAtIndex(root.activeIndex)
        const adjacent = Math.abs(root.activeIndex - previous) === 1
        // Large seeks may target an uninstantiated delegate; let ListView position it directly.
        if (!root.reducedMotion && item && (adjacent || forceAnimation || followScroll.running)) {
            const targetY = item.y + item.height / 2 - lyricsList.height / 2
            if (followScroll.running && Math.abs(followScroll.to - targetY) < 0.5) return
            followScroll.stop()
            followScroll.from = lyricsList.contentY
            followScroll.to = targetY
            followScroll.start()
        } else {
            followScroll.stop()
            lyricsList.positionViewAtIndex(root.activeIndex, ListView.Center)
        }
    }
}
