# 主题制作与外观设置

在「设置 → 外观」选择主题立即应用；浅色、深色与跟随系统独立保存。默认保持夜樱深色。内置夜樱、深海、森林均提供两种配色。系统无法报告明暗时按深色显示。

启动时主窗口等待已保存的主题／界面扩展恢复，以及 QML 插槽和静态背景完成加载后再显示，避免先露出默认界面。缺失、禁用或加载失败的主题按既有规则回退；安全模式直接使用内置界面。等待仅用于首次显示，不影响之后的换肤或托盘隐藏；后端无响应时最多等待 12 秒，再显示可用界面并在外观页提示超时。

完整 Shell 启动时只创建选中的界面，内置 Shell 在需要回退或切回时才创建。已选界面扩展优先于无关后台扩展启动，仍遵守扩展依赖关系；同一轮状态变化合并处理，避免中间状态触发多余的界面创建。

导入只安装主题，点击卡片才启用。相同 ID 更新需要确认替换；一个主题包可以包含多个主题，删除包会删除其中全部主题，正在使用的主题先恢复默认。停用扩展只临时回退，不丢失原选择。后端断连期间不排队提交主题管理操作。

## 更新与重新加载

已导入主题的卡片显示包版本，提供「更新主题包」和「重新加载」。更新时选择同一 ID 的新版 ZIP，校验后确认替换；选错包会被拒绝，旧包及选择保持不变。更新当前主题后界面自动重建，保留明暗模式和播放队列；新增 QML 权限仍须点击卡片确认信任。

「重新加载」适用于已启用的主题，重新读取安装目录中的清单、QML 和资源；不会从原 ZIP 路径更新安装内容。开发目录挂载会自动监听变动，也可手动重新加载。安全模式禁用重新加载。普通扩展提供的主题仍在扩展管理器中更新，以维护后台服务。

完整 Shell 建议复用 `qrc:/qml/components/AppearancePanel.qml`，传入 `controller: api.app.theme` 和上下文中的 `translator: i18n`，使宿主新增管理操作随版本更新。

## 创建、校验与打包

在仓库的 `extensions` 目录运行：

```sh
pnpm run create-theme /tmp/my-theme
# 或创建包含 QML 背景入口的模板：
pnpm run create-qml-theme /tmp/my-qml-theme
pnpm run validate /tmp/my-theme
pnpm run pack /tmp/my-theme /tmp/my-theme.nktheme.zip
```

参考 `extensions/examples/theme-palette` 和 `extensions/examples/theme-qml`。主题作者编辑 JSON/QML 文件；应用没有内置主题编辑器或在线主题目录。

## 文件格式

主题包是 ZIP，根目录包含 `extension.json`：

```json
{
  "id": "theme.example",
  "name": "Example theme",
  "version": "1.0.0",
  "apiVersion": 1,
  "type": "theme",
  "contributes": {
    "themes": [{
      "id": "default",
      "title": {"zh": "示例", "en": "Example"},
      "author": "Your name",
      "schemaVersion": 1,
      "tokens": {"radiusMd": 12, "fontFamily": "sans-serif"},
      "variants": {
        "light": {"tokens": {"accentPrimary": "#2E6B4A"}},
        "dark": {
          "tokens": {"accentPrimary": "#9BD4B3"},
          "background": {
            "source": "assets/background.png",
            "opacity": 1,
            "overlayColor": "#101A16",
            "overlayOpacity": 0.65,
            "blur": 0.3
          }
        }
      },
      "ui": {
        "background": "Background.qml",
        "bottomPlayer": "Player.qml",
        "pages": {"home": "Home.qml"}
      }
    }]
  }
}
```

`ui`、`tokens` 和 `background` 可省略；使用示例中的入口和图片时须提供对应文件。每个新主题必须有 `light` 与 `dark`，未覆盖的值继承对应明暗的基础主题。覆盖顺序是基础值、公共 `tokens`、变体 `tokens`。

完整变量、类型、默认值和范围以 [`shared/themes/catalog.json`](../shared/themes/catalog.json) 为准。颜色写为 `#RRGGBB` 或 Qt 的 `#AARRGGBB`；数值必须有限且在范围内，整数变量不接受小数。字体名称引用系统字体，缺失时使用系统回退，不携带字体文件。旧版仅有 `tokens` 的扩展主题继续兼容，浅色模式使用内置浅色并提示。

背景参数 `opacity`、`overlayOpacity`、`blur` 均为 0–1。静态背景默认显示在主内容区和歌词页，图片按比例裁剪填充。未指定遮罩时使用主题底色、0.65 的遮罩透明度。软件渲染关闭模糊；图片读取失败时保留主题底色。

包内允许 QML、JavaScript、JSON、`qmldir`、PNG/JPEG。禁止链接文件、越界资源路径、Node 入口、原生模块和扩展依赖。压缩包最多 20 MiB，解压文件最多 64 MiB、2048 个文件，图片总像素量最多 1600 万。

## QML 界面

`ui` 可包含 `background`、`shell`、`bottomPlayer` 与 `pages`。页面键为内置页面 ID：`home`、`library`、`queue`、`lyrics`、`settings`、`extensions`、`music_sources`、`lyrics_debug`。入口为包内相对路径，根对象必须继承 `Item`。

```qml
import QtQuick
import NekoTune 1.0

Rectangle {
    ExtensionApi { id: api }
    color: api.theme.bgSurface
    Text {
        anchors.centerIn: parent
        text: api.theme.isDark ? "Dark" : "Light"
        color: api.theme.textPrimary
        font.family: api.theme.fontFamily
        font.pixelSize: api.theme.fontBody
    }
}
```

通过 `api.app` 调用宿主业务控制器，通过 `api.theme` 获取主题变量。深浅色共用同一入口，不因系统切换销毁视图。专用主题包没有后台服务；需要 Worker、外部依赖或服务注册时应作为普通扩展提供主题。

插槽优先级为当前主题、独立扩展选择、内置实现。切走主题恢复原独立选择；默认 Shell 才会自动放置背景和页面。完整 Shell 负责自己的布局、背景及导航。浏览主题卡片不实例化主题代码，页面入口只在显示时加载。

含 QML/JavaScript 的主题首次启用需要信任，拥有与现有扩展相同的访问能力。纯数据主题不需要代码授权。两者都不创建 Node Worker。独立 QML 引擎不是安全沙箱，也没有独立 GUI 线程；不能依赖它隔离死循环或恶意代码。

## 导出与恢复

主题专用包导出时保留全部主题、QML 及资源。普通扩展的「导出配色与背景」只导出所选主题的两个配色和静态图片，不复制可能依赖后台的 QML；分享完整界面应分享原扩展包。

QML 编译、创建或根类型错误会显示错误并回退到下一层插槽；其他主题样式仍生效。恢复默认主题不改变独立扩展插槽。`Ctrl+Shift+E` 打开固定内置样式的恢复窗口，可管理外观或恢复全部内置界面。无法交互时以 `nekotune --safe-mode` 启动，此时不加载自定义主题/QML。

新主题注册接口和导出参数见 [扩展文档](extensions.md) 与 [API 文档](API.md)。
