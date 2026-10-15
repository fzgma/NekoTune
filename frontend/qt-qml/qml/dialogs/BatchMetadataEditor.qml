pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Popup {
    id: root
    required property var controller
    required property var translator
    property var songIds: []
    property bool pending: false
    property string error: ""
    component ApplyChoice: CheckBox {
        id: choice
        palette.windowText: Theme.textPrimary
        indicator: Rectangle {
            x: choice.leftPadding
            y: (choice.height - height) / 2
            width: 18; height: 18; radius: 4
            color: choice.checked ? Theme.accentPrimary : Theme.bgSurface
            border.color: choice.activeFocus ? Theme.accentPrimary : Theme.borderControl
            Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 2; color: Theme.textOnAccent; visible: choice.checked }
        }
    }
    component BatchField: ColumnLayout {
        id: field
        required property string modelData
        readonly property string key: modelData
        readonly property bool numeric: key === "year" || key === "disc_number"
        property alias applyField: choice.checked
        property alias value: input.text
        readonly property bool valid: input.acceptableInput
        Layout.fillWidth: true
        spacing: Theme.spaceXs
        ApplyChoice {
            id: choice
            objectName: "batchApply_" + field.key
            text: root.t("metadata_" + field.key)
            enabled: !root.pending
            palette.windowText: Theme.textPrimary
        }
        InputField {
            id: input
            objectName: "batchValue_" + field.key
            Layout.fillWidth: true
            enabled: field.applyField && !root.pending
            placeholderText: root.t("batch_metadata_empty_clears")
            validator: field.numeric ? numbers : null
            IntValidator { id: numbers; bottom: 0; top: 9999 }
            Accessible.name: choice.text
        }
    }
    function t(key) { return translator.text(key, translator.language) }
    function openForSongs(ids) {
        if (pending || !ids.length) return
        songIds = ids.slice()
        error = ""
        for (let i = 0; i < fields.count; ++i) {
            const field = fields.itemAt(i) as BatchField
            field.applyField = false
            field.value = ""
        }
        tagsChoice.checked = false
        tagsInput.text = ""
        tagMode.currentIndex = 0
        open()
    }
    function save() {
        if (pending) return
        const patch = {}
        for (let i = 0; i < fields.count; ++i) {
            const field = fields.itemAt(i) as BatchField
            if (!field.applyField) continue
            if (field.numeric && field.value && !field.valid) { error = t("metadata_invalid_number"); return }
            patch[field.key] = field.numeric ? Number(field.value || 0) : field.value
        }
        if (tagsChoice.checked) patch.tags = tagsInput.text.split(/[,，\n]/).map(tag => tag.trim()).filter(tag => tag.length)
        if (!Object.keys(patch).length) { error = t("batch_metadata_no_fields"); return }
        pending = true
        error = ""
        controller.saveBatchMetadata(songIds, patch, String(tagMode.currentValue))
    }
    objectName: "batchMetadataEditor"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(560, parent ? parent.width - 40 : 560)
    height: Math.min(620, parent ? parent.height - 40 : 620)
    modal: true
    focus: true
    padding: 24
    closePolicy: pending ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { objectName: "shortcutBlocker"; color: Theme.bgRaised; radius: Theme.radiusLg; border.color: Theme.borderSubtle }
    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spaceMd
        Label { text: root.t("batch_metadata_title").replace("%1", root.songIds.length); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
        Label { Layout.fillWidth: true; text: root.t("batch_metadata_hint"); wrapMode: Text.WordWrap; color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
        ScrollView {
            id: scroll
            objectName: "batchMetadataScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: scroll.availableWidth
                spacing: Theme.spaceMd
                Repeater {
                    id: fields
                    model: ["custom_title", "artist", "album", "album_artist", "genre", "year", "disc_number"]
                    delegate: BatchField {}
                }
                ApplyChoice { id: tagsChoice; objectName: "batchApply_tags"; text: root.t("tags"); enabled: !root.pending }
                ChoiceField {
                    id: tagMode
                    objectName: "batchTagMode"
                    Layout.fillWidth: true
                    enabled: tagsChoice.checked && !root.pending
                    textRole: "label"; valueRole: "value"
                    model: ["add", "replace", "remove"].map(value => ({value: value, label: root.t("batch_tags_" + value)}))
                    Accessible.name: root.t("tags")
                }
                InputField { id: tagsInput; objectName: "batchValue_tags"; Layout.fillWidth: true; enabled: tagsChoice.checked && !root.pending; placeholderText: root.t("batch_tags_hint") }
            }
        }
        Label { objectName: "batchMetadataError"; Layout.fillWidth: true; visible: root.error.length > 0; text: root.error; textFormat: Text.PlainText; color: Theme.statusError; wrapMode: Text.WrapAnywhere }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            TextButton { text: root.t("cancel"); subtle: true; enabled: !root.pending; onClicked: root.close() }
            TextButton { objectName: "batchMetadataSave"; text: root.t("batch_metadata_apply").replace("%1", root.songIds.length); enabled: !root.pending; onClicked: root.save() }
        }
    }
    Connections {
        target: root.controller
        function onBatchMetadataSaved(count) { if (root.pending) { root.pending = false; root.close() } }
        function onRequestFailed(method, message) {
            if (method === "library.update_metadata" && root.pending) { root.pending = false; root.error = message }
        }
    }
}
