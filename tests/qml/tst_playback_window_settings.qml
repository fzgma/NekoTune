import QtQuick
import QtTest
import "../../frontend/qt-qml/qml/components" as Components

Item {
    width: 800; height: 600
    QtObject {
        id: manager
        property bool miniMode: false
        property bool miniAlwaysOnTop: false
        property bool desktopLyricsVisible: false
        property bool desktopLyricsLocked: false
        property bool desktopLyricsSupported: true
        property bool canLockDesktopLyrics: true
        property bool wayland: false
        property string desktopLyricsUnavailableReason: "desktop_lyrics_wayland"
    }
    Components.PlaybackWindowSettings { id: panel; width: 700; manager: manager; translator: i18n }
    TestCase {
        name: "PlaybackWindowSettings"
        when: windowShown
        function init() {
            manager.miniMode = false
            manager.miniAlwaysOnTop = false
            manager.desktopLyricsVisible = false
            manager.desktopLyricsLocked = false
            manager.desktopLyricsSupported = true
            manager.canLockDesktopLyrics = true
            manager.wayland = false
            i18n.language = "zh"
        }
        function test_controlsShareWindowState() {
            mouseClick(findChild(panel, "settingsMiniModeButton"))
            verify(manager.miniMode)
            mouseClick(findChild(panel, "miniAlwaysOnTopCheckBox"))
            verify(manager.miniAlwaysOnTop)
            const lock = findChild(panel, "desktopLyricsLockCheckBox")
            verify(!lock.enabled)
            mouseClick(findChild(panel, "desktopLyricsCheckBox"))
            verify(manager.desktopLyricsVisible)
            verify(lock.enabled)
            mouseClick(lock)
            verify(manager.desktopLyricsLocked)
            manager.canLockDesktopLyrics = false
            verify(!lock.enabled)
        }
        function test_waylandNoticeAndLanguage() {
            manager.wayland = true
            manager.desktopLyricsSupported = false
            const checkbox = findChild(panel, "desktopLyricsCheckBox")
            verify(!checkbox.enabled)
            const notice = findChild(panel, "desktopLyricsPlatformNotice")
            verify(notice.visible)
            verify(notice.text.indexOf("合成器") >= 0)
            i18n.language = "en"
            verify(notice.text.indexOf("compositor") >= 0)
            const screenshot = testFixtures.screenshotPath("wayland-window-settings")
            if (screenshot) grabImage(panel).save(screenshot)
        }
    }
}
