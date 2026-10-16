import QtQuick
import "LyricTiming.js" as Timing

// Shared bounded visual interpolation; hidden windows never run a frame timer.
Item {
    id: clock
    property var lines: []
    property real position: 0
    property real playbackRate: 1.0
    property bool playing: false
    property bool running: visible
    property bool reducedMotion: Theme.reducedMotion
    readonly property bool hasWordTiming: lines.some(line => line.words && line.words.length > 0)
    property real displayPosition: position
    property real anchorPosition: position
    property real anchorClock: Date.now()
    readonly property real shownPosition: playing && hasWordTiming && !reducedMotion ? displayPosition : position
    readonly property int activeIndex: Timing.activeIndex(lines, position)

    function reset() {
        displayPosition = position
        anchorPosition = position
        anchorClock = Date.now()
    }
    onLinesChanged: reset()
    onPlayingChanged: reset()
    onPlaybackRateChanged: reset()
    onRunningChanged: reset()
    onPositionChanged: {
        displayPosition = position
        anchorPosition = position
        anchorClock = Date.now()
    }
    Timer {
        objectName: "lyricsFrameTimer"
        interval: 16
        repeat: true
        running: clock.running && clock.playing && clock.hasWordTiming && !clock.reducedMotion
        // Only smooth words between decoder ticks; never accumulate a wall-clock timeline.
        onTriggered: clock.displayPosition = clock.anchorPosition
            + Math.max(0, Math.min(120, Date.now() - clock.anchorClock)) * clock.playbackRate
    }
}
