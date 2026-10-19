pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root
    required property var manager
    required property var translator
    readonly property var preferences: manager ? manager.settings : ({})
    readonly property var outputs: manager ? manager.outputs : []
    function t(key) { return translator.text(key, translator.language) }
    component OptionCheck: CheckBox {
        id: option
        Layout.fillWidth: true
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
        palette.highlight: Theme.accentPrimary
        contentItem: Text {
            text: option.text; font: option.font
            textFormat: Text.PlainText; wrapMode: Text.Wrap
            leftPadding: option.indicator ? option.indicator.width + option.spacing : 0
            verticalAlignment: Text.AlignVCenter
            color: option.enabled ? Theme.textPrimary : Theme.textDisabled
        }
    }
    implicitHeight: content.implicitHeight + 36
    radius: Theme.radiusMd; color: Theme.bgSurface; border.color: Theme.borderSubtle
    ColumnLayout {
        id: content
        anchors.fill: parent; anchors.margins: 18
        spacing: Theme.spaceMd
        Label { text: root.t("wallpaper_title"); color: Theme.textPrimary; font.pixelSize: Theme.fontDialogTitle }
        OptionCheck {
            objectName: "wallpaperEnabled"
            Layout.fillWidth: true
            text: root.t("wallpaper_enabled")
            checked: root.manager ? root.manager.requestedEnabled : false
            enabled: root.manager !== null
            onClicked: root.manager.setOption("enabled", checked)
        }
        Label {
            objectName: "wallpaperStatus"
            Layout.fillWidth: true
            text: !root.manager ? "" : !root.manager.supported ? root.t(root.manager.unavailableReason)
                : root.t("wallpaper_running_count") + " " + root.manager.runningCount
            color: Theme.textSecondary; wrapMode: Text.Wrap; textFormat: Text.PlainText
        }
        Label {
            Layout.fillWidth: true
            visible: root.manager !== null && root.manager.error !== ""
            text: root.manager ? root.t(root.manager.error) : ""
            color: Theme.statusError; wrapMode: Text.Wrap; textFormat: Text.PlainText
        }
        OptionCheck {
            Layout.fillWidth: true
            text: root.t("wallpaper_auto_restore")
            checked: root.preferences.autoRestore || false
            onClicked: root.manager.setOption("autoRestore", checked)
        }
        Label { text: root.t("wallpaper_style"); color: Theme.textSecondary }
        ChoiceField {
            objectName: "wallpaperStyle"
            Layout.fillWidth: true
            model: [root.t("wallpaper_style_classic"), root.t("wallpaper_style_vinyl"), root.t("wallpaper_style_lyrics")]
            currentIndex: ["classic", "vinyl", "lyrics"].indexOf(root.preferences.style || "classic")
            onActivated: index => root.manager.setOption("style", ["classic", "vinyl", "lyrics"][index])
        }
        Label { text: root.t("wallpaper_target"); color: Theme.textSecondary }
        ChoiceField {
            objectName: "wallpaperTarget"
            Layout.fillWidth: true
            model: [root.t("wallpaper_primary"), root.t("wallpaper_all"), root.t("wallpaper_selected")]
            currentIndex: ["primary", "all", "selected"].indexOf(root.preferences.target)
            onActivated: index => root.manager.setOption("target", ["primary", "all", "selected"][index])
        }
        ChoiceField {
            objectName: "wallpaperSelectedOutput"
            Layout.fillWidth: true
            visible: root.preferences.target === "selected"
            model: root.outputs.filter(output => output.name.length > 0)
            textRole: "name"; valueRole: "id"
            currentIndex: model.findIndex(output => output.id === root.preferences.screenId)
            onActivated: index => root.manager.setOption("screenId", model[index].id)
        }
        Repeater {
            model: root.outputs
            delegate: Label {
                required property var modelData
                Layout.fillWidth: true
                text: (modelData.name || root.t("wallpaper_selected")) + " · " + root.t("wallpaper_output_" + modelData.state)
                    + (modelData.error ? " — " + root.t(modelData.error) : "")
                color: modelData.error ? Theme.statusWarning : Theme.textMuted
                wrapMode: Text.Wrap; textFormat: Text.PlainText; font.pixelSize: Theme.fontCaption
            }
        }
        TextButton {
            text: root.t("wallpaper_retry"); subtle: true
            onClicked: root.manager.retry()
        }
        Repeater {
            model: [
                {key: "coverBackground", label: "wallpaper_cover_background"},
                {key: "blur", label: "wallpaper_blur"},
                {key: "lyrics", label: "wallpaper_lyrics"},
                {key: "nextLine", label: "wallpaper_next_line"},
                {key: "transparent", label: "wallpaper_transparent"}
            ]
            delegate: OptionCheck {
                required property var modelData
                Layout.fillWidth: true
                text: root.t(modelData.label)
                visible: modelData.key !== "transparent" || (root.manager && root.manager.linuxOverlay)
                enabled: modelData.key !== "blur" || Boolean(root.preferences.coverBackground)
                checked: Boolean(root.preferences[modelData.key])
                onClicked: root.manager.setOption(modelData.key, checked)
            }
        }
        OptionCheck {
            objectName: "wallpaperSpectrum"
            text: root.t("wallpaper_spectrum")
            checked: false; enabled: false
        }
        Label {
            Layout.fillWidth: true
            text: root.manager ? root.t(root.manager.audioAnalysis.reason) : ""
            color: Theme.textMuted; wrapMode: Text.Wrap; font.pixelSize: Theme.fontCaption
        }
        Label { Layout.fillWidth: true; text: root.t("wallpaper_update_hz"); color: Theme.textSecondary; wrapMode: Text.Wrap }
        ChoiceField {
            objectName: "wallpaperUpdateHz"
            Layout.fillWidth: true
            model: ["30 Hz", "60 Hz"]
            currentIndex: root.preferences.updateHz === 60 ? 1 : 0
            onActivated: index => root.manager.setOption("updateHz", index === 1 ? 60 : 30)
        }
        Label { text: root.t("wallpaper_intensity"); color: Theme.textSecondary }
        PlayerSlider {
            Layout.fillWidth: true
            from: 0; to: 1; stepSize: 0.05
            value: root.preferences.intensity === undefined ? 0.35 : root.preferences.intensity
            onPressedChanged: if (!pressed) root.manager.setOption("intensity", value)
            onMoved: if (!pressed) root.manager.setOption("intensity", value)
            Accessible.name: root.t("wallpaper_intensity")
        }
        Label {
            Layout.fillWidth: true
            text: root.t("wallpaper_limits")
            color: Theme.textMuted; font.pixelSize: Theme.fontCaption
            wrapMode: Text.Wrap; textFormat: Text.PlainText
        }
    }
}
