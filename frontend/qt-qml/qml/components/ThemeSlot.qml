import QtQuick
import QtQuick.Controls
import NekoTune 1.0

Item {
    id: root
    property var themeDescriptor: ({})
    property var fallbackDescriptor: ({})
    required property var controllers
    required property var translator
    required property var hostWindow
    property QtObject wallpaperContext: null
    property bool showErrors: true
    property bool active: true
    property bool themeActive: active
    readonly property bool ready: primary.ready || fallback.ready
    readonly property bool resolved: ready || ((!themeActive || !themeDescriptor.source || primary.error !== "")
        && (!active || !fallbackDescriptor.source || fallback.error !== ""))
    readonly property var item: primary.ready ? primary.item : fallback.item
    readonly property string error: [primary.error, fallback.error].filter(value => value !== "").join("\n")
    function invokeAction(method: string): bool {
        return primary.ready ? primary.invokeAction(method) : fallback.invokeAction(method)
    }
    ExtensionView {
        id: primary; objectName: root.objectName + "Theme"; anchors.fill: parent
        descriptor: root.themeActive ? root.themeDescriptor : ({})
        controllers: root.controllers; translator: root.translator; hostWindow: root.hostWindow
        wallpaperContext: root.wallpaperContext
        visible: ready
    }
    ExtensionView {
        id: fallback; objectName: root.objectName; anchors.fill: parent
        descriptor: root.active && (!root.themeDescriptor.source || primary.error !== "") ? root.fallbackDescriptor : ({})
        controllers: root.controllers; translator: root.translator; hostWindow: root.hostWindow
        wallpaperContext: root.wallpaperContext
        visible: ready && !primary.ready
    }
    Rectangle {
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: errorLabel.implicitHeight + Theme.spaceMd * 2
        visible: root.showErrors && root.error !== ""
        color: Theme.statusErrorBg; z: 100
        Label {
            id: errorLabel
            anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; anchors.margins: Theme.spaceMd
            text: root.error; color: Theme.statusError; wrapMode: Text.WordWrap; textFormat: Text.PlainText
        }
    }
}
