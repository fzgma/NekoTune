import QtQuick
import "../components"
Item {
    required property var shell
    required property var controllers
    required property var translator
    required property var transport
    ExtensionsPanel { anchors.fill: parent; anchors.margins: 24; client: controllers.extensions; themeController: controllers.theme || null; onAppearanceRequested: shell.navigate("appearance"); translator: parent.translator }
}
