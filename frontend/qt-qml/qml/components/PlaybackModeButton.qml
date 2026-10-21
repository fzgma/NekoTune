pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import NekoTune 1.0

// The icon/checkmark follows the backend-confirmed playbackMode property.
// Selecting an option only emits a request, never assumes persistence success.
// Menu keyboard selection is temporary state separate from confirmed mode.
// Disable closes the popup when transport availability changes.
// Closing returns focus to the button for predictable keyboard navigation.
IconButton {
    id: button
    property string playbackMode: "sequential"
    property var shuffleStrategies: [{ id: "shuffle_bag", name: "Shuffle bag" }]
    property string shuffleStrategy: "shuffle_bag"
    property string effectiveShuffleStrategy: "shuffle_bag"
    property var controllers: null
    property var hostWindow: null
    property var shuffleSettings: []
    readonly property var strategySettings: shuffleSettings.find(page =>
        page.extensionId + "/" + page.shuffleStrategy === effectiveShuffleStrategy) || null
    required property var translator
    readonly property var modes: ["sequential", "repeat_one", "shuffle", "repeat_all"]
    readonly property int entryCount: modes.length + (playbackMode === "shuffle"
        ? shuffleStrategies.length + (strategySettings ? 1 : 0) : 0)
    signal modeRequested(string mode)
    signal strategyRequested(string id)
    onEntryCountChanged: modeMenu.currentIndex = Math.max(0, Math.min(modeMenu.currentIndex, entryCount - 1))
    objectName: "playbackModeButton"
    kind: playbackMode
    tooltipText: t("playback_order") + ": " + t("playback_mode_" + playbackMode)
    function t(key) { return translator.text(key, translator.language) }
    function strategyName(strategy) { return strategy.id === "shuffle_bag" ? t("shuffle_bag") : strategy.name }
    function placePopup(popup) {
        const point = button.mapToItem(popup.parent, button.width, button.height)
        const top = button.mapToItem(popup.parent, 0, 0).y
        popup.x = Math.max(8, Math.min(point.x - popup.width, popup.parent.width - popup.width - 8))
        popup.y = point.y + popup.implicitHeight + 6 <= popup.parent.height - 8
            ? point.y + 6 : Math.max(8, top - popup.implicitHeight - 6)
    }
    function openSettings() {
        if (!enabled || !strategySettings || !controllers) return
        algorithmPopup.descriptor = strategySettings
        placePopup(algorithmPopup)
        algorithmPopup.open()
    }
    onStrategySettingsChanged: {
        const previous = algorithmPopup.descriptor
        if (algorithmPopup.visible && (!strategySettings || previous.id !== strategySettings.id
            || previous.generation !== strategySettings.generation || previous.source !== strategySettings.source)) algorithmPopup.close()
    }
    onPlaybackModeChanged: if (playbackMode !== "shuffle") algorithmPopup.close()
    onClicked: {
        menuViewport.contentY = 0
        modeMenu.currentIndex = Math.max(0, modes.indexOf(playbackMode))
        placePopup(modeMenu)
        modeMenu.open()
    }
    onEnabledChanged: if (!enabled) { modeMenu.close(); algorithmPopup.close() }
    onVisibleChanged: if (!visible) { modeMenu.close(); algorithmPopup.close() }
    Popup {
        id: algorithmPopup
        objectName: "shuffleSettingsPopup"
        property var descriptor: ({})
        parent: Overlay.overlay
        width: Math.min(420, parent.width - 16)
        implicitHeight: Math.min(460, parent.height - 16)
        padding: 8
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: { descriptor = ({}); button.forceActiveFocus() }
        background: Rectangle {
            objectName: "shortcutBlocker"
            color: Theme.bgRaised; radius: Theme.radiusMd; border.color: Theme.borderSubtle
        }
        contentItem: Item {
            ExtensionView {
                id: settingsView
                objectName: "shuffleSettingsView"
                anchors.fill: parent
                descriptor: algorithmPopup.visible ? algorithmPopup.descriptor : ({})
                controllers: button.controllers; translator: button.translator; hostWindow: button.hostWindow
            }
            Connections {
                target: settingsView.item
                ignoreUnknownSignals: true
                function onDismissRequested() { algorithmPopup.close() }
            }
            Label {
                anchors.fill: parent; anchors.margins: 12
                visible: settingsView.error !== ""
                text: settingsView.error; textFormat: Text.PlainText; wrapMode: Text.WordWrap; color: Theme.statusError
            }
        }
    }
    Popup {
        id: modeMenu
        objectName: "playbackModeMenu"
        property int currentIndex: 0
        parent: Overlay.overlay
        width: 220
        padding: 6
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: { contentItem.forceActiveFocus(); Qt.callLater(revealCurrent) }
        onClosed: button.forceActiveFocus()
        function choose(index) {
            if (!button.enabled || index < 0 || index >= button.entryCount) return
            close()
            if (index < button.modes.length) button.modeRequested(button.modes[index])
            else if (index < button.modes.length + button.shuffleStrategies.length)
                button.strategyRequested(button.shuffleStrategies[index - button.modes.length].id)
            else button.openSettings()
        }
        onCurrentIndexChanged: Qt.callLater(revealCurrent)
        function revealCurrent() {
            const row = currentIndex < button.modes.length ? modeRows.itemAt(currentIndex)
                : currentIndex < button.modes.length + button.shuffleStrategies.length
                    ? strategyRows.itemAt(currentIndex - button.modes.length) : configureRow
            if (!row) return
            if (row.y < menuViewport.contentY) menuViewport.contentY = row.y
            else if (row.y + row.height > menuViewport.contentY + menuViewport.height)
                menuViewport.contentY = row.y + row.height - menuViewport.height
            menuViewport.contentY = Math.max(0, Math.min(menuViewport.contentY,
                menuViewport.contentHeight - menuViewport.height))
        }
        background: Rectangle {
            objectName: "shortcutBlocker"
            color: Theme.bgRaised; radius: Theme.radiusMd; border.color: Theme.borderSubtle
        }
        contentItem: Flickable {
            id: menuViewport
            implicitHeight: Math.min(menuColumn.implicitHeight, Math.max(60, modeMenu.parent.height - 28))
            contentWidth: width
            contentHeight: menuColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            focus: true
            Keys.onDownPressed: modeMenu.currentIndex = (modeMenu.currentIndex + 1) % button.entryCount
            Keys.onUpPressed: modeMenu.currentIndex = (modeMenu.currentIndex + button.entryCount - 1) % button.entryCount
            Keys.onReturnPressed: modeMenu.choose(modeMenu.currentIndex)
            Keys.onEnterPressed: modeMenu.choose(modeMenu.currentIndex)
            Keys.onSpacePressed: modeMenu.choose(modeMenu.currentIndex)
            Column {
                id: menuColumn
                width: parent.width
                spacing: 2
                onImplicitHeightChanged: Qt.callLater(modeMenu.revealCurrent)
                Repeater {
                    id: modeRows
                    model: button.modes
                    delegate: TextButton {
                        required property string modelData
                        required property int index
                        readonly property bool selected: button.playbackMode === modelData
                        objectName: "playbackMode_" + modelData
                        width: parent.width
                        text: (selected ? "✓  " : "    ") + button.t("playback_mode_" + modelData)
                        subtle: true
                        subtleBg: modeMenu.currentIndex === index ? Theme.bgSelected : "transparent"
                        subtleBorder: "transparent"
                        subtleText: selected ? Theme.accentPrimary : Theme.textSecondary
                        Accessible.role: Accessible.MenuItem
                        Accessible.name: button.t("playback_mode_" + modelData)
                        Accessible.checkable: true
                        Accessible.checked: selected
                        onHoveredChanged: if (hovered) modeMenu.currentIndex = index
                        onClicked: modeMenu.choose(index)
                    }
                }
                Label {
                    visible: button.playbackMode === "shuffle"
                    width: parent.width
                    topPadding: 8; bottomPadding: 4; leftPadding: 8
                    text: button.t("shuffle_algorithm")
                    color: Theme.textSecondary
                    textFormat: Text.PlainText
                }
                Label {
                    objectName: "shuffleFallbackHint"
                    visible: button.playbackMode === "shuffle" && button.shuffleStrategy !== button.effectiveShuffleStrategy
                    width: parent.width
                    padding: 8
                    text: button.t("shuffle_algorithm_fallback")
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    color: Theme.textSecondary
                }
                Repeater {
                    id: strategyRows
                    model: button.playbackMode === "shuffle" ? button.shuffleStrategies : []
                    delegate: TextButton {
                        required property var modelData
                        required property int index
                        readonly property bool selected: button.effectiveShuffleStrategy === modelData.id
                        objectName: "shuffleStrategy_" + modelData.id
                        width: parent.width
                        text: (selected ? "✓  " : "    ") + button.strategyName(modelData)
                        subtle: true
                        subtleBg: modeMenu.currentIndex === index + button.modes.length ? Theme.bgSelected : "transparent"
                        subtleBorder: "transparent"
                        subtleText: selected ? Theme.accentPrimary : Theme.textSecondary
                        Accessible.role: Accessible.MenuItem
                        Accessible.name: button.strategyName(modelData)
                        Accessible.checkable: true
                        Accessible.checked: selected
                        onHoveredChanged: if (hovered) modeMenu.currentIndex = index + button.modes.length
                        onClicked: modeMenu.choose(index + button.modes.length)
                    }
                }
                TextButton {
                    id: configureRow
                    objectName: "configureShuffleStrategy"
                    visible: button.playbackMode === "shuffle" && button.strategySettings !== null
                    width: parent.width
                    text: button.t("shuffle_algorithm_configure")
                    subtle: true
                    subtleBg: modeMenu.currentIndex === button.entryCount - 1 ? Theme.bgSelected : "transparent"
                    Accessible.role: Accessible.MenuItem
                    Accessible.name: text
                    onHoveredChanged: if (hovered) modeMenu.currentIndex = button.entryCount - 1
                    onClicked: modeMenu.choose(button.entryCount - 1)
                }
            }
        }
    }
}
