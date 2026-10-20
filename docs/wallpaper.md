# 动态音乐桌面壁纸（实验性）

此功能是同一前端进程中的额外展示层。Linux 使用原生 Wayland LayerBottom；Windows 尝试嵌入实际 Explorer 桌面图标后方的 WorkerW。没有普通全屏窗口降级路径。关闭壁纸不停止音乐，不修改 awww、系统背景或登录启动项。

## 架构与边界

- `WallpaperManager` 管理设置、输出身份、每屏状态、QML 窗口及退出清理。声明在主 `QQmlApplicationEngine` 后，退出与析构均同步销毁所有壁纸；销毁早于引擎及控制器。
- `WallpaperPlatform` 是 GUI 线程的平台边界，分别由 Wayland、Windows 和 unavailable 实现。`prepare` 成功后才能显示窗口；`starting` 只有得到 Qt expose 后才成为 `running`。这里的 expose 只确认已映射，不表示没有遮挡。
- 每个输出的宿主窗口共享主引擎、`AppControllers`、`IpcClient`、`I18n` 和封面网络缓存；主题内容沿用 `ExtensionView` 的独立 QML 引擎，没有新播放器、音频输出、独立后端或辅助进程。
- `Wallpaper.qml` 读取已有播放状态及歌词。`CompactLyrics` 继续核对 `resource_key / song_hash == lyrics.track_id`，拒绝迟到歌词；LRC、KRC、seek、倍速及纯文本规则不变。
- 新连接必须收到完整 `server.connected` 才重新开始动画。断线立即停止插值；不轮询 `player.status`，不发送任何壁纸专用 IPC，也不重放修改请求。
- `AlbumArt` 按实际尺寸及 DPR 限制解码尺寸，保留后端 `cover_url` 与默认封面回退。背景只读同一 URL，不增加搜索或来源选择。
- 后端的 CMake 目标、播放器接口及音频线程均未修改；独立前端同样可以创建壁纸，退出不会停止外部后端。

## 设置与生命周期

设置页的「动态音乐壁纸」独立组件提供启用、样式、主屏／全部／指定屏、封面背景、模糊、当前／下一行歌词、透明叠加、歌词插值更新频率、装饰强度和启动恢复。

样式保存在 `wallpaper.style`，默认 `classic`，可选 `vinyl`、`lyrics`、`theme`（跟随当前主题）。旧配置缺少该键或值非法时保留经典卡片；非法写入及保存失败不会切换样式。运行时由同一窗口内的 Loader 或主题插槽替换布局，旧布局及动画随之销毁，不重建原生 surface、不新增壁纸专用 IPC 请求。所有输出使用同一份样式设置。

- **经典卡片**：保留原有封面、歌曲信息、进度及紧凑歌词。
- **黑胶唱片**：封面放在唱片中央，旁边显示歌曲及歌词。竖屏采用上下布局；原生旋转动画由播放状态驱动，强度控制转速，暂停保留当前角度，断线、隐藏、零强度和 reducedMotion 停止动画。它不是节拍检测或频谱。
- **全屏歌词**：顶部显示封面、歌曲及进度，中央放大当前行与可选下一行。`CompactLyrics.wrap` 为此样式启用换行，原有桌面歌词等调用默认仍不换行；同步身份、KRC／LRC 时间轴、纯文本无法同步提示保持不变。关闭歌词时只保留歌曲信息。

三种布局共用 `WallpaperTrackInfo`；新布局分别位于 `VinylWallpaper.qml` 和 `LyricsWallpaper.qml`。封面背景、模糊、透明叠加、歌词开关和 30／60 Hz 插值设置对所有样式一致生效。

「跟随当前主题」使用当前活动主题的 `ui.wallpaper` QML `Item`，入口与只读 `ExtensionApi.wallpaper` 上下文见 [主题制作](themes.md#主题音乐壁纸)。公共背景仍由宿主绘制；主题负责遵守公共歌词及动画设置。选主题不会开启壁纸，内置样式不加载主题壁纸。切换、更新和重新加载主题会释放旧内容并载入新内容，每屏独立实例化，原生窗口保持不变。

当前主题无入口、被停用／移除或入口加载失败时，保留 `theme` 偏好并回退经典卡片，恢复可用后自动接回。`outputs` 的 `themeStatus` 为 `loading`、`active`、`missing`、`error`、`safe-mode` 或空字符串（内置样式）；`themeError` 提供加载错误详情。设置显示提示；这些内容错误不改变原生输出的 `running` 状态，也不触发平台重试。安全模式不加载自定义壁纸。

`WallpaperContext` 是窗口拥有的只读 C++ 对象，直接转发控制器的歌曲、歌词和播放通知，由各个消费引擎转换为自己的值；不跨引擎传递宿主 QML `var` 对象。播放位置、歌词、设置使用各自通知，避免位置更新反复替换歌词文档。每屏内容状态通过独立 `outputsChanged` 通知，避免重新触发壁纸设置绑定。

2026-10-20 主题壁纸验收：SDK 类型检查与 33 项测试通过；壁纸、扩展、QML 组件及依赖边界四组 CTest 通过。Niri 原生单屏 Wayland 检查主题加载／重载、原生句柄保持及退出释放通过，使用临时 IPC 播放数据，没有实际音频输出。示例包及截图位于本地 `build/theme-wallpaper-evidence/`；未新增 Windows、多屏或性能验收结论。

`CompactLyrics` 换句时对当前歌词与下一句一起淡入淡出，并附带 14px 位移；向后跳转时反向过渡。沿用主题 `durationSmooth` 时长，逐字高亮继续跟随播放时间。连续跳转只保留最近一组过渡，结束后释放旧歌词；切歌、替换歌词、断线、隐藏、暂停更新、零装饰强度或减少动态效果时立即结束过渡。

设置通过 `AppPaths::saveSetting("wallpaper", ...)` 合并保存至现有 `config/settings.json`，保留其他键及 `NEKOTUNE_HOME` 隔离。非法类型、范围或枚举拒绝保存；读入非法值使用默认值。保存失败明确报告，正常设置保留旧值。关闭操作即使保存失败也立即销毁窗口，但下次启动可能仍读到旧偏好。

`settings.enabled` 是保存的意愿，`requestedEnabled` 是本次会话请求，`supported / unavailableReason` 是平台能力，`outputs` 提供每屏 `state / error`，`runningCount` 是已映射实例数。勾选启用并不等于成功运行。未启用「启动恢复」时下次会话仍需手动打开；`--safe-mode` 始终禁止自动恢复，便于失败恢复。此功能不配置系统登录自启动。

输出 ID 为接口名、制造商、型号和序列号的 SHA-256。接口变化、EDID 缺失或改变会影响稳定性；不以列表下标选择屏幕。指定屏缺失或标识重复时保持不可用，不擅自转移；重连后按相同身份恢复。主屏模式跟随主屏变化，全部模式为新增输出创建窗口。移除输出立即销毁对应实例；失败输出不影响其他实例。关闭、退出及销毁可重复调用。

关闭到托盘时壁纸继续运行。壁纸不参与 `PlaybackWindowController::presentationVisible()`，不影响无托盘时主窗口恢复、关闭即退出、托盘退出或 MPRIS Quit。停止时取消恢复计时器并停止展示，再释放原生窗口。

## Wayland / Niri

可选依赖为 Qt 6.10+、匹配的 QtWaylandClient 私有开发接口和 LayerShellQt 6.6+，导出目标为 `LayerShellQt::Interface`。项目总体最低 Qt 6.6 不变；更旧 Qt 或依赖缺失时关闭此平台能力并打印配置说明。私有接口用于映射前核验实际 layer-shell integration，而不是假设调用配置 API 已经成功。

已核对 [LayerShellQt 6.6 的窗口实现](https://github.com/KDE/layer-shell-qt/blob/0665a494323c828b69c9e6125135287c0e2559d4/src/interfaces/window.cpp)及开发机 6.7.4 接口：`Window::get(window)` 在 `SurfaceCreated` 时为该窗口调用 `setShellIntegration`。不调用全局 `Shell::useLayerShell()`，不设置 `QT_WAYLAND_SHELL_INTEGRATION`，不改 QPA。因此主窗口、迷你窗口及扩展恢复窗口仍走普通 xdg-shell。

平台先检查 `QGuiApplication::platformName()`，用隐藏的原生窗口核验合成器协议及实际集成，再为目标输出配置：

| 参数 | 值 |
| --- | --- |
| layer | `LayerBottom`（协议值 1） |
| anchors | Top、Bottom、Left、Right（15） |
| exclusive zone | −1 |
| keyboard interactivity | None |
| namespace / scope | `nekotune-music-wallpaper` |
| 输出 | 显式 `LayerShellQt::Window::setScreen(QScreen*)` |
| 输入 | `Qt::WindowTransparentForInput` |

配置先于首次映射；QtWayland 和 LayerShellQt 的 configure 负责逻辑尺寸、缩放及旋转后的重绘。Qt 的透明输入 flag 会提交空的 `wl_region`，见 [Qt 6.11.2 的 updateInputRegion](https://github.com/qt/qtbase/blob/v6.11.2/src/plugins/platforms/wayland/qwaylandwindow.cpp)。不把空 `QWindow::setMask` 当作输入穿透，因为其语义不同。compositor dismiss 或平台 surface 丢失后关闭实例；手动重试重新检查能力。xcb、offscreen 及无协议合成器都显示不可用。

不需要 Niri KDL 配置，也不需要普通窗口规则。已有 awww 位于 Background 时，Bottom 壁纸自然在其上方、普通应用下方；透明处可保留已有背景，不依赖同层创建顺序。若用户的其他组件使用不同层级，应以其实际配置和 `niri msg layers` 为准。

Arch 构建（沿用 README 的播放器依赖）：

```sh
sudo pacman -S --needed base-devel cmake ninja pnpm qt6-base qt6-declarative \
  qt6-multimedia qtkeychain-qt6 ffmpeg zlib libsecret layer-shell-qt wayland
# 当前 Arch Qt 6.11 的 Wayland 支持随 qt6-base 提供；较旧仓库可能另有 qt6-wayland。
cmake -S . -B build -DNEKOTUNE_ENABLE_WALLPAPER=ON
cmake --build build -j2
QT_QPA_PLATFORM=wayland ./build/nekotune
```

从设置手动启用。程序不自行强制切换 QPA；上面的环境变量只作用于这次显式启动。验证实际层级与已有背景：

```sh
niri msg version
niri msg --json outputs
niri msg --json layers
awww --version
awww query
# 隔离测试会自行建立临时 NEKOTUNE_HOME / SOCKET；不连接用户后端。
QT_QPA_PLATFORM=wayland WAYLAND_DEBUG=1 NEKOTUNE_TEST_NATIVE_WALLPAPER=1 \
  ./build/tests/nekotune_wallpaper_test nativeWallpaper
```

## Windows 10/11

沿用 MSYS2 UCRT64 的 CMake / Ninja / GCC / Qt6 工具链及现有 CI，不查找 LayerShellQt。新增链接仅有系统的 `user32` 和 `comctl32`。

```sh
# 在 MSYS2 UCRT64 shell 中，其他 Node / pnpm 要求仍按 README 配置。
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-declarative \
  mingw-w64-ucrt-x86_64-qt6-multimedia mingw-w64-ucrt-x86_64-qtkeychain \
  mingw-w64-ucrt-x86_64-ffmpeg mingw-w64-ucrt-x86_64-zlib
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DNEKOTUNE_ENABLE_WALLPAPER=ON
cmake --build build -j2
./build/nekotune.exe
```

运行时确认 shell PID、进程映像为系统目录的 Explorer、图标宿主含 `SHELLDLL_DefView / SysListView32`。只选该宿主后方、覆盖目标显示器且没有其他未知子窗口的顶层 WorkerW；有歧义或新版嵌套布局则失败。必要时对经过验证的 Progman 发送一次最长 200 ms 的 `0x052c` 请求，两次请求至少相隔 30 秒。

嵌入前设置 WS_CHILD、去除 WS_POPUP、无边框、NoActivate、Layered 和 Transparent；检查 `SetParent`、样式及几何 API 的结果。零返回值按 `GetLastError` 的接口约定处理。显示器用 `MONITORINFOEX::szDevice` 对应 QScreen 名称，使用原生 monitor rect 并映射到父客户区，避免把负坐标或全局 Qt 坐标统一乘单一 DPR。设置一次 Z-order 后，几何未变化时不重复移动重绘。

跨进程 `SetParent` 可能改变 DPI awareness，参见 [Microsoft 说明](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setparent)。本实现要求原窗口和 Explorer 父窗口的 DPI context 完全相等，否则取消嵌入，不修改 Qt awareness。因此部分正常 Windows 桌面也会被保守拒绝。

原生子类 guard 在父窗口失效或 Qt 重建 HWND 时阻止显示未嵌入窗口。GUI 线程每秒核验句柄、父窗口和显示器；失败销毁该实例，以 1／2／4／8／16／30 秒退避，最多 8 次且总窗口 120 秒，之后需要手动重试。禁用及退出取消恢复；不隐藏或重建图标宿主，不结束 Explorer，不写注册表。

WorkerW 是 Explorer 的内部机制，不是稳定公开 API。当前没有 Windows 真机／构建环境可验收，不能承诺所有 Windows 10/11 版本有效。尤其需要验收混合 DPI、负坐标、Explorer 重启、子窗口渲染和 HWND 重建。

## 动画、频谱及功耗

壁纸只实现播放状态驱动的柔和装饰动画，未实施 PCM 采样、FFT 或频谱 IPC。`WallpaperManager.audioAnalysis` 明确返回 `spectrumAvailable: false`、`source: "none"`、空 `bands` 和原因；设置中的真实频谱不可选。FFmpeg 虽已有 PCM 混音／DSP／QAudioSink 管线，也不等于已有分析接口；Qt 兼容后端同样不提供频谱。本次没有接触音频关键路径或重复解码。

30／60 Hz 是**目标逐字歌词插值更新频率**，分别设置共享 `LyricsClock` 的 33／16 ms interval，仅作用于壁纸实例。Timer 受 Qt 调度限制，不是实测帧率或窗口 FPS 上限。装饰动画仍使用 Qt Quick 原生动画和合成器刷新循环；未改变主界面的全局渲染设置。

`Theme.reducedMotion`、暂停、无歌曲、断线或隐藏会停止插值／装饰动画。背景模糊使用已有 QtQuick.Effects，静态封面为源，不因进度更新重新计算源图。关闭模糊销毁效果 Loader，关闭背景销毁整棵背景子树；软件渲染禁用模糊。

未提供跨合成器的锁屏、显示器睡眠、全屏遮挡或桌面可见性探测。`isVisible` 和 `isExposed` 不等于用户能看到壁纸。遮挡时能否停帧还取决于平台渲染循环；不声称已实现这些情形下的自动低功耗模式。没有测量 CPU／GPU／内存／真实 FPS，本文不提供性能数值。

## Linux 发布包

CI 从固定的 LayerShellQt 6.7.4 源码（`1613a5f61150192d8187989466d4ff961f4ce2c4`）及 ECM 6.26.0 构建，使用与 NekoTune 相同的 Qt SDK。Qt 6.11 的 Wayland 来自 Qt base；构建必须能找到 `Qt6::WaylandClientPrivate`，且 CI 断言生成的 `wallpaper-runtime.json` 中 backend 为 `wayland`。不得复制针对更新系统 Qt 构建的库到较旧 SDK 包中。

Qt 6.11 的私有头文件使用 Wayland 1.24 新增的 `wl_fixes`，Ubuntu 24.04 自带的 Wayland 1.22 无法满足。CI 从校验 SHA-256 的官方 Wayland 1.24.0 发行包构建到临时前缀，统一设置 scanner、pkg-config、CMake 和动态库查找路径；该版本的运行库通过 ELF 依赖收集进入发布包，`COPYING` 保存到 `licenses/Wayland/`。

打包新增 Wayland QPA、xdg-shell、Wayland 图形／装饰／输入设备插件目录，以及 LayerShellQt／QtWayland 的 ELF 依赖闭包；QtQuick.Effects 已随 QtQuick 目录收集。按窗口调用 API 时不需要设置全局 shell 插件环境变量，也不导入 LayerShellQt 的 QML 模块。后端不链接这些 GUI 依赖。

脚本要求构建 manifest 的 Qt 版本匹配 qmake，复制每个 Qt 库时校验与所选 SDK 文件一致。先用 `ldd` 收集依赖闭包，再对程序及插件入口执行 `ldd -r` 检查缺失符号；Qt FFmpeg 的叶子库可能依赖同一插件加载的 SSL stub，不能单独做符号检查。仍要求构建来源一致，不能仅凭相同版本号保证私有 ABI 兼容。IBus 和 Fcitx 插件保持必需且继续做加载冒烟；Fcitx 和 LayerShellQt 许可证随包复制。

```sh
cmake --install build --prefix "$PWD/stage"
uv run --no-project python scripts/ci/package_release.py \
  --stage stage --qt-prefix /path/to/the/build-qt \
  --library-dir /path/to/layershell/lib --plugin-dir /path/to/layershell/plugins \
  --plugin-dir /path/to/fcitx/plugins --fcitx-source /path/to/fcitx-source \
  --layershell-source /path/to/layershell-source --platform linux --version local-check
# 解压到独立目录，再运行；以下脚本均创建隔离数据和 socket。
node scripts/ci/smoke_release.mjs /path/to/extracted-package
# 需要真实支持 layer-shell 的桌面。不会改变 awww 配置。
node scripts/ci/smoke_release.mjs /path/to/extracted-package --wayland
```

默认 smoke 的 offscreen 测试不等于原生验收。`--wayland` 在隔离配置启用启动恢复，检查真实壁纸映射，同时检查独立后端和两种输入法插件。Windows 发布继续使用既有 windeployqt 和 UCRT64 DLL 收集；普通 CI 不自动修改 Explorer 桌面进行原生壁纸验收。

## 验证记录与待验收项

2026-10-19 本机：Arch Linux、Qt 6.11.2、LayerShellQt 6.7.4、Niri fork `26.04 (v26.04-115-g42a2e06e)`、awww 0.12.1；单 eDP-1，1920×1080，scale 1，Normal。此版本信息只描述本次环境。

同日追加黑胶／全屏歌词后，重新完成 ON、OFF 两套全量构建及 CTest，各 24/24 通过。新增自动化覆盖三种样式切换时窗口及原生句柄不变、旧布局释放且不增加 IPC、设置恢复与保存失败、三种样式下的断线／重连／迟到歌词、黑胶暂停／零强度／减少动画／隐藏停转，以及 LRC／KRC 换行。新壁纸 QML 与 `CompactLyrics` 的 qmllint 无诊断。另在同一 Niri 原生 Wayland 环境运行三样式切换，查看 1920×1080 截图；800×600 横屏、720×1280 竖屏采用隔离 offscreen 窗口进行布局测试及截图检查，不等同于实体显示器旋转或热插拔验收。截图位于本地 `build/wallpaper-style-evidence/`，没有新增 Windows 或性能验收结论。

已做的原生测试使用 Wayland QPA、Qt Quick OpenGL（Mesa Intel Graphics RPL-P，Mesa 26.1.7），播放状态来自临时 IPC fixture（没有实际音频输出）。透明、歌词及装饰开启；另测封面背景、模糊开启和关闭后 Loader 释放。协议日志核验 layer=1、anchors=15、exclusive zone=−1、keyboard=0、无 add 操作的空输入 region；同时创建的普通窗口使用 xdg-toplevel。截图 alpha 验证透明区域；关闭后记录 layer surface destroy。awww 查询保持原背景，实际 layer 列表为 Background。此约 2.4 秒的功能测试不是性能测量。

| 本次执行 | 结果与范围 |
| --- | --- |
| `NEKOTUNE_ENABLE_WALLPAPER=ON` 全量构建／CTest | 三个程序构建成功，24/24 项通过；收尾修改后再运行相关窗口、托盘、MPRIS、QML 测试均通过 |
| `NEKOTUNE_ENABLE_WALLPAPER=OFF` 全量构建／CTest | 三个程序构建成功，24/24 项通过；收尾组件测试通过 |
| 缺少 LayerShellQt 的配置 | `CMAKE_DISABLE_FIND_PACKAGE_LayerShellQt=ON` 配置成功，明确报告 backend=disabled |
| 原生 Niri | 壁纸映射／释放、普通窗口隔离、透明 alpha、模糊释放及原有迷你窗口原生测试通过；协议核验空输入区域 |
| QML 与视觉 | 新设置 Quick Test 通过；查看中英文 360px 面板、透明与模糊壁纸截图。qmllint 新壁纸文件无诊断；合并检查仍有 SettingsPage 的 3 条 unqualified access 警告（其中 1 条是新增上下文属性）和 AlbumArt 原有 unused import 信息 |
| Python 打包测试 | 6/6 通过，含 Wayland 依赖缺失和混用 Qt 拒绝 |
| 本地 Linux 验证包 | 用本机一致的 Qt／LayerShellQt 打包，再解压启动；默认 offscreen 与 `--wayland` 两组 smoke 均通过独立后端、统一桌面、独立前端、Fcitx／IBus、SQLite、IPC 及主题导入导出；Wayland 组核验壁纸 mapped 日志，移除独立前端进程后外部后端仍响应 |
| 依赖边界及差异 | `dependency_boundaries.cmake`、`git diff --check` 通过；后端动态依赖没有 Quick、QML、Wayland、LayerShellQt |
| Windows／Ubuntu CI 包 | 已更新代码和 CI 配方；本机未执行 Windows 构建或桌面测试，也未执行远程 Ubuntu CI。不将本机 Arch 验证包结果等同于发布平台验收 |

CMake 的 Qt 私有 ABI 提示为预期警告；OFF 全量构建还显示音频引擎现有 `QAudio::UnderrunError` 弃用警告。打包的 ldd 对数个没有执行位的库打印提示，但依赖／符号检查及解压包启动成功。没有提交、推送或发布 Release。

自动化覆盖启停幂等、共享控制器且不发播放命令、无能力安全失败、保存失败仍可关闭、非法设置、自动恢复与安全模式、指定屏缺失、模拟输出移除、断线重连、迟到歌词、进度／暂停／倍速保留、隐藏停止插值、surface 丢失、恢复取消及退出回调。沿用完整 CTest 覆盖迷你窗口、桌面歌词、托盘、MPRIS 和音频管线。模拟测试不能替代硬件验收。

仍需真实验收：多显示器、混合缩放、旋转、实体热插拔、物理鼠标点击穿透、长时间资源稳定性、锁屏／休眠行为，以及 Windows 的全部原生行为。未实现：真实频谱数据。无法可靠保证：所有 Explorer 版本的 WorkerW 结构、跨合成器遮挡检测、独立窗口硬 FPS 限制。

验证命令：

```sh
cmake -S . -B build -DNEKOTUNE_ENABLE_WALLPAPER=ON
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
cmake -S . -B build/wallpaper-off -DNEKOTUNE_ENABLE_WALLPAPER=OFF
cmake --build build/wallpaper-off -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build/wallpaper-off --output-on-failure
cmake -DSOURCE_DIR="$PWD" -P tests/dependency_boundaries.cmake
uv run --no-project python -m unittest discover -s scripts/ci/tests
git diff --check
```

## 修改文件清单

路径相对于仓库根目录；构建产物与截图不计入源码交付。

| 职责 | 文件 |
| --- | --- |
| 构建／集成 | `CMakeLists.txt`、`frontend/qt-qml/CMakeLists.txt`、`frontend/qt-qml/src/main.cpp`、`frontend/qt-qml/resources.qrc` |
| 管理／平台 | `frontend/qt-qml/src/wallpaper/wallpaper_manager.h`、`wallpaper_manager.cpp`、`wallpaper_platform.h`、`wallpaper_wayland.cpp`、`wallpaper_windows.cpp`、`wallpaper_unavailable.cpp`（后五项同目录） |
| 展示／设置 | `frontend/qt-qml/qml/wallpaper/Wallpaper.qml`、`WallpaperSettings.qml`、`WallpaperTrackInfo.qml`、`VinylWallpaper.qml`、`LyricsWallpaper.qml`、`qmldir`（均在 wallpaper 目录）；`frontend/qt-qml/qml/components/SettingsPanel.qml`、`AlbumArt.qml`、`CompactLyrics.qml`、`LyricsClock.qml`（后四者同 components 目录）；`frontend/qt-qml/qml/pages/SettingsPage.qml` |
| 翻译 | `frontend/qt-qml/i18n/en.json`、`frontend/qt-qml/i18n/zh.json` |
| 测试 | `tests/wallpaper_test.cpp`、`tests/qml/tst_wallpaper_settings.qml`、`tests/qml/tst_compact_lyrics.qml`、`tests/CMakeLists.txt`、`tests/dependency_boundaries.cmake` |
| 发布 | `.github/workflows/ci.yml`、`.gitignore`、`scripts/ci/package_release.py`、`scripts/ci/smoke_release.mjs`、`scripts/ci/tests/test_package_release.py` |
| 文档 | `README.md`、`docs/architecture.md`、`docs/API.md`、`docs/audio-engine.md`、`docs/wallpaper.md` |
