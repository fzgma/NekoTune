pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

TextButton {
    id: button
    required property var translator
    property real playbackRate: 1.0
    readonly property var rates: [0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0]
    signal rateRequested(real rate)
    objectName: "playbackRateButton"
    text: formatRate(playbackRate)
    subtle: true
    subtleBg: "transparent"
    subtleBorder: "transparent"
    implicitWidth: 56
    implicitHeight: 32
    leftPadding: 6; rightPadding: 6
    Accessible.name: t("playback_rate") + ": " + text
    ToolTip.visible: hovered || activeFocus
    ToolTip.text: Accessible.name
    function t(key) { return translator.text(key, translator.language) }
    function formatRate(rate) { return (rate % 1 === 0 ? rate.toFixed(1) : String(rate)) + "×" }
    onEnabledChanged: if (!enabled) rateMenu.close()
    onClicked: {
        rateMenuContent.forceLayout()
        rateMenu.currentIndex = Math.max(0, rates.indexOf(playbackRate))
        const point = button.mapToItem(rateMenu.parent, button.width, button.height)
        const top = button.mapToItem(rateMenu.parent, 0, 0).y
        rateMenu.x = Math.max(8, Math.min(point.x - rateMenu.width, rateMenu.parent.width - rateMenu.width - 8))
        rateMenu.y = point.y + rateMenu.implicitHeight + 6 <= rateMenu.parent.height - 8
            ? point.y + 6 : Math.max(8, top - rateMenu.implicitHeight - 6)
        rateMenu.open()
    }
    Popup {
        id: rateMenu
        objectName: "playbackRateMenu"
        property int currentIndex: 2
        parent: Overlay.overlay
        width: 160
        padding: 6
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: contentItem.forceActiveFocus()
        onClosed: button.forceActiveFocus()
        function choose(index) {
            if (!button.enabled) return
            close()
            button.rateRequested(button.rates[index])
        }
        background: Rectangle {
            objectName: "shortcutBlocker"
            color: Theme.bgRaised; radius: Theme.radiusMd; border.color: Theme.borderSubtle
        }
        contentItem: Column {
            id: rateMenuContent
            spacing: 2
            focus: true
            Keys.onDownPressed: rateMenu.currentIndex = (rateMenu.currentIndex + 1) % button.rates.length
            Keys.onUpPressed: rateMenu.currentIndex = (rateMenu.currentIndex + button.rates.length - 1) % button.rates.length
            Keys.onReturnPressed: rateMenu.choose(rateMenu.currentIndex)
            Keys.onEnterPressed: rateMenu.choose(rateMenu.currentIndex)
            Keys.onSpacePressed: rateMenu.choose(rateMenu.currentIndex)
            Repeater {
                model: button.rates
                delegate: TextButton {
                    required property real modelData
                    required property int index
                    readonly property bool selected: button.playbackRate === modelData
                    objectName: "playbackRate_" + modelData
                    width: parent.width
                    text: (selected ? "✓  " : "    ") + button.formatRate(modelData)
                    subtle: true
                    subtleBg: rateMenu.currentIndex === index ? Theme.bgSelected : "transparent"
                    subtleBorder: "transparent"
                    subtleText: selected ? Theme.accentPrimary : Theme.textSecondary
                    Accessible.role: Accessible.MenuItem
                    Accessible.name: button.formatRate(modelData)
                    Accessible.checkable: true
                    Accessible.checked: selected
                    onHoveredChanged: if (hovered) rateMenu.currentIndex = index
                    onClicked: rateMenu.choose(index)
                }
            }
        }
    }
}
