import QtQuick
import QtQuick.Controls

TextField {
    id: field
    implicitHeight: Math.max(Theme.controlMinHeight, contentHeight + topPadding + bottomPadding)
    leftPadding: Theme.spaceMd
    rightPadding: Theme.spaceMd
    color: Theme.textPrimary
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accentPrimary
    selectedTextColor: Theme.textOnAccent
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody
    selectByMouse: true
    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.bgSurface
        border.color: field.activeFocus ? Theme.accentPrimary : Theme.borderControl
        border.width: field.activeFocus ? 2 : 1
    }
}
