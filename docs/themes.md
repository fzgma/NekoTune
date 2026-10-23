# 主题制作与外观设置

主题用于呈现宿主已有能力：外观主题调整配色与样式，界面主题调整布局和交互，业务规则、数据及系统生命周期仍由宿主负责。职责、状态归属、接口及迁移目标统一见[主题边界规范](theme-boundaries.md)。

点击卡片应用整套主题；含界面的主题还提供“只用配色”和“只用界面”。配色与界面分别保存，可分别恢复默认；不含界面的卡片只改变配色，保留当前界面。

在「设置 → 外观」选择主题立即应用；浅色、深色与跟随系统独立保存。默认保持夜樱深色。内置夜樱、深海、森林均提供两种配色。系统无法报告明暗时按深色显示。「设置 → 外观」还提供“减少动态效果”，所有主题都必须遵守，主题无法更改此用户偏好。

启动时主窗口等待已保存的主题／界面扩展恢复，以及 QML 插槽和静态背景完成加载后再显示，避免先露出默认界面。缺失、禁用或加载失败的主题按既有规则回退；安全模式直接使用内置界面。等待仅用于首次显示，不影响之后的换肤或托盘隐藏；后端无响应时最多等待 12 秒，再显示可用界面并在外观页提示超时。

完整 Shell 启动时只创建选中的界面，内置 Shell 在需要回退或切回时才创建。已选界面扩展优先于无关后台扩展启动，仍遵守扩展依赖关系；同一轮状态变化合并处理，避免中间状态触发多余的界面创建。

导入只安装主题，点击卡片才启用。相同 ID 更新需要确认替换；一个主题包可以包含多个主题，删除包会删除其中全部主题，指向该包的配色和界面选择重置为空，其他包的选择不受影响。停用扩展只临时回退，不丢失原选择。后端断连期间不排队提交主题管理操作。

## 更新与重新加载

已导入主题的卡片显示包版本，提供「更新主题包」和「重新加载」。更新时选择同一 ID 的新版 ZIP，校验后确认替换；选错包会被拒绝，旧包及选择保持不变。更新当前主题后界面自动重建，保留明暗模式和播放队列；更新后新增的 QML 在确认信任前不会加载，主题包继续启用，配色仍可使用。

「重新加载」适用于已启用的主题，重新读取安装目录中的清单、QML 和资源；不会从原 ZIP 路径更新安装内容。开发目录挂载会自动监听变动，也可手动重新加载。安全模式禁用重新加载。普通扩展提供的主题仍在扩展管理器中更新，以维护后台服务。

完整 Shell 当前可复用 `qrc:/qml/components/AppearancePanel.qml`，传入 `controller: api.app.theme` 和上下文中的 `translator: i18n`，使宿主新增管理操作随版本更新。这是现有复用入口，不代表全部内部 `qrc:` 路径都是稳定主题 API，详见[接口边界](theme-boundaries.md#4-主题可用的接口)。

## 创建、校验与打包

复制 `examples/extensions/theme-palette`（纯配色）或 `examples/extensions/theme-qml`（含 QML 背景、播放栏和壁纸）作为起点，修改 `extension.json` 的 `id`、`name` 和主题内容。主题作者编辑 JSON/QML 文件；应用没有内置主题编辑器或在线主题目录。

这些示例用于展示外观和局部界面能力，不包含完整默认前端。`sdk/templates/qml` 是普通页面扩展模板；完整默认界面主题的抽取和导出属于[待实现迁移](theme-boundaries.md#7-默认前端与模板的迁移)。

开发时在扩展管理页挂载主题目录，宿主在挂载和每次重载时完成校验，错误显示在管理页。分发时把目录内容打成 ZIP（根目录直接是 `extension.json`），可以用宿主 SDK 的打包脚本：

```sh
cd /tmp/my-theme
cmake -DOUTPUT=/tmp/my-theme.nktheme.zip -P /path/to/NekoTune/sdk/cmake/NekoTunePackage.cmake
```

也可以在「设置 → 外观」导出已安装的主题，得到可再次导入的包。

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
      "wallpaper": "Wallpaper.qml",
      "ui": {
        "background": "Background.qml",
        "bottomPlayer": "Player.qml",
        "pages": {"home": "Home.qml"}
      }
    }]
  }
}
```

`ui`、`wallpaper`、`tokens` 和 `background` 可省略；使用示例中的入口和图片时须提供对应文件。每个新主题必须有 `light` 与 `dark`，未覆盖的值继承对应明暗的基础主题。覆盖顺序是基础值、公共 `tokens`、变体 `tokens`。

完整变量、类型、默认值和范围以 [`shared/themes/catalog.json`](../shared/themes/catalog.json) 为准。每个变量的 `group` 指明它归外观（`appearance`）、界面（`layout`）、共享（`shared`）还是宿主（`host`）；外观主题中的 `layout` 组变量只在搭配内置界面或它自己的界面时生效。`host` 组的 `reducedMotion` 由用户设置决定，主题中的值会被忽略。共享变量在另一界面贡献提供对应值时由界面覆盖。颜色写为 `#RRGGBB` 或 Qt 的 `#AARRGGBB`；数值必须有限且在范围内，整数变量不接受小数。字体名称引用系统字体，缺失时使用系统回退，不携带字体文件。旧版仅有 `tokens` 的扩展主题继续兼容，浅色模式使用内置浅色并提示。

背景参数 `opacity`、`overlayOpacity`、`blur` 均为 0–1。静态背景默认显示在主内容区和歌词页，图片按比例裁剪填充。未指定遮罩时使用主题底色、0.65 的遮罩透明度。软件渲染关闭模糊；图片读取失败时保留主题底色。

包内允许 QML、JavaScript、JSON、`qmldir`、PNG/JPEG。禁止链接文件、越界资源路径、扩展库（`library`）、原生模块和扩展依赖。压缩包最多 20 MiB，解压文件最多 64 MiB、2048 个文件，图片总像素量最多 1600 万。

## QML 界面

`ui` 可包含 `background`、`shell`、`bottomPlayer` 与 `pages`；壁纸入口写在与 `ui` 同级的 `wallpaper` 字段（见[主题音乐壁纸](#主题音乐壁纸)）。页面键为内置页面 ID：`home`、`library`、`queue`、`lyrics`、`settings`、`extensions`、`music_sources`、`lyrics_debug`。入口为包内相对路径，根对象必须继承 `Item`。

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

通过 `api.app` 调用宿主业务控制器，通过 `api.theme` 获取主题变量。深浅色共用同一入口，不因系统切换销毁视图。专用主题包没有后台代码；需要后台逻辑、外部依赖或服务注册时应作为普通扩展提供主题。

主题可以提供编辑器、搜索和下载操作的界面，但校验、保存、下载执行和数据一致性由宿主或功能扩展负责。初始化、重载和配色变化不得自动执行这些业务操作；需要用户明确动作或已有宿主业务流程。具体规则见[职责分配](theme-boundaries.md#2-职责分配)。

插槽优先级为当前界面选择、独立扩展选择、内置实现。切走界面主题恢复原独立选择；默认 Shell 才会自动放置背景和页面。完整 Shell 负责自己的布局、背景及导航。浏览主题卡片不实例化主题代码，页面入口只在显示时加载。

主题包无需信任即可启用并提供配色；含 QML/JavaScript 的界面或壁纸在首次加载前需要信任，信任后拥有与现有扩展相同的访问能力。信任按包保存；只用配色通过 `selectAppearance` 不需要授权，一键应用含代码主题仍会请求信任，在外观页的壁纸分类选用含代码主题的壁纸也会请求信任。普通扩展附带的主题沿用普通扩展的运行授权要求。主题专用包不加载原生库。独立 QML 引擎不是安全沙箱，也没有独立 GUI 线程；不能依赖它隔离死循环或恶意代码。

## 主题音乐壁纸

每个主题可通过顶层字段 `wallpaper` 提供一个完整音乐壁纸布局，值为包内 QML 相对路径，深浅色共用入口，须声明 `schemaVersion: 1`。旧写法 `ui.wallpaper` 已废弃，导入时报错 `Theme wallpaper moved from ui.wallpaper to wallpaper`；把该字段移到与 `ui` 同级即可。

用户可以在「设置 → 外观 → 壁纸」中选择内置样式、“跟随当前主题”（先取当前界面的壁纸，没有再取当前配色的壁纸）或任意主题的壁纸；「设置 → 动态音乐壁纸」中的“壁纸样式”和“主题壁纸来源”是同一份设置。选择未信任的含代码主题壁纸时会先请求授权。两处的选择都不会开启壁纸，开关仍在动态音乐壁纸设置中。其他三种内置样式不会实例化主题壁纸入口。主题切换、更新和重新加载仅替换窗口内容，原生桌面窗口保持不变；每屏有独立视图实例。

完整 Shell 可从 `api.window.wallpaper` 获取宿主壁纸管理器，并复用 `qrc:/qml/wallpaper/WallpaperSettings.qml`，传入 `manager`、`themeController: api.app.theme` 和窗口的 `translator`，在自定义设置页中提供相同的启用、样式及每屏状态控件。

壁纸根对象继承 `Item`，通过 `ExtensionApi.wallpaper` 获取只读上下文；其他扩展视图中该属性为 `null`：

| 属性 | 含义 |
| --- | --- |
| `song`、`lyrics` | 当前歌曲元数据和歌词状态，沿用宿主数据结构 |
| `position`、`duration`、`playbackRate` | 毫秒位置、毫秒时长及播放倍速 |
| `connected`、`hasSong`、`playing`、`updating` | 完整数据已连接、歌曲存在、正在播放和展示可更新 |
| `preferences` | 公共壁纸设置，包括 `lyrics`、`nextLine`、`updateHz`、`intensity` 等 |
| `screen` | 当前屏幕信息，包括 `devicePixelRatio` |

使用 `api.theme` 读取配色与 `reducedMotion`，公共封面背景、模糊和透明叠加由宿主在主题内容后方绘制。自定义布局须遵守歌词开关、插值频率和装饰强度：动画仅在 `playing`／`updating` 允许时运行，并尊重减少动态效果；歌词建议复用 `qrc:/qml/components/CompactLyrics.qml`，传入上下文数据，以保留切歌身份校验和逐字同步。可运行示例见 `examples/extensions/theme-qml/Wallpaper.qml`。真实频谱可由独立功能扩展通过服务调用和事件推送提供，原始 PCM 不进入主题；宿主 SDK 2.2 仅向原生扩展提供 PCM。

未提供入口、主题不可用或入口编译／创建／根类型失败时显示经典卡片，设置页提示原因和每屏加载错误；保存的 `wallpaper.style: "theme"` 保持不变，主题恢复可用后自动接回。指定来源不可用时提示并回退经典卡片，`wallpaper.themeWallpaper` 保留，来源恢复后自动接回。安全模式不加载自定义壁纸且禁止自动恢复；手动开启壁纸时回退经典卡片。加载仍沿用 QML 主题信任要求，独立引擎不是安全沙箱；运行中的任意脚本异常或死循环不保证能自动回退。

## 导出与恢复

主题专用包导出时保留全部主题、QML 及资源。普通扩展的「导出配色与背景」只导出所选主题的两个配色和静态图片，不复制可能依赖后台的 QML；分享完整界面应分享原扩展包。

QML 编译、创建或根类型错误会显示错误并回退到下一层插槽；其他主题样式仍生效。“全部恢复默认”恢复夜樱、内置界面与深色模式，不改变独立扩展插槽或壁纸设置。`Ctrl+Shift+E` 打开固定内置样式的恢复窗口，可管理外观或恢复全部内置界面。无法交互时以 `nekotune --safe-mode` 启动，此时不加载自定义主题/QML。

新主题注册接口和导出参数见 [扩展文档](extensions.md) 与 [API 文档](API.md)。
