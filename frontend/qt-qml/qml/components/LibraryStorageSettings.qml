import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
Rectangle {
    id: root
    required property var controller
    required property var translator
    property bool connected: true
    property var preview: ({})
    property string rollbackId: ""
    readonly property var status: controller ? controller.status : ({})
    readonly property var task: controller ? controller.task : ({})
    readonly property bool busy: controller ? controller.busy : false
    function t(key) { return translator.text(key, translator.language) }
    function resultSummary() {
        const result = task.result || ({})
        if (result.exported !== undefined)
            return t("storage_export_result").replace("%1", result.exported).replace("%2", result.skipped).replace("%3", result.failed)
        if (result.verified !== undefined)
            return t("storage_verify_result").replace("%1", result.verified).replace("%2", result.failed)
        if (result.processed !== undefined)
            return t("storage_migrate_result").replace("%1", result.processed).replace("%2", result.failed)
        if (result.restored) return t("storage_restored")
        if (result.songs !== undefined)
            return t("storage_backup_result").replace("%1", result.songs).replace("%2", (result.missing || []).length)
        return ""
    }
    function restoreSummary() {
        const missing = preview.missing || []
        let summary = t("storage_restore_confirm")
            .replace("%1", preview.songs || 0).replace("%2", missing.length)
        summary += "\n\n" + t("storage_restore_details")
            .replace("%1", preview.version || 1)
            .replace("%2", ((preview.bytes || 0) / 1048576).toFixed(1))
        if (missing.length)
            summary += "\n" + t("storage_missing_objects") + "\n" + missing.slice(0, 3).join("\n")
                + (missing.length > 3 ? "\n…" : "")
        return summary
    }
    implicitHeight: content.implicitHeight + 36
    radius: Theme.radiusMd; color: Theme.bgSurface; border.color: Theme.borderSubtle
    ColumnLayout {
        id: content; anchors.fill: parent; anchors.margins: 18; spacing: Theme.spaceMd
        Label { text: root.t("storage_title"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
        Label { Layout.fillWidth: true; text: root.t("storage_hint"); color: Theme.textSecondary; wrapMode: Text.WordWrap }
        Label { Layout.fillWidth: true; text: root.status.root || ""; color: Theme.textMuted; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText }
        Label { Layout.fillWidth: true; text: root.t("storage_counts").replace("%1", root.status.songs || 0).replace("%2", root.status.pending_migration || 0).replace("%3", root.status.missing || 0); color: Theme.textSecondary; wrapMode: Text.WordWrap }
        Flow {
            Layout.fillWidth: true; spacing: Theme.spaceSm
            TextButton { objectName: "storageMigrate"; text: root.t("storage_migrate"); enabled: root.connected && !root.busy; onClicked: root.controller.migrate() }
            TextButton { objectName: "storageVerify"; text: root.t("storage_verify"); enabled: root.connected && !root.busy; onClicked: root.controller.verify() }
            TextButton { objectName: "storageBackup"; text: root.t("storage_backup"); enabled: root.connected && !root.busy; onClicked: backupFolder.open() }
            TextButton { objectName: "storageRestore"; text: root.t("storage_restore"); enabled: root.connected && !root.busy; onClicked: restoreFolder.open() }
            TextButton { text: root.t("lyrics_refresh"); subtle: true; enabled: root.connected; onClicked: root.controller.refresh() }
        }
        ProgressBar { Layout.fillWidth: true; visible: root.busy; indeterminate: !root.task.total_files; from: 0; to: root.task.total_files || 1; value: root.task.files || 0 }
        Label { Layout.fillWidth: true; visible: root.busy; text: root.t("storage_phase_" + (root.task.phase || "preparing")); color: Theme.textSecondary; wrapMode: Text.WordWrap }
        Label { Layout.fillWidth: true; visible: Boolean(root.task.task); text: root.t("storage_state_" + (root.task.state || "running")) + " · " + (root.task.files || 0) + " · " + Math.round((root.task.bytes || 0) / 1048576) + " MiB"; color: Theme.textSecondary; wrapMode: Text.WordWrap }
        TextButton { objectName: "storageCancel"; visible: root.busy; text: root.t("cancel"); enabled: root.connected && root.task.state !== "switching" && root.task.cancellable !== false; onClicked: root.controller.cancel() }
        Label { Layout.fillWidth: true; visible: Boolean(root.controller && root.controller.message); text: root.controller ? root.controller.message : ""; color: Theme.statusError; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        Label { Layout.fillWidth: true; visible: Boolean(root.task.result && root.task.result.path); text: root.task.result ? root.task.result.path || "" : ""; color: Theme.textSecondary; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText }
        Label { Layout.fillWidth: true; visible: text.length > 0; text: root.resultSummary(); color: Theme.textSecondary; wrapMode: Text.WordWrap }
        Repeater {
            model: root.task.result && root.task.result.errors ? root.task.result.errors : []
            Label { required property var modelData; Layout.fillWidth: true; text: String(modelData.song_id || "") + " " + modelData.message; color: Theme.statusError; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        }
        Repeater {
            model: root.status.migration_errors || []
            Label { required property var modelData; Layout.fillWidth: true; text: String(modelData.song_id) + " · " + modelData.message; color: Theme.textMuted; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
        }
        Repeater {
            model: root.status.rollback_copies || []
            RowLayout {
                required property string modelData
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: root.t("storage_rollback") + " " + parent.modelData; color: Theme.textSecondary; elide: Text.ElideMiddle; textFormat: Text.PlainText }
                TextButton { text: root.t("delete"); enabled: root.connected && !root.busy; onClicked: { root.rollbackId = parent.modelData; removeDialog.open() } }
            }
        }
    }
    FolderDialog { id: backupFolder; title: root.t("storage_backup"); onAccepted: root.controller.backup(selectedFolder.toString()) }
    FolderDialog { id: restoreFolder; title: root.t("storage_restore"); onAccepted: root.controller.previewRestore(selectedFolder.toString()) }
    Connections {
        target: root.controller
        function onRestorePreviewReady(preview) { root.preview = preview; restoreDialog.open() }
    }
    ConfirmationDialog {
        id: restoreDialog
        objectName: "storageRestoreConfirmation"
        title: root.t("storage_restore")
        confirmText: root.t("storage_restore")
        message: root.restoreSummary()
        onConfirmed: { root.controller.restore(); completed() }
    }
    ConfirmationDialog {
        id: removeDialog
        title: root.t("storage_rollback")
        message: root.t("storage_remove_rollback")
        onConfirmed: { root.controller.removeRollback(root.rollbackId); completed() }
    }
    Component.onCompleted: if (controller && connected) controller.refresh()
}
