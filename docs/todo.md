# NekoTune 功能待办

更新：2026-10-16 · 基于对代码库（`backend/`、`frontend/qt-qml/`、`extensions/`、`docs/`）的静态扫描整理。

依据来自 grep 与阅读代码，未逐项实机验证；动手前请先复核对应模块的当前状态。

## 优先级说明

| 级别 | 含义 |
| --- | --- |
| P0 | 基础体验缺口，改动小、收益直接，建议优先完成 |
| P1 | 明显提升日常使用体验，或补齐平台一致性 |
| P2 | 增强型功能，需要较大设计或新依赖 |
| P3 | 生态与长期规划，依赖前面能力成熟 |

工作量：S ≈ 1–2 天，M ≈ 3–5 天，L ≈ 1–2 周，XL ≈ 2 周以上。

## 总览（按优先级）

| 优先级 | 项目 | 分类 | 工作量 |
| --- | --- | --- | --- |
| P0 | 最近播放时间（`last_played_at`） | 曲库 | S |
| P0 | 收藏（喜欢的歌曲） | 曲库 | S–M |
| P0 | 队列「下一首播放」与拖拽重排 | 播放 | M |
| P0 | 睡眠定时器 | 播放 | S |
| P1 | 目录监听自动入库 | 曲库 | M |
| P1 | 歌单拖拽排序与 M3U 导入导出 | 歌单 | M |
| P1 | 无缝播放（gapless） | 播放 | L |
| P1 | ReplayGain / 响度归一化 | 播放 | M–L |
| P1 | Windows 系统媒体控制（SMTC） | 系统集成 | L |
| P1 | 倍速播放 | 播放 | S–M |
| P1 | 桌面通知 | 系统集成 | S |
| P1 | 数据库备份与恢复 | 数据 | S–M |
| P2 | 评分 | 曲库 | S |
| P2 | 重复歌曲模糊检测 | 曲库 | M |
| P2 | 智能歌单 | 歌单 | M–L |
| P2 | CUE + 整轨文件 | 曲库 | L |
| P2 | 淡入淡出（crossfade） | 播放 | M（依赖 gapless） |
| P2 | 迷你模式 / 桌面歌词 | 界面 | M–L |
| P2 | 全局快捷键 | 系统集成 | M |
| P2 | 内嵌标签写回（可选） | 曲库 | M |
| P2 | 扩展 SDK：播放事件订阅 | 扩展 | M |
| P3 | 均衡器 / 音效 | 播放 | XL |
| P3 | Scrobble（ListenBrainz / Last.fm） | 扩展 | M（依赖 SDK 事件） |
| P3 | WebSocket / HTTP 远程控制与 Web 前端 | 远程 | XL |
| P3 | 扩展 SDK：元数据提供方 | 扩展 | L |

---

## 一、播放

相关代码：`backend/application/playback/player_engine.*`、`backend/domain/playback/playback_backend.h`（`IPlaybackBackend`）、`backend/infrastructure/playback/qt_playback_backend.*`、`backend/application/playback/queue_service.*`、`backend/domain/playback/player_queue.*`。

### 1.1 队列「下一首播放」与拖拽重排 — P0 · M

- 现状：队列 API 只有 `queue.add`、`queue.play`、`queue.remove`、`queue.clear`、`queue.status`，没有移动与插入位置。
- 目标：
  - 歌曲行与专辑行的更多菜单增加「下一首播放」和「加入队列末尾」。
  - 队列抽屉支持拖拽重排。
- 方案：
  - 新增 IPC `queue.move`（参数：`queue_id`、目标位置）和 `queue.add` 的 `position`/`next` 选项。
  - `PlayerQueue` 增加 `move` 与 `insertAfterCurrent`，保持「歌曲 ID 与队列项 ID 分开」的既有约定。
  - `QueueService` 先写库、后更新内存；`queue_items.position` 是主键，重排需在事务内整体重写位置。
  - 当前项被移动时，`current_index` 必须跟随，不能触发重新加载；这与 `PlayerEngine` 中「队列通知不得被解释为外部选择」的既有处理相关，需沿用。
- 验收：移动当前项、移动到当前项前后、重复歌曲项各有一个用例；重启后顺序保持；随机模式下「下一首播放」优先于随机序列。
- 测试：扩展 `tests/player_queue_test.cpp`、`tests/playback_order_test.cpp`，QML 侧加拖拽用例。
- 文档：更新 `docs/API.md` 队列章节。

### 1.2 睡眠定时器 — P0 · S

- 目标：设置「N 分钟后暂停」或「播完当前曲目后暂停」，托盘菜单与播放栏都有入口。
- 方案：
  - 后端 `PlayerEngine` 旁新增小型 `SleepTimerService`，到期调用既有暂停路径（而非 stop，保留进度）。
  - IPC：`player.sleep_timer.set` / `.cancel` / `.status`，状态通过事件广播，多前端同步。
  - 「播完当前曲目」需在曲目切换事件上判定，不能只靠 `QTimer`。
- 边界：到期时已被用户手动暂停则不重复操作；设备断开等其他暂停原因不得被覆盖。
- 验收：定时器到期暂停；取消后无动作；重启后不恢复（定时器不持久化）。

### 1.3 无缝播放（gapless） — P1 · L

- 现状：只有 `QtPlaybackBackend`，换曲时重新加载媒体源，专辑连播有间隙。
- 方案选项：
  1. 在 `QtPlaybackBackend` 内用双 `QMediaPlayer` 预加载下一首并在末尾切换：改动小，但精度受 Qt 后端限制，不一定真正无缝。
  2. 新增基于 FFmpeg 解码 + `QAudioSink` 的 `IPlaybackBackend` 实现：可控制样本级衔接、同时为 ReplayGain、倍速、EQ 打基础，但要自己处理 seek、设备切换和 Pulse 端口逻辑（现有 `PulseAudioPorts` 与 `AudioOutputService` 需适配）。
- 建议：先做原型验证方案 1 的实际间隙，不够再走方案 2。若后续确定要做 EQ，直接走方案 2。
- 接口：`IPlaybackBackend` 增加 `preloadNext(source)` 与 `nextTrackStarted` 信号；`PlayerEngine` 在接近曲末时请求预加载，并保证播放次数、MPRIS 元数据、歌词切换仍按「实际切换时刻」更新。
- 风险：在线音源（扩展提供）的解析是异步的，预加载可能失败，需回退到普通切歌；随机与单曲循环模式下"下一首"的判定要与 `PlaybackOrderService` 保持一致。
- 验收：用无静音拼接的测试音频（连续正弦波）测量切换间隙；设备断开、seek、停止时预加载状态被清理。

### 1.4 ReplayGain / 响度归一化 — P1 · M–L

- 现状：没有任何音量归一化。
- 方案：
  - 读取：入库时通过 FFmpeg 读取 `REPLAYGAIN_TRACK_GAIN` / `ALBUM_GAIN` 标签（`audio_duration.cpp`、`music_directory.cpp` 已经在读取内嵌资料，可顺带处理）。
  - 无标签时：后台任务用 EBU R128 估算响度，结果写入数据库新列（`loudness_lufs`、`track_gain_db`），不修改音频文件。
  - 应用：增益作用到后端音量。Qt 后端只能通过缩放音量实现，需限制峰值防止削波（预增益上限 + 峰值保护）。
  - 设置项：关闭 / 单曲 / 专辑；预增益。
- 依赖：若只靠 Qt 音量缩放，正增益有削波风险，完整方案依赖 1.3 的自有解码链路。
- 验收：对同一批不同响度样本，开启后输出响度差在 ±1 LU；专辑模式保留专辑内相对动态。

### 1.5 倍速播放 — P1 · S–M

- 目标：0.5×–2.0×，保持音高；播放栏放一个小入口，设置里可恢复 1.0×。
- 方案：`IPlaybackBackend` 增加 `setPlaybackRate`；`QMediaPlayer` 原生支持。MPRIS 的 `Rate` 属性需同步（当前文档写明系统端不处理循环/随机，Rate 可只读展示）。
- 边界：歌词时间轴与进度条要按实际位置而非墙钟计算；倍速不持久化到单曲，只作为全局设置。
- 验收：不同倍速下歌词高亮与进度不漂移。

### 1.6 淡入淡出（crossfade） — P2 · M

- 依赖 1.3 的双路径播放（两路同时输出并混音）。
- 设置项：淡入淡出时长（0 为关闭），专辑连播内自动跳过淡出。

### 1.7 均衡器 / 音效 — P3 · XL

- 方案选项：
  1. 走 PipeWire filter-chain，由用户在系统层配置；NekoTune 仅提供入口说明。成本最低，但 Windows 无法使用。
  2. 在自有 FFmpeg 解码链路里加入 DSP（`libavfilter` 的 `equalizer`/`superequalizer`）；跨平台，工作量大。
- 前置：1.3 方案 2。不建议在 Qt Multimedia 后端上硬做。

---

## 二、曲库

相关代码：`backend/storage/song_repository.*`、`backend/application/library/library_service.*`、`backend/runtime/library_scanner.*`、`backend/infrastructure/library/music_directory.*`、`frontend/qt-qml/src/controllers/library_controller.*`、`frontend/qt-qml/qml/components/LibraryPanel.qml`。

### 2.1 最近播放时间 — P0 · S

- 现状：`songs` 表有播放次数（`incrementPlayCount`），没有最后播放时间。
- 目标：新增 `last_played_at`，曲库排序与首页「最近播放」使用。
- 方案：
  - 数据库迁移（参照 `database_session.cpp` 现有 `PRAGMA table_info` 式的补列逻辑）：`ALTER TABLE songs ADD COLUMN last_played_at TEXT NOT NULL DEFAULT ''`。
  - 在 `player_engine.cpp` 计入播放次数的同一时刻（`m_playCounted`）写入，保证两者一致。
  - 曲库排序增加「最近播放」；筛选增加「近 7 天 / 30 天播放过」。
  - 首页新增「最近播放」区块，或把现有「最近加入」做成可切换。
- 验收：播放满足计数条件后时间更新；仅切换页面、搜索、预览不更新；旧库迁移后默认值为空且排在最后。
- 文档：README 功能表、`docs/API.md` 歌曲字段。

### 2.2 收藏 — P0 · S–M

- 方案：
  - 新表 `song_favorites(song_id PRIMARY KEY REFERENCES songs(id) ON DELETE CASCADE, created_at)`，或直接作为内置标签处理（简单但会混入用户标签空间，**不推荐**）。
  - IPC：`song.set_favorite`（支持批量 `ids`）；`library.list` 增加 `favorite` 筛选与排序。
  - UI：`TrackRow` 增加心形按钮；侧栏或首页增加「我喜欢的」入口；MPRIS 不涉及。
  - 删除歌曲时随外键清理；扩展在线曲目（无本地歌曲 ID）暂不支持收藏，需在文档中写明。
- 验收：批量收藏/取消、重启保持、删除歌曲后无残留。

### 2.3 目录监听自动入库 — P1 · M

- 现状：没有 `QFileSystemWatcher`，扫描仅在后端启动与前端连接时触发（`library_scanner.cpp`），新增文件需要手动导入或重启。
- 方案：
  - Linux 使用 inotify（`QFileSystemWatcher` 对大目录树有句柄上限，建议封装递归监听并设上限与回退）；Windows 用 `ReadDirectoryChangesW`，可经 Qt 封装。
  - 去抖：文件写入过程中会连续触发，等待稳定（例如 2 秒无变化且文件大小不再变化）再入库。
  - 复用现有扫描与 SHA-256 去重逻辑，只对变更路径做增量，不做全量重扫。
  - 监听范围：音乐根目录。是否支持额外的用户监听目录，另行决定（涉及设置 UI 与 `scan_ignored` 语义）。
  - 删除事件仅标记不可用（保持"原音频离线保留资料"的既有策略），不自动删除记录。
- 边界：不得修改队列、不得自动播放（与 README 已声明的扫描语义一致）；扫描进行中不重复启动。
- 验收：复制/移动/重命名/删除文件的入库表现；大量文件同时写入时不卡界面；`scan_ignored` 的已删除歌曲不被复活。

### 2.4 重复歌曲模糊检测 — P2 · M

- 现状：仅按音频 SHA-256 去重，同曲不同码率、不同格式、重新编码会被视作不同歌曲。
- 方案：按规范化后的「歌名 + 歌手 + 时长（±2 秒）」分组，复用 `LyricsService::normalizeForMatch` 的归一化。
- UI：曲库增加「疑似重复」视图，展示分组，支持选择保留一首并批量删除其余（沿用现有删除确认与"清理托管文件"选项）。
- 不做自动删除。

### 2.5 评分 — P2 · S

- 在收藏之后做。`songs.rating`（0–5），曲库列与排序支持；可与 POPM/RATING 标签导入互通（见 2.8）。

### 2.6 CUE + 整轨文件 — P2 · L

- 现状：没有 CUE 支持，整轨 FLAC/APE 只能当一首歌播放。
- 难点：现有数据模型是"一个文件 = 一首歌（SHA-256 唯一）"，CUE 分轨需要"同一文件多个虚拟曲目 + 起止偏移"。
- 方案草案：
  - 新增 `song_segments` 或在 `songs` 增加 `start_ms`/`end_ms` 与 `source_file_hash`；哈希唯一性改为 (文件哈希, 起点)。
  - 播放层需要在曲目边界处理 seek 与结束判定（与 1.3 gapless 一同设计更合理）。
- 建议在 1.3 之后再启动，避免两次改动播放核心。

### 2.7 智能歌单 — P2 · M–L

- 现状已有筛选维度：标签（多标签 AND）、入库来源、从未播放、搜索、排序。
- 方案：把筛选条件序列化为规则 JSON，保存在 `smart_playlists` 表；打开时实时执行 `library.list`。可选条件在 2.1、2.2、2.5 完成后明显丰富（最近播放、收藏、评分）。
- 前置：2.1、2.2。

### 2.8 内嵌标签写回（可选） — P2 · M

- 现状：编辑歌曲信息只写数据库，README 明确"不修改音频内嵌标签"。
- 目标：作为**明确选择**的功能，不改变默认行为：
  - 单首/批量"写回标签到文件"，外部导入的软链接指向用户原文件，必须二次确认；
  - 写入失败（只读、权限）逐项报告，不影响数据库。
- 技术：FFmpeg 不适合写标签，建议引入 TagLib；需评估新增依赖对 Linux 发行包和 Windows MSYS2 构建的影响。
- 风险：直接修改用户原文件，不可逆；默认关闭，写回前可选备份。

---

## 三、歌单

相关代码：`backend/storage/playlist_repository.*`、`backend/application/library/playlist_service.*`、`frontend/qt-qml/src/controllers/playlist_controller.*`。

### 3.1 歌单拖拽排序 — P1 · M

- 现状：`playlist_items.position` 已存在，但 API 只有 create/list/rename/delete/remove 与添加，没有移动。
- 方案：`playlist.move`（歌单 ID、歌曲 ID、目标位置）；事务内重写位置；UI 在歌单页支持拖拽，与 1.1 的队列拖拽共用拖拽组件。
- 注意：歌单播放"按当前浏览顺序"，排序后应立即影响播放顺序。

### 3.2 M3U / M3U8 导入导出 — P1 · M

- 导出：歌单 → `.m3u8`（UTF-8，`#EXTINF` 含时长与"歌手 - 歌名"），路径写绝对路径；托管目录下的软链接按链接路径还是真实路径写出，需要决定（建议写真实音频路径，便于其他播放器使用）。
- 导入：解析条目 → 路径存在则走现有"歌单导入音乐"流程（加入曲库与该歌单）；相对路径以 m3u 所在目录为基准；缺失条目汇总报告而不是静默跳过。
- 入口：歌单页更多菜单；IPC `playlist.export` / `playlist.import_m3u`。
- 验收：往返导出再导入，顺序与歌曲一致；含中文、空格、特殊字符的路径。

---

## 四、系统集成

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

## 五、界面

### 5.1 迷你模式 / 桌面歌词 — P2 · M–L

- 迷你模式：小窗口，封面 + 播放控制 + 当前歌词一行，置顶可选。复用 `LrcLyrics.qml` 的逐字高亮。
- 桌面歌词：无边框透明置顶窗口；Wayland 下置顶/点击穿透受合成器限制（需要 layer-shell 或放弃），先实现 X11/Windows，Wayland 给出说明。
- 前置：现有 `ThemeBackground`、`Theme` 已有深浅配色，需要做独立的透明窗口样式。

---

## 六、数据与可靠性

### 6.1 数据库备份与恢复 — P1 · S–M

- 现状：曲库资料、歌单、标签、队列、自定义歌词全部在单个 SQLite 里，损坏或误操作后没有恢复手段。
- 方案：
  - 设置页"导出备份"：用 SQLite `VACUUM INTO`（或 backup API）生成一致性快照，附带 `settings.json`（**不含**密钥库中的 Key）。
  - "恢复备份"：校验 schema 版本后在退出/重启时替换，并保留原文件为 `.bak`。
  - 可选：每次迁移前自动备份。
- 注意：备份文件内含本机绝对路径，跨机恢复后音频会显示不可用；文档中说明。

---

## 七、扩展与生态

相关代码：`extensions/sdk/index.d.ts`、`extensions/runtime/*`、`backend/infrastructure/extensions/*`、`docs/extensions.md`。

### 7.1 扩展 SDK：播放事件订阅 — P2 · M

- 现状：扩展可提供歌词、音源、主题、命令、界面，但**没有播放事件**。
- 目标：新增只读事件，例如 `player.trackStarted`、`player.trackEnded`（含播放时长与是否播满）、`player.stateChanged`、`library.songAdded`。
- 方案：沿用现有 `wire.mjs` 事件通道；新增权限声明（读取播放信息），首次启用时在"完整系统权限"提示里已覆盖，但仍建议在 manifest 中显式声明，便于将来细化权限。
- 价值：这是 Scrobble、状态同步（Discord Rich Presence 类）、自动化的共同前置。
- 文档与示例：`extensions/examples/automation` 补充事件用法。

### 7.2 Scrobble 扩展（ListenBrainz / Last.fm） — P3 · M

- 作为**独立扩展**实现，不进主项目（符合"主项目不包含第三方音源插件"的现有边界）。
- 规则：播放超过 50% 或 4 分钟才计一次；离线时本地排队重试；凭据走扩展配置的安全存储。
- 前置：7.1。

### 7.3 扩展 SDK：元数据提供方 — P3 · L

- 目标：扩展可提供封面、歌曲资料（歌名/歌手/专辑/标签）候选，与现有歌词提供方机制对称，用于"编辑歌曲信息"里的候选选择，作为 AI 填写之外的非生成式来源。
- 方案：参照 `extension_lyrics_provider.*` 新增 `extension_metadata_provider.*`；IPC 增加 `song.metadata_candidates`。

---

## 八、远程与多前端（PRD Milestone 4）

### 8.1 WebSocket / HTTP 网关与 Web 前端 — P3 · XL

- 现状：IPC 仅本地 Socket；PRD 目标是"前端可替换"，其中包括 Web 与远程控制。
- 方案：
  - 在后端旁增加可选网关进程/模块，把本地 Socket 协议桥接为 WebSocket（请求/响应/事件语义不变）。
  - 默认**关闭**，仅监听回环地址；开启局域网访问必须设置 token，并建议置于反向代理/TLS 之后。
  - 命令白名单：远程默认只开放播放控制与浏览，不开放 `library.delete`、`extensions.*`（扩展拥有完整系统权限，远程暴露风险极高）、AI 密钥相关接口。
  - Web 前端另起目录，以静态页面形式由网关提供。
- 安全是该项的主要工作量，应在设计阶段单独评审，再决定是否实施。

---

## 建议的实施顺序

1. **第一批（P0，约 1–2 周）**：2.1 最近播放 → 2.2 收藏 → 1.1 队列重排 → 1.2 睡眠定时器。共享迁移与 API 文档改动，可合并为连续几个小 PR；首页因此可增加"最近播放""我喜欢的"区块。
2. **第二批（P1 曲库/歌单/数据）**：2.3 目录监听 → 3.1 歌单排序 → 3.2 M3U → 6.1 备份。
3. **第三批（P1 播放核心）**：先做 1.3 的原型验证，再决定是否换用自有解码链路；之后 1.4 ReplayGain、1.5 倍速。
4. **第四批（平台）**：4.1 Windows SMTC、4.2 通知。可与第三批并行，因为不触碰播放核心。
5. **之后**：按需推进 P2；P3 在 SDK 事件（7.1）落地后再评估。

## 通用要求

- 每项功能附带自动化测试（C++ 单元测试 + `tests/qml` 的 QML 测试），新增 IPC 方法同步更新 `docs/API.md`，面向用户的行为变化同步更新 README 功能表。
- 数据库变更遵循现有"补列式迁移"，必须能打开旧库，且迁移失败不得破坏原数据。
- 文案同时更新 `frontend/qt-qml/i18n/zh.json` 与 `en.json`。
- 任何会修改用户原始文件或对外暴露接口的功能（2.8、8.1）默认关闭，并在设置中给出清晰说明。
