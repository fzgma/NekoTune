# NekoTune 待办

更新：2026-10-22 · 基于主仓库 `backend/`、`frontend/qt-qml/`、`sdk/`、`examples/extensions/` 与专题文档的静态核对，并合并同日代码审查发现的缺陷、性能与工程化问题。

本文统一记录功能规划（一至八）与质量改进（九至十一），区分待开发、待修复、部分实现、已实现和待平台验收；优先级只表示实施顺序。已实现依据来自源码与现有测试用例，本次未重新运行功能测试或逐项实机验收。当前使用方法见[项目 README](../README.md)，专题入口见[文档导航](README.md)。

## 优先级说明

| 级别 | 含义 |
| --- | --- |
| P0 | 基础体验缺口，或可能卡死、崩溃、绕过授权的缺陷；改动小、收益直接，建议优先完成 |
| P1 | 明显提升日常使用体验、补齐平台一致性，或影响数据一致性与安全加固 |
| P2 | 增强型功能、性能优化，需要较大设计或新依赖 |
| P3 | 生态与长期规划、结构性重构，依赖前面能力成熟 |

工作量：S ≈ 1–2 天，M ≈ 3–5 天，L ≈ 1–2 周，XL ≈ 2 周以上。

## 状态总览

“待平台验收”表示源码已有实现，仍有专题文档列出的真实设备检查；“部分实现”仅表示已有部分能力；“待修复”为代码审查发现的问题，标注“已复核”的条目已对照源码确认，其余动手前需先复核。工作量保留原规划估算，不代表剩余工期。

| 状态 | 优先级 | 项目 | 分类 | 原估算 |
| --- | --- | --- | --- | --- |
| 已实现 | — | 最近播放时间（`last_played_at`） | 曲库 | S |
| 待开发 | P0 | 收藏（喜欢的歌曲） | 曲库 | S–M |
| 已实现 | — | 队列「下一首播放」与拖拽重排 | 播放 | M |
| 待开发 | P0 | 宿主睡眠定时器 | 播放 | S |
| 待开发 | P1 | 目录监听自动入库 | 曲库 | M |
| 待开发 | P1 | 歌单拖拽排序 | 歌单 | M |
| 部分实现 | P1 | M3U 导入导出（已有相对路径 M3U8 导出） | 歌单 | M |
| 待平台验收 | P1 | 无缝播放（gapless） | 播放 | L |
| 待平台验收 | P1 | ReplayGain / 响度均衡 | 播放 | M–L |
| 待开发 | P1 | Windows 系统媒体控制（SMTC） | 系统集成 | L |
| 待平台验收 | P1 | 倍速播放（Qt 后端） | 播放 | S–M |
| 待开发 | P1 | 桌面通知 | 系统集成 | S |
| 已实现 | — | 音乐库目录备份与恢复 | 数据 | S–M |
| 待开发 | P2 | 评分 | 曲库 | S |
| 待开发 | P2 | 重复歌曲模糊检测 | 曲库 | M |
| 待开发 | P2 | 智能歌单 | 歌单 | M–L |
| 待开发 | P2 | CUE + 整轨文件 | 曲库 | L |
| 待平台验收 | P2 | 淡入淡出（crossfade） | 播放 | M |
| 待平台验收 | P2 | 迷你模式 / 桌面歌词 | 界面 | M–L |
| 待开发 | P2 | 全局快捷键 | 系统集成 | M |
| 待开发 | P2 | 内嵌标签写回（可选） | 曲库 | M |
| 已实现 | — | 扩展 SDK：宿主播放事件订阅 | 扩展 | M |
| 待平台验收 | P3 | 均衡器 / 音效 | 播放 | XL |
| 待开发 | P3 | Scrobble（ListenBrainz / Last.fm） | 扩展 | M |
| 待开发 | P3 | WebSocket / HTTP 远程控制与 Web 前端 | 远程 | XL |
| 待开发 | P3 | 扩展 SDK：元数据提供方 | 扩展 | L |
| 待修复 | P0 | 解码线程取消丢失唤醒 | 缺陷 | S |
| 待修复 | P0 | Crossfade 尾部 0／1 帧边界 | 缺陷 | S |
| 待修复 | P0 | 包类型变更沿用信任 | 安全 | S |
| 待修复 | P0 | `AudioProxy::revoke` 遍历中删除 | 缺陷 | S |
| 待修复 | P0 | 恢复回滚与新会话顺序 | 数据 | S–M |
| 待修复 | P1 | 音频代理重定向与下载上限 | 安全 | S–M |
| 待修复 | P1 | 导出目标路径规范化校验 | 安全 | S |
| 待修复 | P1 | 导入／恢复孤儿对象与事务边界 | 数据 | M |
| 待修复 | P1 | 切换输出设备丢失预加载 | 播放 | S |
| 待修复 | P1 | 扩展崩溃归因使用全局状态 | 缺陷 | S |
| 待开发 | P1 | 编译警告、Sanitizer 与静态分析 | 工程化 | M |
| 待开发 | P2 | QML 模块化与编译期检查 | 性能 | M–L |
| 待开发 | P2 | 曲库多选与列表模型性能 | 性能 | S–M |
| 待开发 | P2 | 页面按需加载 | 性能 | S |
| 待开发 | P2 | i18n 回退与统一翻译入口 | 界面 | S |
| 待开发 | P2 | 键盘与读屏可访问性 | 界面 | S–M |
| 待开发 | P2 | CI 依赖缓存与发布流程加固 | 工程化 | S–M |
| 待开发 | P3 | 测试拆分与覆盖补齐 | 工程化 | M |
| 待开发 | P3 | 超大模块拆分与分层收敛 | 工程化 | L |

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

### 1.2 宿主睡眠定时器 — P0 · S

- 现状：`examples/extensions/automation` 已演示基于 `QTimer` 的定时暂停命令；宿主统一的设置／取消／状态接口、托盘与播放栏入口仍待开发。
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
- 自动化状态与剩余真机验收见[音频引擎](audio-engine.md)；Linux／Windows 实际声卡、拔插与长时间播放尚需平台验收。

### 1.4 ReplayGain / 响度均衡 — P1 · M–L，待平台验收

- 默认关闭，支持单曲／专辑模式和预增益；已有 ReplayGain 标签优先，缺少对应模式数据时回到 0 dB（不套用预增益或沿用上一首）。
- 增益在连续 PCM 链中按曲目处理，Crossfade 两路分别应用，再进入现有 EQ、自动余量和限幅；参数及曲目切换平滑过渡。
- 用户可启动可取消、单任务并发的 EBU R128 后台分析，播放期间降载；结果只写应用缓存，不修改原音频。
- 界面显示当前增益及来源；Qt 后端明确不可用／旁路，保留配置，不使用主音量缩放替代。
- 验收：标签优先级、专辑内相对音量、缺失数据、静音、正增益保护、Crossfade、后端回退；有足够余量的样本响度收敛至 ±1 LU，分析前后原文件哈希相同。
- 已实现 PCM 分路增益、设置／来源显示、单任务分析和缓存；自动化用例及真机验收范围见[音频引擎](audio-engine.md)。

### 1.5 倍速播放 — P1 · S–M，待平台验收

- 实现状态：已接入全局速度设置、播放栏入口、设置页恢复、MPRIS 只读同步及主／迷你／桌面歌词的速度校准。Qt 6.10+ 且支持音高补偿时可用；旧版保持 1.0×。自动化覆盖七档速度、媒体时间轴、全局持久化、保存失败和多客户端同步；真实音频设备上的音高听感仍需人工验收。
- 目标：0.5×–2.0×，保持音高；播放栏放一个小入口，设置里可恢复 1.0×。
- 后端边界：`IPlaybackBackend::setPlaybackRate` 已接入 Qt；FFmpeg 当前仅支持 1.0×，保留原速度偏好。MPRIS 只读展示实际 `Rate`。
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
  - 播放层需要在曲目边界处理 seek 与结束判定（与 1.3 gapless 一同设计更合理）。
- 建议在 1.3 之后再启动，避免两次改动播放核心。

### 2.7 智能歌单 — P2 · M–L

- 现状已有筛选维度：标签（多标签 AND）、入库来源、从未播放、搜索、排序。
- 方案：把筛选条件序列化为规则 JSON，保存在 `smart_playlists` 表；打开时实时执行 `library.list`。可选条件在 2.1、2.2、2.5 完成后明显丰富（最近播放、收藏、评分）。
- 前置：2.1、2.2。

### 2.8 内嵌标签写回（可选） — P2 · M

- 现状：编辑歌曲信息只写数据库，README 明确"不修改音频内嵌标签"。
- 目标：作为**明确选择**的功能，不改变默认行为：
  - 单首／批量“写回标签到文件”应明确选择导出副本或外部文件；当前托管音频是不可变副本，不能原地改写后继续沿用旧哈希及资源关联；
  - 写入失败（只读、权限）逐项报告，不影响数据库。
- 技术：FFmpeg 不适合写标签，建议引入 TagLib；需评估新增依赖对 Linux 发行包和 Windows MSYS2 构建的影响。
- 风险：改写外部文件必须明确确认；改写库内音频须作为新资源重新计算哈希并事务更新关联，考虑共享引用、响度缓存及回滚。默认关闭。

---

## 三、歌单

相关代码：`backend/storage/playlist_repository.*`、`backend/application/library/playlist_service.*`、`frontend/qt-qml/src/controllers/playlist_controller.*`。

### 3.1 歌单拖拽排序 — P1 · M

- 现状：`playlist_items.position` 已存在，但 API 只有 create/list/rename/delete/remove 与添加，没有移动。
- 方案：`playlist.move`（歌单 ID、歌曲 ID、目标位置）；事务内重写位置；UI 在歌单页支持拖拽，与 1.1 的队列拖拽共用拖拽组件。
- 注意：歌单播放"按当前浏览顺序"，排序后应立即影响播放顺序。

### 3.2 M3U / M3U8 导入导出 — P1 · M，部分实现

- 已实现：`library.export` 接受 `playlist_id`，按歌单顺序导出音频、歌词、封面及相对路径 `playlist.m3u8`；音频字节不变，缺失或纯在线歌曲列入跳过结果，不自动下载。详见[音乐库存储与恢复](library-storage.md#导出)。
- 待开发：M3U／M3U8 解析与导入；相对路径以清单目录为基准，可用条目走明确的曲库与歌单导入，缺失条目汇总报告。
- 后续验收：导出后重新导入的顺序与歌曲一致；覆盖中文、空格、特殊字符及缺失路径。导入接口需单独设计，当前没有 `playlist.import_m3u`。

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

### 6.1 音乐库目录备份与恢复 — 已实现

- `LibraryMaintenance` 已提供 `library.backup`、`library.restore_preview`、`library.restore` 及任务查询／取消；设置页可操作。
- 备份为 SQLite 一致快照、音频、封面及带校验和的清单，包含歌单、队列、标签、歌词与历史；不备份设置、插件、密钥及可重建缓存。待迁移外部引用先处理。
- 恢复先完整预览校验，再暂存、停止播放及关闭数据库、切换文件并重建后端会话；保留恢复前副本，失败或启动发现未完成切换时回退。
- 数据布局、失败边界、回归入口和手工验收清单统一见[音乐库存储与恢复](library-storage.md)。本次仅核对实现，未重新执行恢复操作。

---

## 七、扩展与生态

相关代码：`sdk/include/nekotune/sdk/*`、`backend/infrastructure/extensions/*`、`docs/extensions.md`。

### 7.1 扩展 SDK：宿主播放事件订阅 — 已实现

- `IExtensionContext::on` 已支持按名称或通配符订阅宿主事件；`BackendSession` 将非扩展类的 IPC 事件转发给 `PluginManager::publish`。
- 可订阅 `player.track_changed`、`player.state_changed`、`player.position_changed` 等现有事件，名称与字段见 [API 事件列表](API.md#事件)。`examples/extensions/automation` 已展示曲目变化订阅。
- 尚无专用于 Scrobble 的逐次播放起止及完整收听时长事件；不能把曲目变化或位置通知直接当作一次有效收听。后续需选择扩展自身记账或新增明确的只读事件契约。
- 普通原生扩展仍使用现有完整系统权限信任模型，没有新增细粒度事件权限声明。

### 7.2 Scrobble 扩展（ListenBrainz / Last.fm） — P3 · M

- 作为**独立扩展**实现，不进主项目（符合"主项目不包含第三方音源插件"的现有边界）。
- 规则：播放超过 50% 或 4 分钟才计一次；离线时本地排队重试；凭据走扩展配置的安全存储。
- 前置：7.1 的订阅能力已具备；需先确定可靠的收听时长、跳转与重复播放计数规则。

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

## 九、缺陷与稳定性

来源：2026-10-22 后端代码审查。行号为审查时位置，修复前先复核；每项修复附带能复现原问题的回归测试。

### 9.1 解码线程取消丢失唤醒 — P0 · S，已复核

- 位置：`backend/infrastructure/playback/pcm_decoder.cpp:44`（`cancel()`）与 `:97`（`m_space.wait`）。
- 问题：`cancel()` 未持有 `m_mutex` 即置位并 `notify_all`；解码线程检查谓词后、进入等待前收到的通知会丢失，`~PcmDecoder` 的 `join()` 永久阻塞，连带 `FFmpegPlaybackBackend` 与响度分析析构卡死。
- 修复：`cancel()` 先 `std::lock_guard lock(m_mutex)` 再置位、通知。

### 9.2 Crossfade 尾部 0／1 帧边界 — P0 · S，已复核

- 位置：`backend/infrastructure/playback/pcm_renderer.cpp:162`、`:227-259`。
- 问题：只判断 `length > 1`，`fadeFrames` 却取 `current->available()`，可能为 0 或 1。为 1 时相位计算除以 0 得到 NaN 并停播；为 0 时走无缝切换分支，`next` 被移走而 `fading` 未清，下一轮空指针。
- 修复：触发条件增加 `current->available() >= 2`；无缝切换分支同时清除 `fading`。

### 9.3 包类型变更沿用信任 — P0 · S，已复核

- 位置：`backend/infrastructure/extensions/plugin_manager.cpp:1308`（`trustRetained`）。
- 问题：旧包为已信任的代码主题时，同 id 替换为原生扩展会保留 `trusted` 与 `enabled`，直接加载原生库；用户只同意过运行主题 QML。
- 修复：包类型变化或新增原生模块时重置为未信任，需用户重新确认；补充替换场景测试。

### 9.4 `AudioProxy::revoke` 遍历中删除 — P0 · S，已复核

- 位置：`backend/infrastructure/extensions/audio_proxy.cpp:176`。
- 问题：遍历 `m_downloads` 时调用 `cancelDownload`，经 `finishDownload` 删除元素，迭代器失效；扩展停止时若有下载进行中，可能崩溃或漏取消。
- 修复：遍历键的副本。

### 9.5 恢复回滚与新会话顺序 — P0 · S–M

- 位置：`backend/runtime/backend_runtime.cpp:87-119`。
- 问题：`m_session->start()` 成功后才执行 `recoverRestore`，校验失败时会对已打开的 SQLite 文件与 `media/` 执行重命名或删除；回滚失败后 `m_session` 为空，IPC 全部失效，任务停在 `switching`。
- 修复：先恢复再构建会话；回滚失败时报告致命错误或启动只用于报告失败的降级会话。

### 9.6 音频代理重定向与下载上限 — P1 · S–M

- 位置：`backend/infrastructure/extensions/audio_proxy.cpp:83`、`:226-235`、`:470-482`。
- 问题：跨域重定向只移除 `authorization`／`cookie`，扩展自定义的密钥头会发往新主机；未配置 `allowedHosts` 时允许 https 降级到 http；`maxBytes` 默认不限且不检查 `Content-Length`，可写满磁盘。
- 修复：跨域时移除全部自定义头；禁止降级；宿主侧设置硬上限。

### 9.7 导出目标路径规范化校验 — P1 · S

- 位置：`backend/runtime/library_maintenance.cpp:251-253`、`backend/infrastructure/library/library_file_io.cpp:87`。
- 问题：`within()` 只用 `absoluteFilePath`，不解析符号链接，可借符号链接把导出写入曲库目录；备份已使用规范化目标，导出未使用。
- 修复：导出同样走规范化校验，并要求 IPC 传入绝对路径。

### 9.8 导入／恢复孤儿对象与事务边界 — P1 · M

- 位置：`backend/infrastructure/library/music_directory.cpp:184-197`；`backend/runtime/library_maintenance.cpp:40-45`、`:442`、`:494`、`:605-619`、`:643`、`:703-724`、`:782`。
- 问题：
  - 导入时音频先发布进 `media/` 并写入 `audio_objects`，之后封面发布失败直接返回，不在事务中。
  - 中断导入的恢复路径插入 `retained=1` 却无 `song_assets` 的对象；`stageRemoval` 只处理 `retained=0`，文件和记录永久占用空间。
  - 多处 `exec()` 不检查返回值；取消时忽略 `rollbackRestore` 结果仍报告已取消；迁移中歌词保存、迁移状态更新与旧文件清理各自独立执行。
- 修复：先校验封面再发布，失败时补偿回滚；恢复和迁移的相关写操作合并进同一 `Transaction`；统一检查 SQL 结果并如实报告。

### 9.9 切换输出设备丢失预加载 — P1 · S

- 位置：`backend/infrastructure/playback/ffmpeg_playback_backend.cpp:105-125`、`backend/application/playback/player_engine.cpp:69-70`、`:184-187`。
- 问题：切换设备时清空了下一首，但 `m_preloadKey` 未清，不会重新预加载，本曲剩余部分失去 Gapless／Crossfade；过期的 `nextTrackStarted` 令牌不匹配时调用 `loadCurrent()`，旧曲从头播放。
- 修复：后端变化时总是失效并刷新预加载；取消预加载时递增代次，忽略过期事件。

### 9.10 崩溃归因使用进程全局状态 — P1 · S

- 位置：`backend/infrastructure/extensions/crash_guard.cpp:23-24`、`:62-66`。
- 问题：扩展自有工作线程崩溃时，信号处理读取的是后端线程当时所在的扩展，可能禁用无关扩展。
- 修复：改用 `thread_local`；工作线程中的崩溃归为未知，不自动禁用。

---

## 十、性能与界面质量

来源：2026-10-22 前端代码审查。

### 10.1 QML 模块化与编译期检查 — P2 · M–L

- 现状：`frontend/qt-qml/CMakeLists.txt` 以 `resources.qrc` 打包 QML，`src/main.cpp` 通过 `setContextProperty` 注入 `i18n`、控制器等全局对象；qmlcachegen／qmlsc 不生效，qmllint 无法解析全局名称，绑定只能运行时解释。
- 方案：迁移到 `qt_add_qml_module`，控制器改为 `QML_SINGLETON` 或 required property；CI 增加 `all_qmllint`。
- 注意：迁移会改变 `AGENT.md` 中“新增 QML 同步 `resources.qrc`”的约定，需同步文档。

### 10.2 曲库多选与列表模型性能 — P2 · S–M

- `qml/components/LibraryPanel.qml:52-54`、`:323`、`:330`：每行执行 `selectedSongIds.indexOf`，全选判断为 `every` 套 `indexOf`，选择变化时全部行重算；每行还通过 `songActions(modelData)` 新建菜单数组。改为 C++ 侧 `QSet` 维护选择并以 role 或方法暴露，全选状态由控制器计算，菜单在打开时生成。
- `src/models/record_model.cpp:12-22`（已复核）：常规增量更新接近 O(n)，整体重排（如切换排序）退化为 O(n²)，且每次比较都 `toMap()` 复制。可先建标识到行号的索引，并缓存标识值。
- 验收：数千首曲库切换排序、全选、批量选择不卡顿；用 `QAbstractItemModelTester` 覆盖 `RecordModel` 增删改移。

### 10.3 页面按需加载 — P2 · S

- 现状：`qml/Main.qml:290-297` 与 `qml/shell/PageHost.qml:19-21` 中非调试页面始终 `active`，仅切换可见性，冷启动实例化全部页面。
- 方案：首次访问时激活并保留（维持页面状态），可选异步加载。

### 10.4 i18n 回退与统一翻译入口 — P2 · S

- 现状：`zh.json`／`en.json` 键集合一致。`src/i18n.cpp:62-66` 缺键时返回空字符串，界面空白；约 17 组动态拼接键（如 `lyrics_error_` + 代码）静态检查无法覆盖；25 个 QML 文件各自定义 `t()`／`tr()`，并混用 `i18n.text`、`translator.text`。
- 方案：缺键回退英文，再回退键名并记录警告；提供统一翻译入口；增加测试校验两份文件键集合一致及字面量键存在。

### 10.5 键盘与读屏可访问性 — P2 · S–M

- 现状：`LrcLyrics.qml:163`（点击歌词跳转）、`LyricsPanel.qml:312`（歌词候选）仅有 `TapHandler`，无焦点、按键处理与 `Accessible.role`；`ActionMenu.qml`、`ChoiceField.qml`、`InputField.qml`、`Sidebar.qml` 无 `Accessible.*`。
- 方案：改用按钮类控件，或补充 `activeFocusOnTab`、Enter 处理与 `Accessible.name`；遵循 [UI 设计规范](ui-design-guidelines.md)。

---

## 十一、工程化

### 11.1 编译警告、Sanitizer 与静态分析 — P1 · M

- 现状：各 CMake 目标没有警告选项；仓库无 `.clang-tidy`、`.clang-format`、`CMakePresets.json`。9.1、9.4 这类问题本可由 TSan／ASan 暴露。
- 方案：GCC／Clang 开启 `-Wall -Wextra -Wpedantic`（逐步清理后再考虑 `-Werror`）；CI 增加 Linux Debug + ASan／UBSan，播放与扩展相关测试另跑 TSan；增加只检查改动的 clang-tidy；提供 CMakePresets。

### 11.2 CI 依赖缓存与发布流程加固 — P2 · S–M

- `.github/workflows/ci.yml:62-133`：每次从源码构建 Wayland、ECM、LayerShellQt、fcitx5-qt、QtKeychain，未缓存；`:154` 的 ccache 键包含提交 SHA，每次新增缓存挤占配额。按固定依赖版本缓存安装目录，ccache 使用前缀恢复键。
- `.github/workflows/release.yml:31-36`：`gh run list --branch=main` 取 `.[0]`，同一提交重跑 CI 时可能选错；`:68` 产物数量写死为 8。改为按 `headSha` 与成功状态筛选，产物数量从清单推导；后续评估 Windows 代码签名与构建来源证明。
- macOS 构建按产品需要另行评估，不在本项范围。

### 11.3 测试拆分与覆盖补齐 — P3 · M

- `tests/refactor_test.cpp`（约 1800 行）混合 IPC／运行时测试与完整 `Main.qml` 界面测试，失败难定位、无法并行；拆为运行时与界面两个目标，引擎搭建移入 `tests/support/`。`tests/extensions_test.cpp` 同步评估。
- 缺少直接测试：`RecordModel` 对比逻辑（见 10.2）、酷狗账号会话、`ThemeController`、`backend/ipc/api/*` 参数校验。

### 11.4 超大模块拆分与分层收敛 — P3 · L

- `backend/runtime/library_maintenance.cpp`（约 800 行）直接执行 SQL、注册 IPC 方法并包含 `ipc/api` 头，违反 [架构文档](architecture.md) 中 runtime 只负责装配与生命周期的约定；拆为 storage 仓储、`application/library` 用例与 `ipc/api` 路由。
- `backend/infrastructure/extensions/plugin_manager.cpp`（约 1850 行）可拆为 SDK 桥接、注册表持久化、包存储、生命周期与依赖图、提供方与音频路由；`:1757` 起的方法分派改为表驱动。
- `player_engine.cpp:160`、`:252`、`:305` 用 `QFileInfo::isFile()` 判断本地／远程来源，文件 I/O 应下沉到 infrastructure。
- 遵循最小改动原则，在相关功能改动时分步进行，不单独大规模重构。

---

## 建议的实施顺序

1. **P0 缺陷**：9.1–9.4 改动小、风险高，逐项修复并补回归测试；随后处理 9.5。
2. **质量兜底**：11.1 加入 Sanitizer 与警告，防止同类问题回归；再处理 9.6–9.10。
3. **P0 功能**：2.2 收藏、1.2 宿主睡眠定时器。最近播放与队列重排已实现，不再作为待开发任务。
4. **曲库与歌单**：先推进 3.1 歌单排序、3.2 M3U 导入；2.3 目录监听先确认与自包含音乐库相容的产品语义。6.1 备份／恢复维护现有能力。曲库规模增长时同步推进 10.2、10.3。
5. **播放验收**：对 1.3 Gapless、1.4 ReplayGain、1.5 Qt 倍速、1.6 Crossfade 和 1.7 EQ 执行专题文档中的真机检查；不重复启动已完成的引擎原型或实现。
6. **平台能力**：4.1 Windows SMTC、4.2 桌面通知，可与播放验收并行。
7. **之后**：按需推进其余 P2／P3；Scrobble 可复用现有事件订阅，收听记账契约另行设计；11.3、11.4 随相关模块改动分步进行。

## 通用要求

- 每项功能附带自动化测试（C++ 单元测试 + `tests/qml` 的 QML 测试），新增 IPC 方法同步更新 `docs/API.md`，面向用户的行为变化同步更新 README 功能表。
- 数据库变更遵循现有版本与资源迁移机制，兼容旧库；迁移失败不得破坏原数据。文件与数据库关联变更考虑暂存、事务、回滚及重启恢复。
- 文案同时更新 `frontend/qt-qml/i18n/zh.json` 与 `en.json`。
- 任何会修改用户原始文件或对外暴露接口的功能（2.8、8.1）默认关闭，并在设置中给出清晰说明。
