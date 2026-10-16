import QtQuick
import QtQuick.Controls

Button {
    id: button

    property string kind: "dot"
    property string tooltipText: ""
    property color fillColor: "transparent"
    property color hoverColor: Theme.bgHover
    property color pressedColor: Theme.bgSelected
    property color borderColor: "transparent"
    property color glyphColor: Theme.textMuted
    property color hoverGlyphColor: Theme.textPrimary
    property real iconSize: 18
    property real cornerRadius: Theme.radiusSm

    hoverEnabled: true
    implicitWidth: Theme.iconButtonSize
    implicitHeight: Theme.iconButtonSize

    ToolTip.visible: button.hovered && button.tooltipText.length > 0
    ToolTip.text: button.tooltipText
    ToolTip.delay: 400

    Accessible.role: Accessible.Button
    Accessible.name: button.tooltipText

    contentItem: Glyph {
        anchors.centerIn: parent
        width: button.iconSize
        height: button.iconSize
        kind: button.kind
        size: button.iconSize
        color: !button.enabled ? Theme.textDisabled
               : button.hovered ? button.hoverGlyphColor
               : button.glyphColor
    }

    background: Rectangle {
        id: bgRect
        radius: button.cornerRadius
        color: !button.enabled ? "transparent"
               : button.down ? button.pressedColor
               : button.hovered ? button.hoverColor
               : button.fillColor
        border.color: button.enabled ? button.borderColor : "transparent"
        border.width: 1

        // Keyboard focus ring (Section 7.1 & 11)
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: bgRect.radius + 4
            color: "transparent"
            border.color: Theme.accentPrimary
            border.width: 2
            visible: button.activeFocus
        }

        Behavior on color {
            ColorAnimation { duration: Theme.reducedMotion ? 0 : Theme.durationFast }
        }
    }
}
