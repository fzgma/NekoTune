pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: root
    required property var controller
    required property var translator
    property string pendingId: ""
    property string pendingTarget: "theme"
    property string pendingPath: ""
    property string pendingUpdateId: ""
    property string updateId: ""
    property string exportId: ""
    function t(key) { return translator.text(key, translator.language) }
    function label(value) {
        return typeof value === "string" ? value : value ? value[translator.language] || value.en || value.zh || "" : ""
    }
    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: scroll.availableWidth
            spacing: Theme.spaceLg
            Label { text: root.t("appearance"); color: Theme.textPrimary; font.pixelSize: Theme.fontTitle; font.bold: true }
            Label { Layout.fillWidth: true; text: root.t("theme_description"); color: Theme.textSecondary; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Label { text: root.t("theme_mode"); color: Theme.textSecondary }
                ChoiceField {
                    objectName: "themeModePicker"
                    Layout.fillWidth: true
                    enabled: !root.controller.safeMode
                    model: [{id:"light", label:root.t("theme_light")}, {id:"dark", label:root.t("theme_dark")}, {id:"system", label:root.t("theme_system")}]
                    textRole: "label"; valueRole: "id"
                    currentIndex: model.findIndex(item => item.id === root.controller.mode)
                    onActivated: root.controller.setMode(currentValue)
                }
            }
            CheckBox {
                objectName: "reducedMotionCheck"
                text: root.t("theme_reduced_motion")
                checked: root.controller.reducedMotion
                onToggled: root.controller.setReducedMotion(checked)
            }
            Label {
                Layout.fillWidth: true
                text: root.t("theme_reduced_motion_hint")
                color: Theme.textMuted; wrapMode: Text.WordWrap
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spaceSm
                TextButton { text: root.t("theme_import"); enabled: root.controller.connected && !root.controller.busy; onClicked: importPicker.open() }
                TextButton { objectName: "resetThemeButton"; text: root.t("theme_reset"); subtle: true; enabled: root.controller.connected && !root.controller.busy; onClicked: root.controller.reset() }
            }
            Label {
                Layout.fillWidth: true
                text: root.t("theme_update_hint")
                color: Theme.textMuted; wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: root.controller.error ? root.t(root.controller.error) : root.controller.notice ? root.t(root.controller.notice) : ""
                color: root.controller.error ? Theme.statusError : Theme.statusSuccess
                wrapMode: Text.WordWrap; textFormat: Text.PlainText
            }
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: root.controller.safeMode ? root.t("theme_safe_mode") : !root.controller.connected ? root.t("theme_offline")
                    : root.controller.unavailable ? root.t("theme_unavailable") : root.controller.legacyLight ? root.t("theme_legacy_light") : ""
                color: Theme.statusWarning; wrapMode: Text.WordWrap
            }
            GridLayout {
                Layout.fillWidth: true
                columns: width >= 1000 ? 3 : width >= 610 ? 2 : 1
                columnSpacing: Theme.spaceMd; rowSpacing: Theme.spaceMd
                Repeater {
                    model: root.controller.items
                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        readonly property bool selected: root.controller.selectedId === modelData.id
                        Layout.fillWidth: true
                        Layout.minimumWidth: 250
                        Layout.fillHeight: true
                        implicitHeight: content.implicitHeight + Theme.spaceLg * 2
                        color: selectButton.activeFocus ? Theme.bgHover : Theme.bgSurface
                        radius: Theme.radiusMd
                        border.color: selected || selectButton.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                        border.width: selected || selectButton.activeFocus ? 2 : 1
                        ColumnLayout {
                            id: content
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: Theme.spaceLg
                            spacing: Theme.spaceSm
                            AbstractButton {
                                id: selectButton
                                objectName: "themeCard_" + card.modelData.id
                                Layout.fillWidth: true
                                implicitHeight: cardBody.implicitHeight
                                enabled: root.controller.connected && !root.controller.busy
                                    && (!root.controller.safeMode || card.modelData.origin === "builtin")
                                Accessible.name: root.label(card.modelData.title)
                                Accessible.description: root.t(card.selected ? "theme_selected" : "theme_apply")
                                onClicked: root.controller.selectTheme(card.modelData.id)
                                background: Item {}
                                contentItem: ColumnLayout {
                                    id: cardBody
                                    spacing: Theme.spaceSm
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 0
                                        Repeater {
                                            model: card.modelData.swatches
                                            Rectangle { required property var modelData; Layout.fillWidth: true; implicitHeight: 48; color: modelData }
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label { Layout.fillWidth: true; text: root.label(card.modelData.title); font.pixelSize: Theme.fontDialogTitle; font.bold: true; color: Theme.textPrimary; textFormat: Text.PlainText; elide: Text.ElideRight }
                                        Label { text: card.selected ? "✓" : ""; color: Theme.accentPrimary }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: root.t("theme_origin_" + card.modelData.origin) + (card.modelData.ui ? " · QML" : "")
                                            + (card.modelData.version ? " · v" + card.modelData.version : "")
                                            + (card.modelData.schemaVersion ? "" : " · " + root.t("theme_dark_only"))
                                        color: Theme.textMuted; font.pixelSize: Theme.fontCaption; wrapMode: Text.WordWrap
                                    }
                                    Label { Layout.fillWidth: true; text: root.label(card.modelData.author) || "—"; color: Theme.textSecondary; textFormat: Text.PlainText; elide: Text.ElideRight }
                                }
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: Theme.spaceSm
                                TextButton {
                                    objectName: "updateTheme_" + card.modelData.id
                                    text: root.t("theme_update")
                                    visible: card.modelData.origin === "imported"
                                    subtle: true; enabled: root.controller.connected && !root.controller.busy
                                    onClicked: { root.updateId = card.modelData.id; updatePicker.open() }
                                }
                                TextButton {
                                    objectName: "reloadTheme_" + card.modelData.id
                                    text: root.t("theme_reload")
                                    visible: card.modelData.origin === "imported"
                                    subtle: true
                                    enabled: root.controller.connected && !root.controller.busy
                                        && !root.controller.safeMode && card.modelData.available
                                    onClicked: root.controller.reloadTheme(card.modelData.id)
                                }
                                TextButton {
                                    objectName: "exportTheme_" + card.modelData.id
                                    text: root.t(card.modelData.origin === "extension" ? "theme_export_palette" : "theme_export")
                                    subtle: true; enabled: root.controller.connected && !root.controller.busy
                                    onClicked: { root.exportId = card.modelData.id; exportPicker.open() }
                                }
                                TextButton {
                                    text: root.t("theme_delete")
                                    visible: card.modelData.origin === "imported"
                                    subtle: true; subtleText: Theme.statusError
                                    enabled: root.controller.connected && !root.controller.busy
                                    onClicked: { root.pendingId = card.modelData.id; deleteDialog.open() }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    FileDialog { id: importPicker; title: root.t("theme_import"); nameFilters: ["NekoTune (*.nktheme.zip *.zip)"]; onAccepted: root.controller.importTheme(String(selectedFile)) }
    FileDialog { id: updatePicker; title: root.t("theme_update"); nameFilters: ["NekoTune (*.nktheme.zip *.zip)"]; onAccepted: root.controller.updateTheme(root.updateId, String(selectedFile)) }
    FileDialog { id: exportPicker; title: root.t("theme_export"); fileMode: FileDialog.SaveFile; defaultSuffix: "nktheme.zip"; nameFilters: ["NekoTune (*.nktheme.zip)"]; onAccepted: root.controller.exportTheme(root.exportId, String(selectedFile)) }
    Connections {
        target: root.controller
        enabled: root.visible
        function onTrustRequested(id, target) { root.pendingId = id; root.pendingTarget = target; trustDialog.open() }
        function onReplaceRequested(path, themeId) { root.pendingPath = path; root.pendingUpdateId = themeId; replaceDialog.open() }
    }
    Dialog {
        id: trustDialog
        implicitHeight: 360
        parent: root; anchors.centerIn: parent; width: Math.min(500, parent ? parent.width - 40 : 500)
        title: root.t("extension_trust_title"); modal: true; standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label { text: root.t("theme_trust_body"); color: Theme.textPrimary; wrapMode: Text.WordWrap }
        onAccepted: {
            if (root.pendingTarget === "layout")
                root.controller.selectLayout(root.pendingId, true)
            else if (root.pendingTarget === "appearance")
                root.controller.selectAppearance(root.pendingId, true)
            else
                root.controller.selectTheme(root.pendingId, true)
        }
    }
    Dialog {
        id: replaceDialog
        objectName: "replaceThemeDialog"
        implicitHeight: 240
        parent: root; anchors.centerIn: parent; width: Math.min(500, parent ? parent.width - 40 : 500)
        title: root.t("theme_replace"); modal: true; standardButtons: Dialog.Yes | Dialog.Cancel
        contentItem: Label { objectName: "themeReplaceMessage"; text: root.t("theme_replace_body"); color: Theme.textPrimary; wrapMode: Text.WordWrap }
        onAccepted: {
            if (root.pendingUpdateId)
                root.controller.updateTheme(root.pendingUpdateId, root.pendingPath, true)
            else
                root.controller.importTheme(root.pendingPath, true)
        }
    }
    Dialog {
        id: deleteDialog
        implicitHeight: 240
        parent: root; anchors.centerIn: parent; width: Math.min(500, parent ? parent.width - 40 : 500)
        title: root.t("theme_delete"); modal: true; standardButtons: Dialog.Yes | Dialog.Cancel
        contentItem: Label { text: root.t("theme_delete_body"); color: Theme.textPrimary; wrapMode: Text.WordWrap }
        onAccepted: root.controller.removeTheme(root.pendingId)
    }
}
