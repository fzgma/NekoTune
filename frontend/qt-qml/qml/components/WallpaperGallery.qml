pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Wallpaper category of the appearance page. It picks the style and source only;
// turning the wallpaper on stays in the wallpaper settings.
ColumnLayout {
    id: root
    required property var controller
    required property var manager
    required property var translator
    readonly property var preferences: manager.settings || ({})
    readonly property string style: preferences.style || "classic"
    readonly property string source: preferences.themeWallpaper || ""
    readonly property var entries: {
        const list = ["classic", "vinyl", "lyrics"].map(style => ({
            key: style, style: style, source: "", theme: false, missing: false,
            title: t("wallpaper_style_" + style), detail: t("wallpaper_gallery_builtin")}))
        list.push({key: "follow", style: "theme", source: "", theme: false, missing: false,
                   title: t("wallpaper_theme_follow"), detail: t("wallpaper_gallery_follow_hint")})
        const ids = []
        for (const item of controller.items || []) {
            if (!item.hasWallpaper)
                continue
            ids.push(item.id)
            list.push({key: item.id, style: "theme", source: item.id, theme: true, missing: false,
                       title: label(item.title),
                       detail: t("theme_origin_" + item.origin)
                           + (item.hasCode && !item.trusted ? " · " + t("wallpaper_gallery_needs_trust") : "")})
        }
        // Keep a saved source visible while its package is disabled or removed.
        if (source !== "" && !ids.includes(source))
            list.push({key: source, style: "theme", source: source, theme: true, missing: true,
                       title: t("wallpaper_theme_source_unavailable"), detail: source})
        return list
    }
    function t(key) { return translator.text(key, translator.language) }
    function label(value) {
        return typeof value === "string" ? value : value ? value[translator.language] || value.en || value.zh || "" : ""
    }
    function selected(entry) { return entry.style === style && (style !== "theme" || entry.source === source) }
    function choose(entry) {
        if (entry.theme)
            controller.selectWallpaper(entry.source)
        else if (entry.style === "theme")
            manager.setOptions({style: "theme", themeWallpaper: ""})
        else
            manager.setOption("style", entry.style)
    }
    spacing: Theme.spaceLg

    Label {
        Layout.fillWidth: true
        text: root.t("wallpaper_gallery_description")
        color: Theme.textSecondary; wrapMode: Text.WordWrap
    }
    Label {
        objectName: "wallpaperGalleryOff"
        Layout.fillWidth: true
        visible: !root.preferences.enabled
        text: root.t("wallpaper_gallery_off")
        color: Theme.textMuted; wrapMode: Text.WordWrap
    }
    Label {
        objectName: "wallpaperGalleryError"
        Layout.fillWidth: true
        visible: text !== ""
        text: root.manager.error ? root.t(root.manager.error)
            : root.controller.error === "theme_wallpaper_unavailable" ? root.t(root.controller.error) : ""
        color: Theme.statusError; wrapMode: Text.WordWrap; textFormat: Text.PlainText
    }
    GridLayout {
        Layout.fillWidth: true
        columns: width >= 1000 ? 3 : width >= 610 ? 2 : 1
        columnSpacing: Theme.spaceMd; rowSpacing: Theme.spaceMd
        Repeater {
            model: root.entries
            delegate: Rectangle {
                id: card
                required property var modelData
                readonly property bool current: root.selected(modelData)
                Layout.fillWidth: true
                Layout.minimumWidth: 250
                implicitHeight: body.implicitHeight + Theme.spaceLg * 2
                color: button.activeFocus ? Theme.bgHover : Theme.bgSurface
                radius: Theme.radiusMd
                border.color: current || button.activeFocus ? Theme.accentPrimary : Theme.borderSubtle
                border.width: current || button.activeFocus ? 2 : 1
                AbstractButton {
                    id: button
                    objectName: "wallpaperCard_" + card.modelData.key
                    anchors.fill: parent
                    enabled: !card.modelData.theme || (!card.modelData.missing && root.controller.connected
                        && !root.controller.busy && !root.controller.safeMode)
                    Accessible.name: card.modelData.title
                    Accessible.description: card.current ? root.t("wallpaper_gallery_selected") : card.modelData.detail
                    onClicked: root.choose(card.modelData)
                    background: Item {}
                }
                ColumnLayout {
                    id: body
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: Theme.spaceLg
                    spacing: Theme.spaceSm
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; text: card.modelData.title; font.pixelSize: Theme.fontDialogTitle; font.bold: true; color: button.enabled ? Theme.textPrimary : Theme.textDisabled; textFormat: Text.PlainText; elide: Text.ElideRight }
                        Label { objectName: "wallpaperBadge_" + card.modelData.key; text: card.current ? root.t("wallpaper_gallery_selected") : ""; color: Theme.accentPrimary; textFormat: Text.PlainText }
                    }
                    Label { Layout.fillWidth: true; text: card.modelData.detail; color: Theme.textMuted; font.pixelSize: Theme.fontCaption; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
                }
            }
        }
    }
    Connections {
        target: root.controller
        function onWallpaperReady(id) { root.manager.setOptions({style: "theme", themeWallpaper: id}) }
    }
}
