pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtTest
import "../../frontend/qt-qml/qml/components"

Item {
    id: scene
    width: 640
    height: 480
    QtObject {
        id: mockTheme
        property var background: ({source: "qrc:/artwork/default-cover.png"})
        property var uiSlots: ({})
    }
    QtObject {
        id: app
        property QtObject theme: mockTheme
    }
    QtObject {
        id: mockTranslator
        property string language: "en"
        function text(key, language) { return key }
    }
    Component {
        id: backgroundComponent
        ThemeBackground {
            width: 320; height: 180
            controllers: app
            translator: mockTranslator
            hostWindow: scene.Window.window
        }
    }
    TestCase {
        name: "ThemeBackground"
        when: windowShown
        function test_decodeSizeAndRelease() {
            const background = createTemporaryObject(backgroundComponent, scene)
            verify(background)
            const image = findChild(background, "themeStaticBackgroundImage")
            verify(image)
            tryCompare(image, "status", Image.Ready)
            compare(image.sourceSize, Qt.size(Math.ceil(320 * image.Screen.devicePixelRatio),
                                              Math.ceil(180 * image.Screen.devicePixelRatio)))
            compare(image.cache, false)
            background.width = 480
            tryCompare(image, "sourceSize", Qt.size(Math.ceil(480 * image.Screen.devicePixelRatio),
                                                   Math.ceil(180 * image.Screen.devicePixelRatio)))
            background.visible = false
            tryCompare(image, "status", Image.Null)
            compare(image.source.toString(), "")
            background.visible = true
            tryCompare(image, "status", Image.Ready)
            mockTheme.background = ({})
            tryCompare(image, "status", Image.Null)
            mockTheme.background = ({source: "qrc:/artwork/default-cover.png"})
        }
    }
}
