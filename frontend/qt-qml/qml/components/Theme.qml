pragma Singleton
import QtQuick
import NekoTune 1.0

QtObject {
    id: theme
    readonly property bool recovery: typeof ignoreExtensionTheme !== "undefined" && ignoreExtensionTheme
    readonly property var controller: !recovery && typeof controllers !== "undefined" ? controllers.theme || null : null
    readonly property bool isDark: controller ? controller.isDark : true
    readonly property var baseTokens: ThemeDefaults.defaults(isDark)
    readonly property var overrides: recovery ? ({}) : controller ? controller.tokens
        : typeof controllers !== "undefined" && controllers.extensions ? controllers.extensions.themeTokens : ({})

    // 3.1 Background & Surface Tokens
    readonly property color bgCanvas: overrides.bgCanvas !== undefined ? overrides.bgCanvas : baseTokens.bgCanvas
    readonly property color bgSidebar: overrides.bgSidebar !== undefined ? overrides.bgSidebar : baseTokens.bgSidebar
    readonly property color bgSurface: overrides.bgSurface !== undefined ? overrides.bgSurface : baseTokens.bgSurface
    readonly property color bgRaised: overrides.bgRaised !== undefined ? overrides.bgRaised : baseTokens.bgRaised
    readonly property color bgHover: overrides.bgHover !== undefined ? overrides.bgHover : baseTokens.bgHover
    readonly property color bgSelected: overrides.bgSelected !== undefined ? overrides.bgSelected : baseTokens.bgSelected
    readonly property color bgCard: overrides.bgCard !== undefined ? overrides.bgCard : baseTokens.bgCard
    readonly property color borderSubtle: overrides.borderSubtle !== undefined ? overrides.borderSubtle : baseTokens.borderSubtle
    readonly property color borderControl: overrides.borderControl !== undefined ? overrides.borderControl : baseTokens.borderControl

    // 3.1 Text Tokens
    readonly property color textPrimary: overrides.textPrimary !== undefined ? overrides.textPrimary : baseTokens.textPrimary
    readonly property color textSecondary: overrides.textSecondary !== undefined ? overrides.textSecondary : baseTokens.textSecondary
    readonly property color textMuted: overrides.textMuted !== undefined ? overrides.textMuted : baseTokens.textMuted
    readonly property color textDisabled: overrides.textDisabled !== undefined ? overrides.textDisabled : baseTokens.textDisabled
    readonly property color textOnAccent: overrides.textOnAccent !== undefined ? overrides.textOnAccent : baseTokens.textOnAccent

    // 3.2 Accent & Brand Tokens
    readonly property color accentPrimary: overrides.accentPrimary !== undefined ? overrides.accentPrimary : baseTokens.accentPrimary
    readonly property color accentHover: overrides.accentHover !== undefined ? overrides.accentHover : baseTokens.accentHover
    readonly property color accentPressed: overrides.accentPressed !== undefined ? overrides.accentPressed : baseTokens.accentPressed
    readonly property color accentSecondary: overrides.accentSecondary !== undefined ? overrides.accentSecondary : baseTokens.accentSecondary

    // 3.2 Status Tokens
    readonly property color statusSuccess: overrides.statusSuccess !== undefined ? overrides.statusSuccess : baseTokens.statusSuccess
    readonly property color statusWarning: overrides.statusWarning !== undefined ? overrides.statusWarning : baseTokens.statusWarning
    readonly property color statusError: overrides.statusError !== undefined ? overrides.statusError : baseTokens.statusError
    readonly property color statusErrorBg: overrides.statusErrorBg !== undefined ? overrides.statusErrorBg : baseTokens.statusErrorBg
    readonly property color statusInfo: overrides.statusInfo !== undefined ? overrides.statusInfo : baseTokens.statusInfo

    // Brand atmosphere: layered surfaces, gradients and ambient glows for the
    // "yozakura radio" mood. Accent tints are used at low opacity only.
    readonly property color bgCanvasTop: overrides.bgCanvasTop !== undefined ? overrides.bgCanvasTop : baseTokens.bgCanvasTop
    readonly property color bgElevated: overrides.bgElevated !== undefined ? overrides.bgElevated : baseTokens.bgElevated
    readonly property color accentGlow: overrides.accentGlow !== undefined ? overrides.accentGlow : baseTokens.accentGlow
    readonly property color roseGlow: overrides.roseGlow !== undefined ? overrides.roseGlow : baseTokens.roseGlow
    readonly property real glowOpacity: overrides.glowOpacity !== undefined ? overrides.glowOpacity : baseTokens.glowOpacity

    // 4. Typography
    readonly property string fontFamily: overrides.fontFamily !== undefined ? overrides.fontFamily : baseTokens.fontFamily
    readonly property string fontFamilyMonospace: overrides.fontFamilyMonospace !== undefined ? overrides.fontFamilyMonospace : baseTokens.fontFamilyMonospace
    readonly property int fontTitle: overrides.fontTitle !== undefined ? overrides.fontTitle : baseTokens.fontTitle
    readonly property int fontSongTitle: overrides.fontSongTitle !== undefined ? overrides.fontSongTitle : baseTokens.fontSongTitle
    readonly property int fontDialogTitle: overrides.fontDialogTitle !== undefined ? overrides.fontDialogTitle : baseTokens.fontDialogTitle
    readonly property int fontBody: overrides.fontBody !== undefined ? overrides.fontBody : baseTokens.fontBody
    readonly property int fontBodySecondary: overrides.fontBodySecondary !== undefined ? overrides.fontBodySecondary : baseTokens.fontBodySecondary
    readonly property int fontCaption: overrides.fontCaption !== undefined ? overrides.fontCaption : baseTokens.fontCaption
    readonly property int fontLyricsWide: overrides.fontLyricsWide !== undefined ? overrides.fontLyricsWide : baseTokens.fontLyricsWide
    readonly property int fontLyricsCompact: overrides.fontLyricsCompact !== undefined ? overrides.fontLyricsCompact : baseTokens.fontLyricsCompact
    readonly property real lineHeightBody: overrides.lineHeightBody !== undefined ? overrides.lineHeightBody : baseTokens.lineHeightBody
    readonly property real lineHeightLyrics: overrides.lineHeightLyrics !== undefined ? overrides.lineHeightLyrics : baseTokens.lineHeightLyrics

    // 5. Spacing
    readonly property int spaceXs: overrides.spaceXs !== undefined ? overrides.spaceXs : baseTokens.spaceXs
    readonly property int spaceSm: overrides.spaceSm !== undefined ? overrides.spaceSm : baseTokens.spaceSm
    readonly property int spaceMd: overrides.spaceMd !== undefined ? overrides.spaceMd : baseTokens.spaceMd
    readonly property int spaceLg: overrides.spaceLg !== undefined ? overrides.spaceLg : baseTokens.spaceLg
    readonly property int spaceXl: overrides.spaceXl !== undefined ? overrides.spaceXl : baseTokens.spaceXl
    readonly property int space2Xl: overrides.space2Xl !== undefined ? overrides.space2Xl : baseTokens.space2Xl
    readonly property int space3Xl: overrides.space3Xl !== undefined ? overrides.space3Xl : baseTokens.space3Xl

    // 5. Radius
    readonly property int radiusSm: overrides.radiusSm !== undefined ? overrides.radiusSm : baseTokens.radiusSm
    readonly property int radiusMd: overrides.radiusMd !== undefined ? overrides.radiusMd : baseTokens.radiusMd
    readonly property int radiusLg: overrides.radiusLg !== undefined ? overrides.radiusLg : baseTokens.radiusLg
    readonly property int radiusFull: overrides.radiusFull !== undefined ? overrides.radiusFull : baseTokens.radiusFull

    // 5. Key Dimensions
    readonly property int sidebarWidthWide: overrides.sidebarWidthWide !== undefined ? overrides.sidebarWidthWide : baseTokens.sidebarWidthWide
    readonly property int sidebarWidthCompact: overrides.sidebarWidthCompact !== undefined ? overrides.sidebarWidthCompact : baseTokens.sidebarWidthCompact
    readonly property int bottomBarHeight: overrides.bottomBarHeight !== undefined ? overrides.bottomBarHeight : baseTokens.bottomBarHeight
    readonly property int songRowHeight: overrides.songRowHeight !== undefined ? overrides.songRowHeight : baseTokens.songRowHeight
    readonly property int songThumbnailSize: overrides.songThumbnailSize !== undefined ? overrides.songThumbnailSize : baseTokens.songThumbnailSize
    readonly property int controlMinHeight: overrides.controlMinHeight !== undefined ? overrides.controlMinHeight : baseTokens.controlMinHeight
    readonly property int iconButtonSize: overrides.iconButtonSize !== undefined ? overrides.iconButtonSize : baseTokens.iconButtonSize
    readonly property int iconButtonDenseSize: overrides.iconButtonDenseSize !== undefined ? overrides.iconButtonDenseSize : baseTokens.iconButtonDenseSize
    readonly property int playButtonSize: overrides.playButtonSize !== undefined ? overrides.playButtonSize : baseTokens.playButtonSize
    readonly property int artworkCompact: overrides.artworkCompact !== undefined ? overrides.artworkCompact : baseTokens.artworkCompact
    readonly property int artworkWide: overrides.artworkWide !== undefined ? overrides.artworkWide : baseTokens.artworkWide

    // 10. Animation duration (ms) & Accessibility
    readonly property bool reducedMotion: overrides.reducedMotion !== undefined ? overrides.reducedMotion : baseTokens.reducedMotion
    readonly property int durationFast: overrides.durationFast !== undefined ? overrides.durationFast : baseTokens.durationFast
    readonly property int durationNormal: overrides.durationNormal !== undefined ? overrides.durationNormal : baseTokens.durationNormal
    readonly property int durationDrawer: overrides.durationDrawer !== undefined ? overrides.durationDrawer : baseTokens.durationDrawer
    readonly property int durationSmooth: overrides.durationSmooth !== undefined ? overrides.durationSmooth : baseTokens.durationSmooth
    readonly property int durationScroll: overrides.durationScroll !== undefined ? overrides.durationScroll : baseTokens.durationScroll
}
