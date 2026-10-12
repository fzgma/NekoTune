import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components"
ApplicationWindow {
    id: window
    width: 960; height: 720; minimumWidth: 760; minimumHeight: 580
    title: "NekoTune · " + i18n.text("extensions", i18n.language)
    color: Theme.bgCanvas
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    palette.window: Theme.bgRaised
    palette.windowText: Theme.textPrimary
    palette.text: Theme.textPrimary
    palette.buttonText: Theme.textPrimary
    palette.base: Theme.bgSurface
    palette.highlight: Theme.accentPrimary
    palette.highlightedText: Theme.textOnAccent
    property bool appearance: false
    ColumnLayout {
        anchors.fill: parent; anchors.margins: Theme.spaceXl; spacing: Theme.spaceMd
        RowLayout {
            TextButton { text: i18n.text("extensions", i18n.language); subtle: window.appearance; onClicked: window.appearance = false }
            TextButton { text: i18n.text("appearance", i18n.language); subtle: !window.appearance; onClicked: window.appearance = true }
        }
        ExtensionsPanel {
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: !window.appearance
            client: controllers.extensions; themeController: controllers.theme || null; translator: i18n
            onAppearanceRequested: window.appearance = true
        }
        Loader {
            Layout.fillWidth: true; Layout.fillHeight: true
            active: window.appearance && Boolean(controllers.theme)
            visible: active
            sourceComponent: AppearancePanel { controller: controllers.theme; translator: i18n }
        }
    }
}
