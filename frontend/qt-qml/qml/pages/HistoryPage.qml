pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: page
    required property var shell
    required property var controllers
    required property var translator
    required property var transport
    readonly property var history: controllers.history
    function t(key) { return translator.text(key, translator.language) }
    function duration(ms) { return t("history_duration_seconds").replace("%1", Math.floor(ms / 60000)).replace("%2", Math.floor(ms / 1000) % 60) }
    function applyFilters() { history.filter(period.currentValue || 30, search.text, complete.checked) }
    Component.onCompleted: { applyFilters(); history.refresh() }
    Timer { id: searchTimer; interval: 250; onTriggered: page.applyFilters() }
    Connections {
        target: page.history
        function onRequestSucceeded(method) { if (method === "history.clear") clearDialog.completed() }
        function onRequestFailed(method, message) { if (method === "history.clear") clearDialog.failed(message) }
    }
    ConfirmationDialog {
        id: clearDialog
        objectName: "historyClearDialog"
        title: page.t("history_clear")
        message: page.t("history_clear_message")
        onConfirmed: page.history.clearHistory()
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 32; spacing: 20
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: page.t("history"); color: Theme.textPrimary; font.pixelSize: Theme.fontTitle; font.weight: Font.DemiBold }
            TextButton { objectName: "historyRefresh"; text: page.t("home_retry"); subtle: true; enabled: page.transport.connected && !page.history.loading; onClicked: page.history.refresh() }
            TextButton { objectName: "historyClear"; text: page.t("history_clear"); subtle: true; enabled: page.transport.connected; onClicked: clearDialog.open() }
        }
        RowLayout {
            Layout.fillWidth: true
            ChoiceField {
                id: period; objectName: "historyPeriod"; Layout.preferredWidth: 160
                textRole: "label"; valueRole: "value"; currentIndex: 2
                model: [{value: 1, label: page.t("history_today")}, {value: 7, label: page.t("history_7_days")}, {value: 30, label: page.t("history_30_days")}]
                onActivated: page.applyFilters()
                Accessible.name: page.t("history_period")
            }
            InputField { id: search; objectName: "historySearch"; Layout.fillWidth: true; placeholderText: page.t("history_search"); onTextEdited: searchTimer.restart(); Accessible.name: placeholderText }
            CheckBox { id: complete; objectName: "historyCompleted"; text: page.t("history_complete_only"); palette.windowText: Theme.textPrimary; onToggled: page.applyFilters() }
        }
        Label {
            Layout.fillWidth: true
            text: page.t("history_complete_hint")
            color: Theme.textMuted; font.pixelSize: Theme.fontCaption; wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            visible: !page.transport.connected || page.history.error.length > 0
            text: !page.transport.connected ? page.t("home_offline") : page.history.error
            textFormat: Text.PlainText; color: Theme.statusWarning; wrapMode: Text.WordWrap
        }
        ListView {
            id: list; objectName: "historyList"
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
            model: page.history.records
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: row
                required property var modelData
                width: list.width; height: 76; radius: Theme.radiusSm; color: Theme.bgSurface
                RowLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 20
                    Label { Layout.preferredWidth: 150; text: new Date(row.modelData.started_at).toLocaleString(Qt.locale(), "MM-dd hh:mm:ss"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 5
                        Label { Layout.fillWidth: true; text: row.modelData.title; textFormat: Text.PlainText; color: Theme.textPrimary; elide: Text.ElideRight; font.pixelSize: Theme.fontBody }
                        Label { Layout.fillWidth: true; text: row.modelData.artist || page.t("unknown_artist"); textFormat: Text.PlainText; color: Theme.textMuted; elide: Text.ElideRight; font.pixelSize: Theme.fontCaption }
                    }
                    Label { text: page.duration(row.modelData.listened_ms); color: Theme.textSecondary; font.pixelSize: Theme.fontBodySecondary }
                    Label { Layout.preferredWidth: 100; horizontalAlignment: Text.AlignRight; text: page.t(row.modelData.completed ? "history_completed" : "history_reason_" + (row.modelData.end_reason || "active")); color: row.modelData.completed ? Theme.statusSuccess : Theme.textMuted; font.pixelSize: Theme.fontCaption }
                }
            }
            Label {
                anchors.centerIn: parent; width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                visible: list.count === 0 && page.transport.connected && page.history.error.length === 0
                text: page.t(page.history.loading ? "home_loading" : "history_empty")
                color: Theme.textMuted
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: page.t("history_total").replace("%1", page.history.total); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
            TextButton { objectName: "historyPrevious"; text: page.t("history_previous"); subtle: true; enabled: page.transport.connected && !page.history.loading && page.history.offset > 0; onClicked: { page.history.goToPage(page.history.offset - 50); list.positionViewAtBeginning() } }
            Label { text: Math.floor(page.history.offset / 50) + 1; color: Theme.textSecondary }
            TextButton { objectName: "historyNext"; text: page.t("history_next"); subtle: true; enabled: page.transport.connected && !page.history.loading && page.history.offset + 50 < page.history.total; onClicked: { page.history.goToPage(page.history.offset + 50); list.positionViewAtBeginning() } }
        }
    }
}
