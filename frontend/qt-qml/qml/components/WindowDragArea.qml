import QtQuick

// Native moves work on Wayland; coordinate fallback is confined to X11/Windows.
MouseArea {
    id: area
    required property var controller
    required property var hostWindow
    property int resizeEdges: 0
    cursorShape: resizeEdges ? Qt.SizeFDiagCursor : Qt.SizeAllCursor
    onPressed: {
        if (resizeEdges) controller.startResize(hostWindow, resizeEdges)
        else controller.startMove(hostWindow)
    }
    onPositionChanged: mouse => {
        if (!pressed) return
        const point = area.mapToGlobal(mouse.x, mouse.y)
        controller.updatePointer(hostWindow, point.x, point.y)
    }
    onReleased: controller.endPointer()
    onCanceled: controller.endPointer()
}
