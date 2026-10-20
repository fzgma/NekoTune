# NekoTune 功能待办

更新：2026-10-17 · 基于对代码库（`backend/`、`frontend/qt-qml/`、`extensions/`、`docs/`）的静态扫描整理。

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
| 已完成 | 最近播放时间（`last_played_at`） | 曲库 | S |
| P0 | 收藏（喜欢的歌曲） | 曲库 | S–M |
| 已完成 | 队列「下一首播放」与拖拽重排 | 播放 | M |
| P0 | 睡眠定时器 | 播放 | S |
| P1 | 目录监听自动入库 | 曲库 | M |
| P1 | 歌单拖拽排序与 M3U 导入导出 | 歌单 | M |
| 待平台验收 | 无缝播放（gapless） | 播放 | L |
| P1 | ReplayGain / 响度归一化 | 播放 | M–L |
| P1 | Windows 系统媒体控制（SMTC） | 系统集成 | L |
| P1 | 倍速播放 | 播放 | S–M |
| P1 | 桌面通知 | 系统集成 | S |
| P1 | 数据库备份与恢复 | 数据 | S–M |
| P2 | 评分 | 曲库 | S |
| P2 | 重复歌曲模糊检测 | 曲库 | M |
| P2 | 智能歌单 | 歌单 | M–L |
| P2 | CUE + 整轨文件 | 曲库 | L |
| 待平台验收 | 淡入淡出（crossfade） | 播放 | M（依赖 gapless） |
| 待平台验收 | 迷你模式 / 桌面歌词 | 界面 | M–L |
| P2 | 全局快捷键 | 系统集成 | M |
| P2 | 内嵌标签写回（可选） | 曲库 | M |
| P2 | 扩展 SDK：播放事件订阅 | 扩展 | M |
| 待平台验收 | 均衡器 / 音效 | 播放 | XL |
| P3 | Scrobble（ListenBrainz / Last.fm） | 扩展 | M（依赖 SDK 事件） |
| P3 | WebSocket / HTTP 远程控制与 Web 前端 | 远程 | XL |
| P3 | 扩展 SDK：元数据提供方 | 扩展 | L |

---

## 一、播放

相关代码：`backend/application/playback/player_engine.*`、`backend/domain/playback/playback_backend.h`（`IPlaybackBackend`）、`backend/infrastructure/playback/qt_playback_backend.*`、`backend/application/playback/queue_service.*`、`backend/domain/playback/player_queue.*`。

### 1.1 队列「下一首播放」与拖拽重排 — 已完成

- 已实现 `queue.move(queue_id, position)` 与 `queue.add` 的 `position` / `next` 选项，支持已有歌曲和专辑整批入队。
- 首页、曲库、队列和歌单歌曲菜单支持「下一首播放」「加入队列末尾」；专辑菜单按碟号／音轨号整批插入可用歌曲。
- 队列抽屉和队列页面支持拖拽重排及边缘滚动；搜索结果和歌单视图禁用重排。
- 先提交数据库事务，再发布内存快照；移动当前项只更新位置，保留音频源、播放状态和进度。
- 待播优先标记持久化，随机模式优先播放；成功切换才消耗标记，重启后顺序与待播意图恢复。
- 测试覆盖：队列身份与重复项、前后跨越当前项、失败回滚、不重新加载、重启恢复、随机优先、IPC 校验及 QML 菜单／拖拽。API 与 README 已同步。

### 1.2 睡眠定时器 — P0 · S

- 目标：设置「N 分钟后暂停」或「播完当前曲目后暂停」，托盘菜单与播放栏都有入口。
- 方案：
  - 后端 `PlayerEngine` 旁新增小型 `SleepTimerService`，到期调用既有暂停路径（而非 stop，保留进度）。
  - IPC：`player.sleep_timer.set` / `.cancel` / `.status`，状态通过事件广播，多前端同步。
  - 「播完当前曲目」需在曲目切换事件上判定，不能只靠 `QTimer`。
- 边界：到期时已被用户手动暂停则不重复操作；设备断开等其他暂停原因不得被覆盖。
- 验收：定时器到期暂停；取消后无动作；重启后不恢复（定时器不持久化）。

### 1.3 统一音频播放引擎 / Gapless — P1 · L，待平台验收

- 默认 FFmpeg 解码 + 原生采样率浮点 PCM + 连续重采样 + `QAudioSink` 输出，保留 Qt 与受控回退。
- 候选不消耗随机历史，不提前切换曲目；按设备处理时钟到达边界再提交队列、歌词、MPRIS 和播放次数。
- 覆盖连续波形的逐样本衔接、压缩格式延迟／尾部填充、短曲、seek、取消、预加载失败、在线资源租约和保存失败。
- 自动化状态与剩余真机验收见 `docs/audio-engine.md`；Linux／Windows 实际声卡、拔插与长时间播放尚需平台验收。

### 1.4 ReplayGain / 响度均衡 — P1 · M–L，待平台验收

- 默认关闭，支持单曲／专辑模式和预增益；已有 ReplayGain 标签优先，缺少对应模式数据时回到 0 dB（不套用预增益或沿用上一首）。
- 增益在连续 PCM 链中按曲目处理，Crossfade 两路分别应用，再进入现有 EQ、自动余量和限幅；参数及曲目切换平滑过渡。
- 用户可启动可取消、单任务并发的 EBU R128 后台分析，播放期间降载；结果只写应用缓存，不修改原音频。
- 界面显示当前增益及来源；Qt 后端明确不可用／旁路，保留配置，不使用主音量缩放替代。
- 验收：标签优先级、专辑内相对音量、缺失数据、静音、正增益保护、Crossfade、后端回退；有足够余量的样本响度收敛至 ±1 LU，分析前后原文件哈希相同。
- 已实现 PCM 分路增益、设置／来源显示、单任务分析和缓存；自动化用例及真机验收范围见 `docs/audio-engine.md`。

### 1.5 倍速播放 — P1 · S–M

- 实现状态：已接入全局速度设置、播放栏入口、设置页恢复、MPRIS 只读同步及主／迷你／桌面歌词的速度校准。Qt 6.10+ 且支持音高补偿时可用；旧版保持 1.0×。自动化覆盖七档速度、媒体时间轴、全局持久化、保存失败和多客户端同步；真实音频设备上的音高听感仍需人工验收。
- 目标：0.5×–2.0×，保持音高；播放栏放一个小入口，设置里可恢复 1.0×。
- 方案：`IPlaybackBackend` 增加 `setPlaybackRate`；`QMediaPlayer` 原生支持。MPRIS 的 `Rate` 属性需同步（当前文档写明系统端不处理循环/随机，Rate 可只读展示）。
- 边界：歌词时间轴与进度条要按实际位置而非墙钟计算；倍速不持久化到单曲，只作为全局设置。
- 验收：不同倍速下歌词高亮与进度不漂移。

### 1.6 淡入淡出（crossfade） — P2 · M，待平台验收

- 双路 PCM 等功率混音，0–12 秒、默认关闭，短曲限制为两首各自时长的一半。
- 仅自然换曲启用，严格专辑顺序与单曲循环优先 Gapless；暂停冻结，seek 和手动导航取消过渡。
- 下一首首个非零权重输出时切换身份，旧资源在尾音结束后释放。

### 1.7 均衡器 / 音效 — P3 · XL，待平台验收

- 应用内 10 段 `libavfilter` EQ，支持前级、四组预设、自定义曲线、自动余量和限幅。
- 约 20 ms 参数平滑，不重建输出；Qt 回退期间保留配置，界面按实际能力禁用音效。
- 首批不包含系统音效集成、混响、空间音效或外部插件宿主。

---

## 二、曲库

相关代码：`backend/storage/song_repository.*`、`backend/application/library/library_service.*`、`backend/runtime/library_scanner.*`、`backend/infrastructure/library/music_directory.*`、`frontend/qt-qml/src/controllers/library_controller.*`、`frontend/qt-qml/qml/components/LibraryPanel.qml`。

### 2.1 最近播放时间 — 已完成

- 旧库按列检查新增 `last_played_at TEXT NOT NULL DEFAULT ''`，空时间表示没有可追溯的播放记录。
- 沿用 `PlayerEngine::songStarted` 与 `m_playCounted` 的计数时机，在同一条 SQL 和事务中更新次数及 UTC ISO 8601 毫秒时间。
- 曲库支持「最近播放」倒序排序，未知时间排最后；支持近 7／30 天连续时间窗口筛选，与标签、搜索及来源组合生效，每分钟刷新过期记录。
- 首页最近歌曲区可切换「最近入库／最近播放」，最近播放最多五首，按当前显示顺序发起播放。
- 测试覆盖旧库迁移、重启持久化、统计原子回滚、暂停续播不重复更新、只读预览不更新、IPC 同步与 QML 排序／筛选／首页切换；README 和 API 已同步。

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

### 5.1 迷你模式 / 桌面歌词 — 已实现，待平台实机验收

- 迷你模式替代主窗口显示，提供封面、播放控制、单行逐字歌词和可选置顶；关闭时恢复主界面。
- X11／Windows 桌面歌词显示当前行与下一行，支持透明置顶、拖动缩放和锁定穿透；依赖可用托盘提供解锁入口。Wayland 显示不支持说明，仍可使用迷你模式。
- 共享歌词时钟和行渲染，独立透明外观；保存几何及置顶偏好，重启手动开启且默认解锁。
- 待验收：X11／Windows 原生置顶、透明、点击穿透与混合 DPI；自动化离屏测试不替代平台验收。

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

相关代码：`sdk/include/nekotune/sdk/*`、`backend/infrastructure/extensions/*`、`docs/extensions.md`。

### 7.1 扩展 SDK：播放事件订阅 — P2 · M

- 现状：扩展可提供歌词、音源、主题、命令、界面，但**没有播放事件**。
- 目标：新增只读事件，例如 `player.trackStarted`、`player.trackEnded`（含播放时长与是否播满）、`player.stateChanged`、`library.songAdded`。
- 方案：沿用 `PluginManager::publish` 的事件广播（扩展经 `IExtensionContext::on` 订阅）；新增权限声明（读取播放信息），首次启用时在"完整系统权限"提示里已覆盖，但仍建议在 manifest 中显式声明，便于将来细化权限。
- 价值：这是 Scrobble、状态同步（Discord Rich Presence 类）、自动化的共同前置。
- 文档与示例：`examples/extensions/automation` 补充事件用法。

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
