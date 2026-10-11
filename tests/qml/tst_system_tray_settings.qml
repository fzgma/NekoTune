import QtQuick
import QtQuick.Controls
import QtTest
import "../../frontend/qt-qml/qml/components" as Components

Rectangle {
    width: 1000; height: 720; color: Components.Theme.bgCanvas
    QtObject {
        id: desktop
        property bool closeToTray: true
        property bool trayAvailable: true
    }
    Components.SettingsPanel {
        id: settings
        anchors.fill: parent
        client: null; translator: i18n; desktop: desktop
    }
    TestCase {
        name: "SystemTraySettings"
        when: windowShown
        function init() {
            desktop.closeToTray = true
            desktop.trayAvailable = true
            settings.desktop = desktop
            i18n.language = "zh"
        }
        function test_checkboxUsesSharedPreference() {
            const box = findChild(settings, "closeToTrayCheckBox")
            verify(box.visible)
            verify(box.checked)
            compare(box.text, "关闭窗口后收进系统托盘")
            mouseClick(box)
            compare(desktop.closeToTray, false)
            desktop.closeToTray = true
            compare(box.checked, true)
        }
        function test_unavailableNoticeAndLanguage() {
            const notice = findChild(settings, "trayUnavailableNotice")
            verify(!notice.visible)
            desktop.trayAvailable = false
            verify(notice.visible)
            verify(notice.text.indexOf("关闭窗口将退出") >= 0)
            i18n.language = "en"
            verify(notice.text.indexOf("Closing the window will quit") >= 0)
            const box = findChild(settings, "closeToTrayCheckBox")
            verify(box.text.indexOf("system tray") >= 0)
            verify(box.enabled) // Preference can be saved even without a tray.
            waitForRendering(settings)
            const screenshot = testFixtures.screenshotPath("tray-settings-en")
            if (screenshot) grabImage(settings).save(screenshot)
        }
        function test_optionalDesktopForStandaloneComponents() {
            settings.desktop = null
            verify(!findChild(settings, "closeToTrayCheckBox").visible)
            verify(!findChild(settings, "trayUnavailableNotice").visible)
        }
    }
}
