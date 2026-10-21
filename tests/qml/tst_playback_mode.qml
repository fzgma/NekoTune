import QtQuick
import QtQuick.Controls
import QtTest
import "../../frontend/qt-qml/qml/components" as Components

Rectangle {
    id: scene
    width: 800
    height: 600
    color: "#0E0D14"
    QtObject {
        id: fakeExtensions
        property var settingsPages: []
    }
    QtObject {
        id: app
        property var extensions: fakeExtensions
    }
    QtObject {
        id: controller
        property string playbackMode: "sequential"
        property string requestedMode: ""
        property var shuffleStrategies: [{id: "shuffle_bag", name: "Shuffle bag"}]
        property string shuffleStrategy: "shuffle_bag"
        property string effectiveShuffleStrategy: "shuffle_bag"
        property string requestedStrategy: ""
        signal requestSucceeded(string method)
        signal requestFailed(string method, string message)
        function setPlaybackMode(mode) { requestedMode = mode }
        function setShuffleStrategy(id) { requestedStrategy = id }
    }
    Components.QueuePanel {
        id: fullPanel
        x: 20; y: 20; width: 360; height: 560
        playbackController: controller
        appControllers: app
        playbackMode: controller.playbackMode
    }
    Components.QueuePanel {
        id: drawer
        x: 410; y: 20; width: 360; height: 560
        compact: true
        playbackController: controller
        appControllers: app
        playbackMode: controller.playbackMode
    }
    TestCase {
        name: "PlaybackMode"
        when: windowShown
        function button(panel) { return findChild(panel, "playbackModeButton") }
        function menu(panel) { return findChild(button(panel), "playbackModeMenu") }
        function init() {
            controller.playbackMode = "sequential"
            controller.requestedMode = ""
            controller.shuffleStrategies = [{id: "shuffle_bag", name: "Shuffle bag"}]
            controller.shuffleStrategy = "shuffle_bag"
            controller.effectiveShuffleStrategy = "shuffle_bag"
            controller.requestedStrategy = ""
            fakeExtensions.settingsPages = []
            fullPanel.connected = true
            drawer.connected = true
            waitForRendering(scene)
        }
        function cleanup() {
            menu(fullPanel).close()
            menu(drawer).close()
            tryCompare(menu(fullPanel), "visible", false)
            tryCompare(menu(drawer), "visible", false)
            findChild(button(fullPanel), "shuffleSettingsPopup").close()
            findChild(button(drawer), "shuffleSettingsPopup").close()
        }
        // An empty queue can still choose the mode for future playback.
        // Selecting a menu item submits a request without optimistically changing the icon.
        // Both panel and drawer must follow the same confirmed backend mode.
        function test_menuAndAuthoritativeSelection() {
            const anchor = button(fullPanel)
            const popup = menu(fullPanel)
            verify(anchor.enabled) // Empty queue can choose its future mode.
            mouseClick(anchor)
            tryCompare(popup, "opened", true)
            const modes = ["sequential", "repeat_one", "shuffle", "repeat_all"]
            for (let i = 0; i < modes.length; ++i) {
                const item = findChild(popup.contentItem, "playbackMode_" + modes[i])
                verify(item)
                compare(item.selected, i === 0)
            }
            waitForRendering(popup.contentItem)
            const screenshot = testFixtures.screenshotPath("playback-mode-click")
            if (screenshot) grabImage(popup.parent.parent).save(screenshot)
            mouseClick(findChild(popup.contentItem, "playbackMode_shuffle"))
            compare(controller.requestedMode, "shuffle")
            compare(anchor.kind, "sequential") // Wait for the backend to confirm.
            controller.playbackMode = "shuffle"
            tryCompare(anchor, "kind", "shuffle")
            tryCompare(button(drawer), "kind", "shuffle")
            verify(anchor.tooltipText.indexOf(i18n.text("playback_mode_shuffle", i18n.language)) >= 0)
            mouseClick(button(drawer))
            tryCompare(menu(drawer), "opened", true)
            verify(findChild(menu(drawer).contentItem, "playbackMode_shuffle").selected)
        }
        // The popup owns directional keys and Enter while it is open.
        // Escape cancels navigation without submitting the currently highlighted item.
        // Opening again must restore usable keyboard focus and wrapped item navigation.
        function test_keyboardAndEscape() {
            const anchor = button(fullPanel)
            const popup = menu(fullPanel)
            anchor.forceActiveFocus()
            keyClick(Qt.Key_Space)
            tryCompare(popup, "opened", true)
            keyClick(Qt.Key_Down)
            compare(popup.currentIndex, 1)
            keyClick(Qt.Key_Return)
            compare(controller.requestedMode, "repeat_one")
            tryCompare(popup, "visible", false)
            keyClick(Qt.Key_Space)
            tryCompare(popup, "opened", true)
            keyClick(Qt.Key_Up)
            compare(popup.currentIndex, 3)
            keyClick(Qt.Key_Escape)
            tryCompare(popup, "visible", false)
            compare(controller.requestedMode, "repeat_one")
        }
        // Disconnect closes the popup and disables further mode requests.
        // A persistence error must retain the previous authoritative mode.
        // Only the matching successful operation clears the displayed failure.
        function test_disconnectAndFailure() {
            const anchor = button(drawer)
            const popup = menu(drawer)
            mouseClick(anchor)
            tryCompare(popup, "opened", true)
            drawer.connected = false
            tryCompare(popup, "visible", false)
            verify(!anchor.enabled)
            controller.requestFailed("player.set_playback_mode", "Unable to save playback mode")
            compare(drawer.playbackMode, "sequential")
            compare(drawer.operationError, "Unable to save playback mode")
            controller.requestSucceeded("player.set_playback_mode")
            compare(drawer.operationError, "")
        }
        // The drawer's smaller geometry must still contain the entire mode popup.
        // Each backend mode updates the button icon before its menu is opened.
        // The bounds include the popup's safe inset from the window edges.
        function test_menuFitsDrawerAndIconsUpdate() {
            for (const mode of ["sequential", "repeat_one", "shuffle", "repeat_all"]) {
                controller.playbackMode = mode
                const anchor = button(drawer)
                tryCompare(anchor, "kind", mode)
                mouseClick(anchor)
                const popup = menu(drawer)
                tryCompare(popup, "opened", true)
                verify(popup.x >= 8 && popup.y >= 8)
                verify(popup.x + popup.width <= popup.parent.width - 8)
                verify(popup.y + popup.height <= popup.parent.height - 8)
                const path = testFixtures.screenshotPath("playback-mode-" + mode)
                if (path) {
                    waitForRendering(popup.contentItem)
                    grabImage(popup.parent.parent).save(path)
                }
                popup.close()
                tryCompare(popup, "visible", false)
            }
        }
        function test_shuffleSelectionAndFailure() {
            controller.playbackMode = "shuffle"
            controller.shuffleStrategies = [{id: "shuffle_bag", name: "Shuffle bag"}, {id: "test/artist", name: "Artist-aware"}]
            const popup = menu(fullPanel)
            mouseClick(button(fullPanel))
            tryCompare(popup, "opened", true)
            const algorithm = findChild(popup.contentItem, "shuffleStrategy_test/artist")
            verify(algorithm)
            verify(!algorithm.selected)
            waitForRendering(popup.contentItem)
            mouseClick(algorithm)
            compare(controller.requestedStrategy, "test/artist")
            compare(controller.requestedMode, "")
            compare(controller.shuffleStrategy, "shuffle_bag")
            controller.requestFailed("player.set_shuffle_strategy", "Unable to save shuffle strategy")
            compare(fullPanel.operationError, "Unable to save shuffle strategy")
            controller.shuffleStrategy = "test/artist"
            controller.effectiveShuffleStrategy = "test/artist"
            controller.requestSucceeded("player.set_shuffle_strategy")
            compare(fullPanel.operationError, "")
            mouseClick(button(drawer))
            tryCompare(menu(drawer), "opened", true)
            verify(findChild(menu(drawer).contentItem, "shuffleStrategy_test/artist").selected)
        }
        function test_shuffleFallbackAndDynamicRemoval() {
            controller.playbackMode = "shuffle"
            controller.shuffleStrategy = "test/artist"
            controller.effectiveShuffleStrategy = "test/artist"
            controller.shuffleStrategies = [{id: "shuffle_bag", name: "Shuffle bag"}, {id: "test/artist", name: "Artist-aware"}]
            mouseClick(button(drawer))
            const popup = menu(drawer)
            tryCompare(popup, "opened", true)
            popup.currentIndex = 5
            controller.shuffleStrategies = [{id: "shuffle_bag", name: "Shuffle bag"}]
            controller.effectiveShuffleStrategy = "shuffle_bag"
            tryCompare(popup, "currentIndex", 4)
            verify(findChild(popup.contentItem, "shuffleFallbackHint").visible)
            verify(findChild(popup.contentItem, "shuffleStrategy_shuffle_bag").selected)
            keyClick(Qt.Key_Return)
            compare(controller.requestedStrategy, "shuffle_bag")
        }
        function test_shuffleKeyboardAndScrolling() {
            controller.playbackMode = "shuffle"
            const strategies = [{id: "shuffle_bag", name: "Shuffle bag"}]
            for (let i = 0; i < 20; ++i) strategies.push({id: "test/" + i, name: "Algorithm " + i})
            controller.shuffleStrategies = strategies
            button(fullPanel).forceActiveFocus()
            keyClick(Qt.Key_Space)
            const popup = menu(fullPanel)
            tryCompare(popup, "opened", true)
            verify(popup.y + popup.height <= popup.parent.height - 8)
            keyClick(Qt.Key_Up) // shuffle -> repeat_one
            keyClick(Qt.Key_Up) // sequential
            keyClick(Qt.Key_Up) // wrap to final algorithm
            compare(popup.currentIndex, 24)
            tryVerify(() => popup.contentItem.contentY > 0)
            keyClick(Qt.Key_Return)
            compare(controller.requestedStrategy, "test/19")
            compare(controller.requestedMode, "")
        }
        function test_settingsEntryAndKeyboardPopup() {
            controller.playbackMode = "shuffle"
            controller.shuffleStrategies = [{id: "test/artist", name: "Artist-aware"}]
            controller.effectiveShuffleStrategy = "test/artist"
            fakeExtensions.settingsPages = [{id: "test/preferences", extensionId: "test", shuffleStrategy: "artist", source: "", generation: 1}]
            const anchor = button(drawer), popup = menu(drawer)
            mouseClick(anchor)
            tryCompare(popup, "opened", true)
            verify(findChild(popup.contentItem, "configureShuffleStrategy").visible)
            popup.currentIndex = anchor.entryCount - 1
            keyClick(Qt.Key_Return)
            const settings = findChild(anchor, "shuffleSettingsPopup")
            tryCompare(settings, "opened", true)
            verify(settings.x >= 8 && settings.y >= 8)
            verify(settings.x + settings.width <= settings.parent.width - 8)
            verify(settings.y + settings.height <= settings.parent.height - 8)
            keyClick(Qt.Key_Escape)
            tryCompare(settings, "visible", false)
            verify(anchor.activeFocus)
            anchor.openSettings()
            tryCompare(settings, "opened", true)
            fakeExtensions.settingsPages = [{id: "test/preferences", extensionId: "test", shuffleStrategy: "artist", source: "", generation: 2}]
            tryCompare(settings, "visible", false)
            anchor.openSettings()
            tryCompare(settings, "opened", true)
            fakeExtensions.settingsPages = []
            tryCompare(settings, "visible", false)
            compare(anchor.entryCount, 5)
        }
    }
}
