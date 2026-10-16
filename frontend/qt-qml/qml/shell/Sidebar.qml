import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: sidebar
    required property var shell
    required property var controllers
    required property var transport
    required property var translator
    required property var pages
    property int currentPlaylist: 0
    signal playlistRequested(int id)
    signal newPlaylistRequested()
    Layout.preferredWidth: shell.width < 1200 ? Theme.sidebarWidthCompact : Theme.sidebarWidthWide
    Layout.fillHeight: true
    color: Theme.bgSidebar
    function t(key) { return translator.text(key, translator.language) }
    function pageLabel(page) {
        return page.extensionId && controllers.extensions ? controllers.extensions.label(page.title || page.id, translator.language) : t(page.title)
    }
    function pageIcon(page) {
        const icons = {home: "home", library: "music", music_sources: "cloud", extensions: "extension", settings: "settings"}
        return icons[page.id] || (page.extensionId ? "cloud" : "music")
    }
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.borderSubtle }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceLg
        spacing: Theme.spaceLg
        RowLayout {
            Layout.topMargin: Theme.spaceSm; Layout.bottomMargin: Theme.spaceSm
            spacing: Theme.spaceMd
            Item {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusMd
                    color: Theme.accentPrimary
                }
                Canvas {
                    anchors.centerIn: parent
                    width: 32; height: 32
                    property color ink: Theme.textOnAccent
                    onInkChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.strokeStyle = ink
                        ctx.fillStyle = ink
                        ctx.lineWidth = 1.9
                        ctx.lineCap = "round"
                        ctx.lineJoin = "round"
                        const w = width, h = height
                        // Cat ears: two rounded triangles rising above the note head.
                        ctx.beginPath()
                        ctx.moveTo(w * 0.27, h * 0.44)
                        ctx.lineTo(w * 0.30, h * 0.20)
                        ctx.lineTo(w * 0.48, h * 0.37)
                        ctx.closePath()
                        ctx.fill()
                        ctx.beginPath()
                        ctx.moveTo(w * 0.73, h * 0.44)
                        ctx.lineTo(w * 0.70, h * 0.20)
                        ctx.lineTo(w * 0.52, h * 0.37)
                        ctx.closePath()
                        ctx.fill()
                        // Music note body: stem, flag and head.
                        ctx.beginPath()
                        ctx.moveTo(w * 0.56, h * 0.66)
                        ctx.lineTo(w * 0.56, h * 0.40)
                        ctx.lineTo(w * 0.70, h * 0.36)
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.ellipse(w * 0.40, h * 0.66, w * 0.13, w * 0.10, 0, 0, Math.PI * 2)
                        ctx.fill()
                    }
                }
            }
            Label {
                text: "NekoTune"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontDialogTitle
                font.weight: Font.DemiBold
                font.letterSpacing: 0.6
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: Theme.spaceXs
            Repeater {
                model: sidebar.pages.filter(page => page.group === "primary")
                delegate: NavItem {
                    required property var modelData
                    Layout.fillWidth: true
                    objectName: "nav_" + modelData.id
                    text: sidebar.pageLabel(modelData)
                    iconKind: sidebar.pageIcon(modelData)
                    current: sidebar.shell.viewMode === modelData.id
                    onClicked: sidebar.shell.navigate(modelData.id)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceMd
            Label { Layout.fillWidth: true; text: sidebar.t("playlists"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption; font.weight: Font.DemiBold }
            IconButton { kind: "plus"; implicitWidth: 32; implicitHeight: 32; tooltipText: sidebar.t("new_playlist"); enabled: sidebar.transport.connected; onClicked: sidebar.newPlaylistRequested() }
        }
        ListView {
            id: playlists
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: Theme.spaceXs
            model: sidebar.controllers.playlists.model
            ScrollBar.vertical: ScrollBar {}
            onCountChanged: Qt.callLater(() => {
                const index = sidebar.controllers.playlists.model.items.findIndex(list => Number(list.id) === sidebar.currentPlaylist)
                if (index >= 0) playlists.positionViewAtIndex(index, ListView.Contain)
            })
            delegate: NavItem {
                required property var modelData
                width: playlists.width
                objectName: "nav_playlist_" + modelData.id
                text: modelData.name
                iconKind: "playlist"
                current: sidebar.shell.viewMode === "queue" && sidebar.currentPlaylist === Number(modelData.id)
                onClicked: sidebar.playlistRequested(Number(modelData.id))
                ToolTip.visible: hovered || activeFocus
                ToolTip.text: modelData.name
            }
            Label { anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; anchors.topMargin: Theme.spaceSm; visible: playlists.count === 0; text: sidebar.t("no_playlist_short"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: Theme.spaceXs
            Repeater {
                model: sidebar.pages.filter(page => page.group === "utility" || page.debug && sidebar.shell.debugEnabled)
                delegate: NavItem {
                    required property var modelData
                    Layout.fillWidth: true
                    objectName: modelData.debug ? "lyricsDebugNavigation" : "nav_" + modelData.id
                    text: sidebar.pageLabel(modelData)
                    iconKind: sidebar.pageIcon(modelData)
                    current: sidebar.shell.viewMode === modelData.id
                    onClicked: sidebar.shell.navigate(modelData.id)
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: Theme.spaceSm; Layout.leftMargin: Theme.spaceMd; spacing: Theme.spaceSm
                Rectangle { Layout.preferredWidth: 8; Layout.preferredHeight: 8; radius: 4; color: sidebar.transport.connected ? Theme.statusSuccess : Theme.statusError }
                Label { text: sidebar.t(sidebar.transport.connected ? "connected" : "offline"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
            }
        }
    }
}
