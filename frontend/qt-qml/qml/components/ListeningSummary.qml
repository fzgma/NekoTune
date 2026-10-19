pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var history
    required property var translator
    required property var transport
    signal openHistory()
    spacing: Theme.spaceLg
    readonly property var stats: history ? history.stats : ({})
    readonly property var days: stats.days || []
    readonly property int leading: days.length ? (new Date(days[0].date + "T12:00:00").getDay() + 6) % 7 : 0
    function t(key) { return translator.text(key, translator.language) }
    function duration(ms) { return t("history_duration").replace("%1", Math.floor((ms || 0) / 3600000)).replace("%2", Math.floor((ms || 0) / 60000) % 60) }
    RowLayout {
        Layout.fillWidth: true
        Label { Layout.fillWidth: true; text: root.t("history_listening"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle; font.weight: Font.DemiBold }
        TextButton { objectName: "homeHistoryButton"; text: root.t("history"); subtle: true; onClicked: root.openHistory() }
    }
    Label {
        Layout.fillWidth: true
        visible: !root.transport.connected || !root.history || Boolean(root.history.statsError) || root.days.length === 0
        text: root.t(!root.transport.connected ? "home_offline" : root.history && root.history.statsError ? "home_load_failed" : "home_loading")
        color: Theme.textMuted
    }
    TextButton {
        visible: root.transport.connected && root.history && Boolean(root.history.statsError)
        text: root.t("home_retry"); subtle: true; onClicked: root.history.refresh()
    }
    GridLayout {
        Layout.fillWidth: true
        columns: root.width < 650 ? 1 : 2
        columnSpacing: 32; rowSpacing: 20
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.alignment: Qt.AlignTop
            spacing: 12
            Label { text: root.t("history_week") + " · " + root.duration(root.stats.week_ms); color: Theme.accentPrimary; font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold }
            Label { text: root.t("history_month"); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
            GridLayout {
                objectName: "homeListeningHeatmap"
                columns: 7; columnSpacing: 6; rowSpacing: 6
                Repeater {
                    model: root.t("history_weekdays").split(",")
                    Label { required property string modelData; Layout.preferredWidth: 28; text: modelData; horizontalAlignment: Text.AlignHCenter; color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
                }
                Repeater {
                    model: root.leading + root.days.length
                    delegate: Rectangle {
                        id: cell
                        required property int index
                        readonly property var day: index >= root.leading ? root.days[index-root.leading] : ({})
                        readonly property real minutes: (day.listened_ms || 0) / 60000
                        Layout.preferredWidth: 28; Layout.preferredHeight: 28
                        radius: Theme.radiusSm
                        visible: true
                        opacity: index < root.leading ? 0 : 1
                        color: minutes === 0 ? Theme.bgRaised : Qt.rgba(Theme.accentPrimary.r, Theme.accentPrimary.g, Theme.accentPrimary.b, Math.min(1, .3 + minutes / 120))
                        border.color: Theme.borderSubtle
                        activeFocusOnTab: index >= root.leading
                        Accessible.role: Accessible.StaticText
                        Accessible.name: String(day.date || "") + " · " + root.duration(day.listened_ms)
                        HoverHandler { id: hover }
                        ToolTip.visible: cell.index >= root.leading && (hover.hovered || cell.activeFocus)
                        ToolTip.text: cell.Accessible.name
                        border.width: activeFocus ? 2 : 1
                    }
                }
            }
            Label { text: root.days.length ? root.days[0].date + " — " + root.days[root.days.length - 1].date : ""; color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.alignment: Qt.AlignTop
            spacing: 10
            Label { text: root.t("history_favorites"); color: Theme.textPrimary; font.pixelSize: Theme.fontBody; font.weight: Font.DemiBold }
            Repeater {
                model: root.stats.top_songs || []
                delegate: RowLayout {
                    id: ranking
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    Label { text: ranking.index + 1; color: Theme.textMuted; font.pixelSize: Theme.fontCaption; Layout.preferredWidth: 20 }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 3
                        Label { Layout.fillWidth: true; text: ranking.modelData.title; textFormat: Text.PlainText; color: Theme.textPrimary; elide: Text.ElideRight; font.pixelSize: Theme.fontBody }
                        Label { Layout.fillWidth: true; text: ranking.modelData.artist || root.t("unknown_artist"); textFormat: Text.PlainText; color: Theme.textMuted; elide: Text.ElideRight; font.pixelSize: Theme.fontCaption }
                    }
                    Label { text: root.duration(ranking.modelData.listened_ms); color: Theme.textSecondary; font.pixelSize: Theme.fontCaption }
                }
            }
            Label {
                objectName: "homeHistoryEmpty"
                Layout.fillWidth: true
                visible: root.days.length > 0 && (root.stats.top_songs || []).length === 0
                text: root.t(root.stats.total_sessions > 0 ? "history_month_empty" : "history_first_use")
                color: Theme.textMuted; wrapMode: Text.WordWrap; font.pixelSize: Theme.fontBody
            }
        }
    }
}
