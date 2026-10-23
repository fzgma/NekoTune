# feat · 功能规划

[返回待办总览](README.md) · 更新：2026-10-22

新增功能与尚未完成的功能扩展。已完成及已放行能力不再重复列入。

保留原条目编号，优先级与工作量说明见[总览](README.md)。

## 清单

| 状态 | 优先级 | 项目 | 分类 | 原估算 |
| --- | --- | --- | --- | --- |
| 待开发 | P0 | 收藏（喜欢的歌曲） | 曲库 | S–M |
| 待开发 | P0 | 宿主睡眠定时器 | 播放 | S |
| 待开发 | P1 | 目录监听自动入库 | 曲库 | M |
| 待开发 | P1 | 歌单拖拽排序 | 歌单 | M |
| 部分实现 | P1 | M3U 导入导出（已有相对路径 M3U8 导出） | 歌单 | M |
| 待开发 | P1 | Windows 系统媒体控制（SMTC） | 系统集成 | L |
| 待开发 | P1 | 桌面通知 | 系统集成 | S |
| 待开发 | P2 | 评分 | 曲库 | S |
| 待开发 | P2 | 重复歌曲模糊检测 | 曲库 | M |
| 待开发 | P2 | 智能歌单 | 歌单 | M–L |
| 待开发 | P2 | CUE + 整轨文件 | 曲库 | L |
| 待开发 | P2 | 全局快捷键 | 系统集成 | M |
| 待开发 | P2 | 内嵌标签写回（可选） | 曲库 | M |
| 待开发 | P3 | Scrobble（ListenBrainz / Last.fm） | 扩展 | M |
| 待开发 | P3 | WebSocket / HTTP 远程控制与 Web 前端 | 远程 | XL |
| 待开发 | P3 | 扩展 SDK：元数据提供方 | 扩展 | L |

## 播放

相关代码：`backend/application/playback/player_engine.*`、`backend/domain/playback/playback_backend.h`（`IPlaybackBackend`）、`backend/infrastructure/playback/qt_playback_backend.*`、`backend/application/playback/queue_service.*`、`backend/domain/playback/player_queue.*`。

### 1.2 宿主睡眠定时器 — P0 · S

- 现状：`examples/extensions/automation` 已演示基于 `QTimer` 的定时暂停命令；宿主统一的设置／取消／状态接口、托盘与播放栏入口仍待开发。
- 目标：设置「N 分钟后暂停」或「播完当前曲目后暂停」，托盘菜单与播放栏都有入口。
- 方案：
  - 后端 `PlayerEngine` 旁新增小型 `SleepTimerService`，到期调用既有暂停路径（而非 stop，保留进度）。
  - IPC：`player.sleep_timer.set` / `.cancel` / `.status`，状态通过事件广播，多前端同步。
  - 「播完当前曲目」需在曲目切换事件上判定，不能只靠 `QTimer`。
- 边界：到期时已被用户手动暂停则不重复操作；设备断开等其他暂停原因不得被覆盖。
- 验收：定时器到期暂停；取消后无动作；重启后不恢复（定时器不持久化）。

---

## 曲库

相关代码：`backend/storage/song_repository.*`、`backend/application/library/library_service.*`、`backend/runtime/library_scanner.*`、`backend/infrastructure/library/music_directory.*`、`frontend/qt-qml/src/controllers/library_controller.*`、`frontend/qt-qml/qml/components/LibraryPanel.qml`。

### 2.2 收藏 — P0 · S–M

- 方案：
  - 新表 `song_favorites(song_id PRIMARY KEY REFERENCES songs(id) ON DELETE CASCADE, created_at)`，或直接作为内置标签处理（简单但会混入用户标签空间，**不推荐**）。
  - IPC：`song.set_favorite`（支持批量 `ids`）；`library.list` 增加 `favorite` 筛选与排序。
  - UI：`TrackRow` 增加心形按钮；侧栏或首页增加「我喜欢的」入口；MPRIS 不涉及。
  - 删除歌曲时随外键清理；扩展在线曲目（无本地歌曲 ID）暂不支持收藏，需在文档中写明。
- 验收：批量收藏/取消、重启保持、删除歌曲后无残留。

### 2.3 目录监听自动入库 — P1 · M

- 现状：`LibraryScanner` 在后端启动与前端连接时检查已登记音频和封面、补全缺失时长，不再发现根目录中的任意文件。新增歌曲必须明确导入，重启不会自动入库。扩展开发目录已有文件监视，与曲库自动入库无关。
- 方案：
  - Linux 使用 inotify（`QFileSystemWatcher` 对大目录树有句柄上限，建议封装递归监听并设上限与回退）；Windows 用 `ReadDirectoryChangesW`，可经 Qt 封装。
  - 去抖：文件写入过程中会连续触发，等待稳定（例如 2 秒无变化且文件大小不再变化）再入库。
  - 监听单独配置的外部导入目录，复用 `ImportExecutor` 的复制、SHA-256 去重和资源发布逻辑；不把内容寻址的 `media/`、暂存及回退目录当作待导入目录。
  - 自动入库改变当前“明确导入”的行为，需设计用户主动开启、监听范围和删除记录保护，再同步存储与使用文档。
  - 外部来源的删除事件不删除记录，也不把已有库内副本标记为不可用；库内对象的缺失仍交给现有资源校验处理。
- 边界：不得修改队列、不得自动播放；重复任务去重，不重复发布同一资源；外部原文件删除不影响已复制到库内的音频。
- 验收：复制／移动／重命名的去抖与入库表现；大量文件同时写入时不卡界面；已删除歌曲不被监听任务自动复活。

### 2.4 重复歌曲模糊检测 — P2 · M

- 现状：仅按音频 SHA-256 去重，同曲不同码率、不同格式、重新编码会被视作不同歌曲。
- 方案：按规范化后的「歌名 + 歌手 + 时长（±2 秒）」分组，复用 `LyricsService::normalizeForMatch` 的归一化。
- UI：曲库增加「疑似重复」视图，展示分组，支持选择保留一首并批量删除其余（沿用现有删除确认与"清理托管文件"选项）。
- 不做自动删除。

### 2.5 评分 — P2 · S

- 在收藏之后做。`songs.rating`（0–5），曲库列与排序支持；可与 POPM/RATING 标签导入互通（见 2.8）。

### 2.6 CUE + 整轨文件 — P2 · L

- 现状：没有 CUE 支持，整轨 FLAC/APE 只能当一首歌播放。
- 难点：音频对象按 SHA-256 去重，歌曲通过 `song_assets` 关联对象；目前没有 CUE 虚拟曲目与起止偏移。分轨需在同一音频对象上表达多个曲目区间。
- 方案草案：
  - 新增 `song_segments` 或等价的曲目区间关联，保存音频对象与 `start_ms`／`end_ms`；保留音频对象按文件哈希去重，虚拟曲目身份单独设计。
  - 播放层需要在曲目边界处理 seek 与结束判定，复用已有 Gapless 引擎的边界机制。
- 在现有播放引擎上扩展虚拟曲目区间，避免另建一套播放核心。

### 2.7 智能歌单 — P2 · M–L

- 现状已有筛选维度：标签（多标签 AND）、入库来源、从未播放、搜索、排序。
- 方案：把筛选条件序列化为规则 JSON，保存在 `smart_playlists` 表；打开时实时执行 `library.list`。已有最近播放筛选，收藏、评分完成后可补充对应条件。
- 前置：2.2 收藏；最近播放能力已具备。

### 2.8 内嵌标签写回（可选） — P2 · M

- 现状：编辑歌曲信息只写数据库，README 明确"不修改音频内嵌标签"。
- 目标：作为**明确选择**的功能，不改变默认行为：
  - 单首／批量“写回标签到文件”应明确选择导出副本或外部文件；当前托管音频是不可变副本，不能原地改写后继续沿用旧哈希及资源关联；
  - 写入失败（只读、权限）逐项报告，不影响数据库。
- 技术：FFmpeg 不适合写标签，建议引入 TagLib；需评估新增依赖对 Linux 发行包和 Windows MSYS2 构建的影响。
- 风险：改写外部文件必须明确确认；改写库内音频须作为新资源重新计算哈希并事务更新关联，考虑共享引用、响度缓存及回滚。默认关闭。

---

## 歌单

相关代码：`backend/storage/playlist_repository.*`、`backend/application/library/playlist_service.*`、`frontend/qt-qml/src/controllers/playlist_controller.*`。

### 3.1 歌单拖拽排序 — P1 · M

- 现状：`playlist_items.position` 已存在，但 API 只有 create/list/rename/delete/remove 与添加，没有移动。
- 方案：`playlist.move`（歌单 ID、歌曲 ID、目标位置）；事务内重写位置；UI 在歌单页支持拖拽，复用已有队列拖拽能力。
- 注意：歌单播放"按当前浏览顺序"，排序后应立即影响播放顺序。

### 3.2 M3U / M3U8 导入导出 — P1 · M，部分实现

- 已实现：`library.export` 接受 `playlist_id`，按歌单顺序导出音频、歌词、封面及相对路径 `playlist.m3u8`；音频字节不变，缺失或纯在线歌曲列入跳过结果，不自动下载。详见[音乐库存储与恢复](../library-storage.md#导出)。
- 待开发：M3U／M3U8 解析与导入；相对路径以清单目录为基准，可用条目走明确的曲库与歌单导入，缺失条目汇总报告。
- 后续验收：导出后重新导入的顺序与歌曲一致；覆盖中文、空格、特殊字符及缺失路径。导入接口需单独设计，当前没有 `playlist.import_m3u`。

---

## 系统集成

相关代码：`frontend/qt-qml/src/platform/`（现仅 `linux/mpris_service.*`）、`frontend/qt-qml/src/platform/system_media_bridge.*`、`system_tray_controller.*`。

### 4.1 Windows 系统媒体控制（SMTC） — P1 · L

- 现状：CI 已发布 Windows 包，但 `platform/` 只有 Linux 实现，Windows 无媒体键面板与锁屏控件，两个平台能力不对等。
- 方案：在 `platform/windows/` 新增实现 `SystemMediaBridge` 的平台部分，使用 `SystemMediaTransportControls`（WinRT / C++/WinRT，MSYS2 UCRT64 工具链兼容性需要先验证；备选是直接用 COM 接口 `ISystemMediaTransportControlsInterop`）。
- 复用：`SystemMediaBridge` 已把"后端确认状态"与"平台协议"分离，Windows 只需实现元数据、播放状态、按钮事件、进度。
- 难点：封面需要提供为 `IRandomAccessStreamReference`；桌面（非 UWP）进程取得 SMTC 需要窗口句柄。
- 验收：媒体键、锁屏/音量浮层显示歌曲信息、进度；失败时仅记录警告，不影响播放（与 MPRIS 策略一致）。
- 文档：README"Linux 系统媒体控制"一节需要拆分为平台说明。

### 4.2 桌面通知 — P1 · S

- 目标：切歌时可选弹出通知（歌名、歌手、封面）；设置里有开关，默认关闭。
- Linux：`org.freedesktop.Notifications`（QDBus，`mpris_service` 已经依赖 Qt DBus）；或 `QSystemTrayIcon::showMessage`（跨平台，但样式简陋）。建议先用后者，Linux 再按需升级。
- 边界：窗口处于前台时不弹；仅"曲目变化"触发，不要在暂停、跳转时弹；快速连续切歌需合并或替换上一条。

### 4.3 全局快捷键 — P2 · M

- 现状：只有窗口内 `Shortcut`；MPRIS/媒体键已覆盖播放、切歌。
- 方案：Wayland 走 `org.freedesktop.portal.GlobalShortcuts`；X11 走 XGrabKey 或 `QHotkey` 类库；Windows 用 `RegisterHotKey`。不同平台能力差异大，建议先限定"可自定义的几个播放控制"。
- 评估：媒体键已足够多数用户，优先级较低。

---

## 扩展与生态

相关代码：`sdk/include/nekotune/sdk/*`、`backend/infrastructure/extensions/*`、`docs/extensions.md`。

### 7.2 Scrobble 扩展（ListenBrainz / Last.fm） — P3 · M

- 作为**独立扩展**实现，不进主项目（符合"主项目不包含第三方音源插件"的现有边界）。
- 规则：播放超过 50% 或 4 分钟才计一次；离线时本地排队重试；凭据走扩展配置的安全存储。
- 前置：宿主事件订阅已具备；需确定供扩展使用的收听时长、跳转与重复播放计数契约，不能直接把曲目变化或位置通知当作一次有效收听。

### 7.3 扩展 SDK：元数据提供方 — P3 · L

- 目标：扩展可提供封面、歌曲资料（歌名/歌手/专辑/标签）候选，与现有歌词提供方机制对称，用于"编辑歌曲信息"里的候选选择，作为 AI 填写之外的非生成式来源。
- 方案：参照 `extension_lyrics_provider.*` 新增 `extension_metadata_provider.*`；IPC 增加 `song.metadata_candidates`。

---

## 远程与多前端（PRD Milestone 4）

### 8.1 WebSocket / HTTP 网关与 Web 前端 — P3 · XL

- 现状：IPC 仅本地 Socket；PRD 目标是"前端可替换"，其中包括 Web 与远程控制。
- 方案：
  - 在后端旁增加可选网关进程/模块，把本地 Socket 协议桥接为 WebSocket（请求/响应/事件语义不变）。
  - 默认**关闭**，仅监听回环地址；开启局域网访问必须设置 token，并建议置于反向代理/TLS 之后。
  - 命令白名单：远程默认只开放播放控制与浏览，不开放 `library.delete`、`extensions.*`（扩展拥有完整系统权限，远程暴露风险极高）、AI 密钥相关接口。
  - Web 前端另起目录，以静态页面形式由网关提供。
- 安全是该项的主要工作量，应在设计阶段单独评审，再决定是否实施。

---
