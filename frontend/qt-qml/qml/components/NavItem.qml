import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: item

    property string iconKind: ""
    property bool current: false
    property bool compact: false

    hoverEnabled: true
    implicitHeight: Theme.controlMinHeight
    leftPadding: Theme.spaceMd
    rightPadding: Theme.spaceMd
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    font.weight: current ? Font.DemiBold : Font.Normal

    Accessible.role: Accessible.Button
    Accessible.name: item.text
    Accessible.selected: item.current

    contentItem: RowLayout {
        spacing: Theme.spaceMd
        Glyph {
            visible: item.iconKind !== ""
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            kind: item.iconKind
            size: 20
            color: !item.enabled ? Theme.textDisabled : item.current ? Theme.accentPrimary : Theme.textMuted
        }
        Text {
            Layout.fillWidth: true
            text: item.text
            textFormat: Text.PlainText
            font: item.font
            color: !item.enabled ? Theme.textDisabled : item.current ? Theme.textPrimary : Theme.textSecondary
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    background: Rectangle {
        id: bg
        radius: Theme.radiusSm
        color: item.current ? Theme.bgSelected
               : item.down ? Theme.bgSelected
               : item.hovered ? Theme.bgHover
               : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.reducedMotion ? 0 : Theme.durationFast } }
        Rectangle {
            x: 0
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 20
            radius: 1.5
            color: Theme.accentPrimary
            visible: item.current
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: bg.radius + 4
            color: "transparent"
            border.color: Theme.accentPrimary
            border.width: 2
            visible: item.activeFocus
        }
    }
}
