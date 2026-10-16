pragma ComponentBehavior: Bound
import QtQuick
import "LyricTiming.js" as Timing

// Identical text layers keep word shaping stable while the reveal clip advances.
Item {
    id: line
    property var lyric: ({})
    property real position: 0
    property bool active: true
    property int distanceToActive: 0
    property int lineIndex: 0
    property bool wrap: true
    property int pixelSize: Theme.fontLyricsCompact
    property int horizontalPadding: 16
    property color activeColor: Theme.textPrimary
    property color nearColor: Theme.textSecondary
    property color inactiveColor: Theme.textMuted
    property bool hovered: false
    property bool outlined: false
    property bool reducedMotion: Theme.reducedMotion
    readonly property var words: lyric.words || []
    readonly property bool hasKrc: words.length > 0
    readonly property int activeWordIndex: active ? Timing.wordIndex(words, position) : -1
    readonly property string fullText: String(lyric.text || words.map(word => word.text).join("") || "♪")
    implicitHeight: Math.max(40, (hasKrc ? wordFlow.height : lineText.implicitHeight) + 12)
    clip: !wrap

    Text {
        id: lineText
        objectName: "lyricText" + line.lineIndex
        anchors.centerIn: parent
        width: Math.max(1, parent.width - line.horizontalPadding * 2)
        visible: !line.hasKrc
        text: line.fullText
        textFormat: Text.PlainText
        wrapMode: line.wrap ? Text.Wrap : Text.NoWrap
        elide: line.wrap ? Text.ElideNone : Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        font.family: Theme.fontFamily
        font.pixelSize: line.pixelSize
        font.weight: line.active ? Font.DemiBold : Font.Normal
        lineHeightMode: Text.ProportionalHeight
        lineHeight: Theme.lineHeightLyrics
        style: line.outlined ? Text.Outline : Text.Normal
        styleColor: "#18121f"
        color: line.active ? line.activeColor : line.hovered ? Theme.textPrimary
               : line.distanceToActive <= 1 ? line.nearColor : line.inactiveColor
        Behavior on color { ColorAnimation { duration: line.reducedMotion ? 0 : Theme.durationFast } }
    }

    Flow {
        id: wordFlow
        objectName: "krcLine" + line.lineIndex
        visible: line.hasKrc
        readonly property real unwrappedWidth: {
            let total = 0
            for (const child of children) {
                if (child !== wordRepeater) total += child.width
            }
            return total
        }
        width: Math.max(1, line.wrap ? Math.min(parent.width - line.horizontalPadding * 2, Math.ceil(unwrappedWidth)) : Math.ceil(unwrappedWidth))
        height: childrenRect.height
        // Keep the current reveal near the center without changing word geometry.
        readonly property real revealX: {
            let x = 0
            for (let i = 0; i < wordRepeater.count; ++i) {
                const item = wordRepeater.itemAt(i)
                if (!item) continue
                const progress = Timing.wordProgress(line.words[i], line.position)
                if (progress < 1) return x + item.width * progress
                x += item.width
            }
            return x
        }
        x: line.wrap || width <= parent.width ? (parent.width - width) / 2
           : -Math.max(0, Math.min(width - parent.width, revealX - parent.width / 2))
        y: (parent.height - height) / 2
        spacing: 0
        opacity: line.active || line.outlined ? 1 : line.distanceToActive <= 1 ? 0.82 : 0.62
        Repeater {
            id: wordRepeater
            model: line.words
            delegate: Item {
                id: wordItem
                required property var modelData
                required property int index
                readonly property real progress: Timing.wordProgress(modelData, line.position)
                objectName: "krcWord" + line.lineIndex + "_" + index
                width: baseWord.implicitWidth
                height: baseWord.implicitHeight * 1.35
                Text {
                    id: baseWord
                    objectName: "krcBaseText" + line.lineIndex + "_" + wordItem.index
                    anchors.verticalCenter: parent.verticalCenter
                    text: wordItem.modelData.text
                    textFormat: Text.PlainText
                    font.family: Theme.fontFamily
                    font.pixelSize: line.pixelSize
                    font.weight: Font.DemiBold
                    style: line.outlined ? Text.Outline : Text.Normal
                    styleColor: "#18121f"
                    color: line.active ? line.inactiveColor : line.distanceToActive <= 1 ? line.nearColor : line.inactiveColor
                }
                Item {
                    objectName: "krcReveal" + line.lineIndex + "_" + wordItem.index
                    width: wordItem.width * wordItem.progress
                    height: wordItem.height
                    clip: true
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: wordItem.modelData.text
                        textFormat: Text.PlainText
                        font.family: Theme.fontFamily
                        font.pixelSize: line.pixelSize
                        font.weight: Font.DemiBold
                        style: line.outlined ? Text.Outline : Text.Normal
                        styleColor: "#18121f"
                        color: line.active ? line.activeColor : line.distanceToActive <= 1 ? line.nearColor : line.inactiveColor
                    }
                }
            }
        }
    }
}
