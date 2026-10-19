import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as Components

Item {
    id: scene
    width: 920; height: 300
    Component {
        id: factory
        Components.CompactLyrics { width: 440; height: 100; translator: i18n; showNext: true }
    }
    TestCase {
        name: "CompactLyrics"
        when: windowShown
        property var lyrics: null
        property var lines: [
            {time_ms: 1000, text: "第一句 <plain>", words: [{time_ms: 1000, duration_ms: 1000, text: "第一句 <plain>"}]},
            {time_ms: 3000, text: "第二句"},
            {time_ms: 5000, text: "最后一句"}
        ]
        function init() {
            i18n.language = "zh"
            lyrics = createTemporaryObject(factory, scene, {
                song: {resource_key: "track-a"},
                lyrics: {track_id: "track-a", state: "ready", document: {lines: lines}}
            })
            verify(lyrics !== null)
        }
        function cleanup() { lyrics = null }
        function test_boundariesAndPreview() {
            compare(lyrics.activeIndex, -1)
            compare(lyrics.nextText, lines[0].text)
            verify(lyrics.placeholder.indexOf("等待") >= 0)
            lyrics.position = 1000
            compare(lyrics.activeIndex, 0)
            compare(lyrics.nextText, "第二句")
            const text = findChild(lyrics, "krcBaseText0_0")
            compare(text.textFormat, Text.PlainText)
            compare(text.text, "第一句 <plain>")
            lyrics.position = 5000
            compare(lyrics.activeIndex, 2)
            compare(lyrics.nextText, "")
            lyrics.position = 0
            compare(lyrics.activeIndex, -1)
        }
        function test_switchTrackAndDisconnectClearStaleLyrics() {
            lyrics.position = 1500
            compare(lyrics.activeIndex, 0)
            lyrics.song = {resource_key: "track-b"}
            compare(lyrics.lines.length, 0)
            compare(lyrics.activeIndex, -1)
            lyrics.song = {song_hash: "track-a"}
            compare(lyrics.activeIndex, 0)
            lyrics.connected = false
            compare(lyrics.lines.length, 0)
            verify(lyrics.placeholder.indexOf("连接") >= 0)
        }
        function test_plainTextAndErrorFallback() {
            lyrics.lyrics = {track_id: "track-a", state: "ready", document: {plain_text: "\n  \n<plain> & text\nsecond"}}
            compare(lyrics.firstPlainLine, "<plain> & text")
            verify(lyrics.placeholder.indexOf("无法同步") >= 0)
            compare(lyrics.nextText, "")
            lyrics.lyrics = {track_id: "track-a", state: "error"}
            verify(lyrics.placeholder.indexOf("失败") >= 0)
        }
        function test_pauseSeekAndHiddenTimer() {
            lyrics.position = 1500
            lyrics.playing = true
            const timer = findChild(lyrics, "lyricsFrameTimer")
            verify(timer.running)
            wait(160)
            verify(lyrics.shownPosition <= 1620)
            lyrics.playing = false
            compare(lyrics.shownPosition, 1500)
            verify(!timer.running)
            const word = findChild(lyrics, "krcWord0_0")
            compare(word.progress, 0.5)
            lyrics.position = 1100
            compare(word.progress, 0.1)
            lyrics.playing = true
            lyrics.running = false
            verify(!timer.running)
        }
        function test_longWordsStayOnOneLineAndFollowHighlight() {
            const words = []
            for (let i = 0; i < 30; ++i) words.push({time_ms: 1000 + i * 200, duration_ms: 200, text: "很长的歌词 "})
            lyrics.lyrics = {track_id: "track-a", state: "ready", document: {lines: [{time_ms: 1000, words: words}]}}
            lyrics.width = 360
            lyrics.position = 1000
            const flow = findChild(lyrics, "krcLine0")
            tryVerify(() => flow.width > lyrics.width)
            const first = findChild(lyrics, "krcWord0_0")
            const last = findChild(lyrics, "krcWord0_29")
            compare(first.y, last.y)
            compare(flow.x, 0)
            lyrics.position = 6500
            verify(flow.x < 0)
            lyrics.position = 1000
            compare(flow.x, 0)
            const screenshot = testFixtures.screenshotPath("desktop-lyrics-content")
            if (screenshot) grabImage(lyrics).save(screenshot)
        }
        function test_zeroDurationGapsAndReducedMotion() {
            lyrics.lyrics = {track_id: "track-a", state: "ready", document: {lines: [{
                time_ms: 1000, words: [
                    {time_ms: 1000, duration_ms: 0, text: "瞬时"},
                    {time_ms: 2000, duration_ms: 1000, text: "间隔"}
                ]
            }]}}
            lyrics.position = 1000
            compare(findChild(lyrics, "krcWord0_0").progress, 1)
            compare(findChild(lyrics, "krcWord0_1").progress, 0)
            lyrics.position = 2500
            compare(findChild(lyrics, "krcWord0_1").progress, 0.5)
            lyrics.reducedMotion = true
            lyrics.playing = true
            verify(!findChild(lyrics, "lyricsFrameTimer").running)
            wait(50)
            compare(lyrics.shownPosition, 2500)
            lyrics.position = 0
            compare(lyrics.activeIndex, -1)
            lyrics.reducedMotion = false
            verify(findChild(lyrics, "lyricsFrameTimer").running)
            lyrics.visible = false
            verify(!findChild(lyrics, "lyricsFrameTimer").running)
        }
        function test_wrappingIsOptInAndReflowsBothLines() {
            lyrics.width = 320
            lyrics.pixelSize = 36
            lyrics.previewPixelSize = 22
            lyrics.lyrics = {track_id: "track-a", state: "ready", document: {lines: [
                {time_ms: 1000, text: "A long lyric line to read across several lines on a portrait screen"},
                {time_ms: 3000, text: "Another long preview line that should wrap rather than disappear"}
            ]}}
            lyrics.position = 1000
            const current = findChild(lyrics, "lyricText0")
            const next = findChild(lyrics, "compactNextLine")
            compare(current.wrapMode, Text.NoWrap)
            compare(next.wrapMode, Text.NoWrap)
            lyrics.wrap = true
            waitForRendering(lyrics)
            verify(current.lineCount > 1)
            verify(next.lineCount > 1)
            const narrowHeight = lyrics.implicitHeight
            lyrics.width = 900
            waitForRendering(lyrics)
            verify(lyrics.implicitHeight < narrowHeight)
            const words = []
            for (let i = 0; i < 12; ++i) words.push({time_ms: 1000 + i * 200, duration_ms: 200, text: "歌词 "})
            lyrics.width = 320
            lyrics.lyrics = {track_id: "track-a", document: {lines: [{time_ms: 1000, words: words}]}}
            const first = findChild(lyrics, "krcWord0_0")
            const last = findChild(lyrics, "krcWord0_11")
            tryVerify(() => last.y > first.y)
            compare(lyrics.activeIndex, 0)
        }
        function settleFirstLine() {
            lyrics.position = 1500
            wait(0)
            tryCompare(findChild(lyrics, "compactLineTransition"), "running", false)
        }
        function test_lineChangeCrossfadesBothRows() {
            settleFirstLine()
            const animation = findChild(lyrics, "compactLineTransition")
            const current = findChild(lyrics, "compactCurrentFrame")
            const outgoing = findChild(lyrics, "compactOutgoingFrame")
            lyrics.position = 3000
            tryCompare(animation, "running", true)
            verify(current.opacity < 1)
            verify(outgoing.opacity > 0)
            compare(findChild(lyrics, "compactCurrentLine").fullText, lines[1].text)
            compare(findChild(lyrics, "compactOutgoingLine").fullText, lines[0].text)
            compare(findChild(lyrics, "compactOutgoingNextLine").text, lines[1].text)
            compare(findChild(lyrics, "compactNextLine").text, lines[2].text)
            compare(lyrics.transitionDirection, 1)
            tryCompare(animation, "running", false)
            compare(current.opacity, 1)
            compare(outgoing.active, false)
            lyrics.position = 3200
            wait(0)
            compare(animation.running, false)
        }
        function test_rapidSeekUsesLatestLineAndReversesDirection() {
            settleFirstLine()
            const animation = findChild(lyrics, "compactLineTransition")
            lyrics.position = 3000
            tryCompare(animation, "running", true)
            lyrics.position = 5000
            tryVerify(() => lyrics.displayedFrame.index === 2)
            compare(findChild(lyrics, "compactOutgoingLine").fullText, lines[1].text)
            lyrics.position = 1500
            tryVerify(() => lyrics.displayedFrame.index === 0)
            compare(lyrics.transitionDirection, -1)
            compare(findChild(lyrics, "compactCurrentLine").fullText, lines[0].text)
            tryCompare(animation, "running", false)
            compare(findChild(lyrics, "compactOutgoingFrame").active, false)
            compare(findChild(lyrics, "krcWord0_0").progress, 0.5)
        }
        function test_disablingMotionFinishesTransition_data() {
            return [{tag: "reduced-motion", property: "reducedMotion", value: true},
                    {tag: "disabled", property: "animateLineChanges", value: false},
                    {tag: "suspended", property: "running", value: false},
                    {tag: "hidden", property: "visible", value: false}]
        }
        function test_disablingMotionFinishesTransition(data) {
            settleFirstLine()
            const animation = findChild(lyrics, "compactLineTransition")
            lyrics.position = 3000
            tryCompare(animation, "running", true)
            lyrics[data.property] = data.value
            compare(animation.running, false)
            compare(findChild(lyrics, "compactCurrentFrame").opacity, 1)
            compare(findChild(lyrics, "compactOutgoingFrame").active, false)
            lyrics.position = 5000
            wait(0)
            compare(animation.running, false)
            compare(findChild(lyrics, "compactCurrentLine").fullText, lines[2].text)
        }
        function test_trackAndDocumentChangesClearOutgoingLyrics_data() {
            return [{tag: "track"}, {tag: "disconnect"}, {tag: "document"}]
        }
        function test_trackAndDocumentChangesClearOutgoingLyrics(data) {
            settleFirstLine()
            const animation = findChild(lyrics, "compactLineTransition")
            lyrics.position = 3000
            tryCompare(animation, "running", true)
            if (data.tag === "track") lyrics.song = {resource_key: "track-b"}
            else if (data.tag === "disconnect") lyrics.connected = false
            else lyrics.lyrics = {track_id: "track-a", document: {lines: [{time_ms: 0, text: "replacement"}]}}
            compare(animation.running, false)
            compare(findChild(lyrics, "compactOutgoingFrame").active, false)
            wait(0)
            compare(animation.running, false)
            verify(findChild(lyrics, "compactCurrentLine").fullText !== lines[1].text)
        }
    }
}
