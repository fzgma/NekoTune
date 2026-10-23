# NekoTune IPC API

NekoTune 使用基于换行分隔的 JSON 协议，通过 Unix domain socket 进行本地 IPC 通信。

## Socket 名称

后端和客户端按以下顺序解析 Qt 本地 socket 名称：

1. `NEKOTUNE_SOCKET`
2. `nekotune`

在 Linux 上，Qt 会将它映射到底层 Unix domain socket，通常位于 `/tmp` 下。

## 请求

```json
{"id":1,"method":"player.status","params":{}}
```

`id` 是可选字段，但建议提供。响应会在存在 `id` 时原样带回，方便客户端匹配请求和响应。

## 响应

成功响应：

```json
{"id":1,"status":"ok","data":{"state":"playing","position":120000,"duration":300000}}
```

错误响应：

```json
{"id":1,"status":"error","message":"No song loaded"}
```

请求必须以换行结束。服务端支持一次读取多个请求，也支持一个请求分多次写入；带有 `id` 的请求会在响应中原样返回该 ID。缺少 `method`、`path`、数值参数或队列项 `id` 时，服务端返回带 ID 的参数错误，不会执行操作。

## 事件

内置音乐壁纸复用 `server.connected` 与 `player.* / lyrics.changed`，不轮询播放状态。前端本地 `WallpaperManager.audioAnalysis` 描述内置布局能力，仍返回 `spectrumAvailable: false`；它不是后端频谱 API。SDK 2.2 可向原生扩展提供默认 FFmpeg 后端的 PCM；主题通过既有 `extensions.call` 和命名空间扩展事件接收分析后的频段，Qt 兼容后端不支持 PCM。见[壁纸说明](wallpaper.md)。

后端会向所有已连接客户端广播事件：

```json
{"event":"player.state_changed","state":"playing"}
```

## 歌曲身份

队列项的 `id` 仍表示本次入队产生的队列项 ID，用于 `queue.play`、`queue.remove` 和 `queue.move`。后端现在还会为每首歌返回：

- `song_hash`：本地文件内容的 SHA-256；在线曲目为空
- `song_id`：SQLite 中的持久歌曲 ID；同一份音频内容重复入队会复用同一个 `song_id`
- `custom_title`：用户自定义歌名
- `artist`：用户自定义作者/歌手名
- `lyrics`：用户自定义歌词，当前播放歌曲对象会返回该字段
- `cover_url`：统一解析的本地优先封面 URL，各歌曲列表与播放状态均可提供
- `source`：`{kind:"local"|"extension",provider_id,track_id}`，在线曲目以来源及来源曲目 ID 标识
- `resource_key`：歌词和封面使用的稳定资源身份，本地为音频 hash，在线为独立命名空间中的缓存键
- `last_played_at`：最后一次成功开始播放的 UTC ISO 8601 时间，含毫秒，例如 `2026-10-16T12:34:56.789Z`；从未记录时为空字符串，只读

在线曲目沿用数字 `song_id`，`path`/`first_path` 为空；客户端不得用空路径推断在线曲目记录应被删除。`lyrics.*` 请求的 `track_id` 使用 `resource_key`，本地歌曲原有 hash 调用保持兼容。播放地址和鉴权头不包含在歌曲状态中。

当前播放歌曲对象还会返回 `queue_id`，它与队列项的 `id` 相同。

## 方法

### 系统媒体控制相关增量

`player.status` 与首次 `server.connected.data` 增加 `capabilities`：`can_play`、`can_pause`、`can_seek`、`can_go_next`、`can_go_previous`，均为布尔值。能力变化广播 `player.capabilities_changed`，携带同形 `capabilities` 对象；普通进度事件不重复计算导航能力。

`player.next`／`player.previous` 接受可选布尔参数 `preserve_state`，缺省为 `false`。传 `true` 时切歌保留原有暂停或停止状态；原有无参数调用继续自动开始播放。

`player.seek` 保留原有非负毫秒 `position`，另支持有符号毫秒 `offset`，两者必须且只能传一个。可选正整数 `queue_id` 用于校验当前队列项。带身份的绝对跳转若已过期或超过持久化歌曲时长，无操作并返回当前快照；相对跳转在后端计算位置，负方向越界归零，越过结尾按保留播放状态的下一首处理。相对请求进入现有命令调度器，因为它可能更新队列选择；原有不带身份的绝对请求保持原校验与响应方式。

适配器确认实际跳转后广播 `{"event":"player.seeked","position":5000,"queue_id":2}`，单位仍为毫秒。请求成功表示已接受操作；客户端应等待此确认或后续位置事件，不将请求目标当成实际位置。换源和停止时取消旧跳转确认。

Linux MPRIS 位于桌面端的会话总线，对外进度／时长为微秒，使用后端资料与 `cover_url`。仅提供常用控制；不开放 LoopStatus、Shuffle、TrackList 或 OpenUri。系统调用的失败和超时通过 D-Bus 错误回复，不改变 IPC 的既有响应结构。

### `player.play`

播放当前曲目、队列中的第一首曲目，或传入的本地文件路径。

```json
{"id":1,"method":"player.play","params":{"path":"/home/user/Music/song.mp3"}}
```

### `player.pause`

暂停播放。

```json
{"id":2,"method":"player.pause","params":{}}
```

### `player.toggle_play_pause`

播放时暂停，暂停或停止时开始/继续播放当前曲目。

```json
{"id":3,"method":"player.toggle_play_pause","params":{}}
```

### `player.stop`

停止播放。

```json
{"id":3,"method":"player.stop","params":{}}
```

### `player.next`

按当前播放顺序切换到下一项；顺序和单曲循环模式在队尾停止。

```json
{"id":4,"method":"player.next","params":{}}
```

### `player.previous`

按当前播放顺序直接切换到上一项；随机模式返回播放历史。

```json
{"id":5,"method":"player.previous","params":{}}
```

### `player.seek`

跳转到指定播放位置，单位为毫秒。

```json
{"id":6,"method":"player.seek","params":{"position":120000}}
```

### `player.set_volume`

设置音量，取值范围为 `0` 到 `1`。

```json
{"id":7,"method":"player.set_volume","params":{"volume":0.8}}
```

### `player.set_playback_mode`

设置全局播放顺序，参数 `mode` 为 `sequential`（顺序）、`repeat_one`（单曲循环）、`shuffle`（随机）或 `repeat_all`（列表循环）。成功返回完整播放器状态，包含 `playback_mode`；非法值或保存失败返回错误并保留原模式。模式保存至 `config/settings.json`，首次使用默认顺序播放。切换不打断当前歌曲或改变进度。

```json
{"id":8,"method":"player.set_playback_mode","params":{"mode":"shuffle"}}
```

`player.status` 和首次连接事件均包含 `playback_mode`。模式变更广播 `{"event":"player.playback_mode_changed","playback_mode":"shuffle"}`，供所有客户端同步。

### `player.shuffle_strategies` / `player.set_shuffle_strategy`

查询返回可用算法目录、保存的偏好和当前有效算法：

```json
{"shuffle_strategies":[{"id":"shuffle_bag","name":"Shuffle bag"},{"id":"example.shuffle/artist","name":"Avoid consecutive artists"}],"shuffle_strategy":"example.shuffle/artist","effective_shuffle_strategy":"example.shuffle/artist"}
```

设置接收 `{"id":"example.shuffle/artist"}`，内置算法 ID 为 `shuffle_bag`，扩展算法为
`扩展ID/算法ID`。未知、不可用 ID 或设置保存失败返回错误并保留原状态；成功返回完整播放状态。
选择全局保存在 `config/settings.json` 的 `shuffle_strategy`，不改变播放模式、当前歌曲或进度。
随机模式的手动下一首、自然播放结束和预加载统一使用所选算法；扩展可允许重复播放。

`player.status` 和 `server.connected.data` 包含上述三个字段；目录、偏好或有效算法改变时广播
`player.shuffle_strategy_changed`，携带同形字段。扩展不可用或计算失败时，保留 `shuffle_strategy`，
将 `effective_shuffle_strategy` 回退至 `shuffle_bag`；扩展恢复或重新注册后自动切回。
切换算法重新建立以当前条目为起点的播放历史，取消旧预加载。算法运行状态不跨重启保存。

### `player.set_playback_rate`

设置全局播放速度，参数 `rate` 为 0.5–2.0 的有限数值，默认 1.0；非法类型、越界值或保存失败返回错误并保留原速度。成功返回完整状态，包含 `playback_rate` 和 `playback_rate_supported`。速度原子保存至 `config/settings.json` 的 `playback_rate`，不写入歌曲或队列，切歌、暂停和重新连接后沿用。

```json
{"id":9,"method":"player.set_playback_rate","params":{"rate":1.5}}
```

速度变化广播 `{"event":"player.playback_rate_changed","playback_rate":1.5,"position":12345}`，其中 `position` 为解码器当前毫秒位置。首次连接与 `player.status` 同样包含速度及支持标记。Qt 6.10+ 且后端支持音高补偿时开放倍速；其他环境保持 1.0 并拒绝非 1.0 请求。非法保存值在启动时回退 1.0，设置页可恢复并保存 1.0。

进度、时长和歌词时间戳始终使用原媒体毫秒位置，不除以倍速；逐字歌词只在解码器更新之间按当前倍速做短暂、有上限的视觉插值，每次位置更新、倍速变化或暂停都会重新校准。歌词行选择始终按解码器位置计算。

Linux MPRIS 的 `Rate` 为只读实际速度，变化发送 `PropertiesChanged`；支持倍速时 `MinimumRate`／`MaximumRate` 为 0.5／2.0，否则均为 1.0。系统端写 `Rate` 返回 `PropertyReadOnly`（包括 0，不作为暂停请求）。

顺序模式播到队尾停止；列表循环在队尾回到队首，上一首可从队首回到队尾。单曲循环只在自然结束时重复，手动切歌仍沿队列顺序。上一首直接切歌，不再根据 3 秒进度重播本曲；顺序和单曲循环在队首重新播放本曲。

随机模式默认使用 `shuffle_bag`：每轮每个队列项播放一次，轮次结束后重新洗牌，至少两项时避免跨轮连续重复同一项。队列显示顺序保持不变，相同歌曲的不同 `queue_id` 分别参与。上一首回到播放历史，下一首先沿历史前进，无历史时上一首重播当前项。直接点播、替换队列或切换模式重建随机轮次；追加项加入本轮待播序列，删除项从待播序列和历史移除。随机历史不跨重启保存。

### 音频引擎设置

- `player.audio_settings`：读取配置、实际后端和能力。
- `player.set_audio_backend`：`{"backend":"ffmpeg"}` 或 `{"backend":"qt"}`，重启生效。
- `player.set_transition`：`{"crossfade_seconds":3.5}`，范围 0–12，0 关闭淡入淡出但保留 Gapless。
- `player.set_equalizer`：部分更新 `enabled`、`auto_headroom`、`preamp_db`、`gains`（恰好 10 个数）或 `preset`（`flat` / `bass` / `vocal` / `bright`）。`gains` 与 `preset` 互斥，所有增益范围 −12～+12 dB。
- `player.set_replay_gain`：部分更新 `mode`（`off` / `track` / `album`）和 `preamp_db`（−12～+12 dB），默认 `off` / `0`。配置持久化，Qt 不支持时仍保留设置。
- `player.analyze_loudness`：可选 `ids`（1～10000 个正整数歌曲 ID，去重）；省略时分析可用本地曲库，传入空数组或不存在 ID 返回错误。选中的专辑会扩展至全部已入库成员，跳过已具备数据的组。仅允许一个后台任务，响应立即返回进度；不修改原音频，不自动重新启动。
- `player.cancel_loudness_analysis`：请求取消当前分析；重复取消安全，停止后 `running` 为 false。已经完成的缓存保留。

以上方法返回 `data.audio_settings`；完整播放状态和 `server.connected` 也包含同一字段。保存成功后发布 `player.audio_settings_changed`，多客户端按确认值同步。配置示例：

```json
{"audio_settings":{"backend":"ffmpeg","active_backend":"ffmpeg","restart_required":false,"crossfade_seconds":0,"gapless_supported":true,"crossfade_supported":true,"equalizer_supported":true,"sample_rate":48000,"frequencies":[31,62,125,250,500,1000,2000,4000,8000,16000],"available_bands":[true,true,true,true,true,true,true,true,true,true],"equalizer":{"enabled":false,"auto_headroom":true,"preamp_db":0,"gains":[0,0,0,0,0,0,0,0,0,0]}}}
```

实际能力来自运行中的后端，可能因受控回退而不同于保存的偏好；输出采样率为 0 时各频段暂不可用。FFmpeg 当前仅支持原速，`playback_rate_supported` 为 false，不改写已存储的倍速值。手动切歌不使用 Crossfade；预加载不发布 `player.track_changed`，边界到达才提交选择、更新播放次数和歌词。

`audio_settings` 还包含以下响度字段，设置、歌曲切换、后端回退和分析进度均通过 `player.audio_settings_changed` 同步：

```json
{"replay_gain":{"mode":"track","preamp_db":0},"replay_gain_supported":true,"replay_gain_status":{"gain_db":-4.5,"source":"tag","peak_limited":false},"loudness_analysis":{"running":false,"cancelling":false,"cancelled":false,"completed":0,"total":0,"error":""}}
```

`source` 为 `off`、`tag`、`analysis`、`missing` 或 `unavailable`。`gain_db` 为选定模式、预增益及峰值余量合成后的 ReplayGain 目标增益，不含 EQ／动态限幅。缺少对应数据时不套用预增益、不回退另一模式，返回 `missing` / `0`；Qt 返回 `unavailable` / `0`。分析 `completed` 为完成或已有数据跳过的文件数，`error` 为最近错误，取消及部分失败时可能小于 `total`。分析分组、−18 LUFS 目标、静音与缓存失效规则见 [音频引擎](audio-engine.md)。

### `player.audio_outputs` / `player.set_audio_output`

查询、设置响应和 `player.audio_outputs_changed` 事件共享设备快照：

```json
{
  "devices": [{"id": "aGVhZHNldA==", "name": "USB Headset", "is_default": true}],
  "audio_output": {
    "selected_id": "", "selected_name": "",
    "active_id": "aGVhZHNldA==", "available": true
  }
}
```

`player.set_audio_output` 接收必填字符串 `device_id`：空字符串表示跟随系统默认，非空值必须来自当前设备列表。ID 是 Qt 设备原始标识的 Base64，不使用显示名称匹配。非法参数、离线 ID 或保存失败返回错误，并保留原选择。设备恰在保存后断开时保留已保存的偏好，按离线状态处理。

选择原子保存到 `config/settings.json` 的 `audio_output` 对象（`device_id`、`device_name`），不覆盖其他设置；首次使用跟随系统默认。保存的设备离线时仍返回它的 ID 和名称，`active_id` 为空、`available` 为 `false`，不会改为扬声器。`player.status` 和 `server.connected.data` 也包含 `audio_output`。

正常切换不重新加载音源，保留进度、音量和播放状态。当前实际输出设备消失时暂停并清除待播放意图，包括正在解析在线音源的情况。默认模式只有在旧设备仍在线、系统主动更改默认输出时连续播放；旧设备消失时先暂停再连接新的默认输出。设备恢复或用户选择其他可用输出后均需手动恢复播放。没有可用输出时 `player.play` 返回 `audio_output_device_unavailable`；点播或切歌仍可更新队列选中项，但保持暂停。设备断开不删除队列项，也不作为音频文件损坏处理。

Linux 编入 libpulse 支持时，设备对象还包含 `ports`（每项为 `id,name,kind,available`）和 `active_port_id`。同一声卡的耳机、扬声器和线路输出按端口分别展示；`kind` 用于翻译显示名称，`name` 保留系统说明。内置扬声器即使因耳机插入而被标记为不可用，仍允许用户显式选择；未连接的耳机端口禁用。

`player.set_audio_output` 可附加 `port_id`，例如 `{"device_id":"...","port_id":"analog-output-headphones"}`。端口切换异步执行并回读确认后保存；保存失败时尝试恢复原端口。状态增加 `selected_port_id`、`selected_port_name`、`active_port_id`；配置增加 `port_id`、`port_name`，旧的仅设备配置继续有效。显式选择的耳机端口断开时等待重连，即使声卡本身仍在线也不回退外放。默认模式也会检测同声卡的耳机拔出并暂停。

同声卡端口是系统共享设置：切换可能影响其他使用该声卡的应用，界面会提示。恢复保存的端口时可能重新应用该设置；系统主动改到另一个仍可用的端口时，显式固定的端口保持暂停，需用户重新选择，避免持续抢占系统设置。

### `player.status`

返回当前播放器状态。

```json
{"id":8,"method":"player.status","params":{}}
```

响应里的 `data.database_path` 是当前 SQLite 数据库文件路径。

### `queue.add`

把本地音频文件或已有曲库歌曲添加到播放队列。`path`、`song_id`、`song_ids` 三选一；`song_ids` 是非空的有序歌曲 ID 数组，适合整张专辑入队，允许重复歌曲。整批歌曲全部有效且本地文件可用时才提交，否则队列保持原样。

可选的插入选项：

- 省略 `position` 和 `next`，或 `next: false`：追加到队列末尾。
- `position`：从 0 开始的整数，范围 `0..队列长度`（含末尾），表示插入后的起始位置。
- `next: true`：插入当前项后面；没有当前项时插入队首。整批歌曲保留输入顺序。最近一次「下一首播放」放在尚未播放的优先项前面。
- `position` 和 `next` 不可同时出现；`next` 必须是布尔值。

所有插入只改变队列，不自动选中或播放。当前项按队列项身份保持，插入到它前面时仅调整 `current_index`，不重新加载音频。每次入队都分配新的 `queue_id`，即使歌曲已经存在于队列中。

随机模式中，`next: true` 的待播项按其队列顺序优先于随机抽取和历史前进分支；成功切换后才消耗该项的优先标记，上一首不会消耗其他待播项。单曲循环仍在自然结束时重复当前曲目，显式下一首继续按队列导航。

```json
{"id":9,"method":"queue.add","params":{"path":"/home/user/Music/song.wav"}}
{"id":91,"method":"queue.add","params":{"song_id":12,"next":true}}
{"id":92,"method":"queue.add","params":{"song_ids":[12,13,14],"position":0}}
```

成功返回 `data.items` 和 `data.current_index`。队列项的 `play_next` 布尔字段表示尚未消耗的优先待播标记。

### `queue.move`

移动一个已有队列项，参数 `queue_id` 是本次入队产生的项 ID，`position` 是移动完成后的目标索引（从 0 开始，范围 `0..队列长度-1`）。不会添加或删除歌曲，重复歌曲的各个队列项保持独立身份。

```json
{"id":93,"method":"queue.move","params":{"queue_id":7,"position":2}}
```

移动当前项或跨越当前项时，`current_index` 跟随原来的 `queue_id`，播放状态、音频源和播放进度保持不变。整个重排在事务内重写位置，失败时内存与数据库均保持原样；成功返回 `data.items`、`data.current_index` 并广播 `queue.changed`。

### `queue.play`

播放队列中指定 `id` 的曲目。

```json
{"id":10,"method":"queue.play","params":{"id":1}}
```

### `queue.remove`

从播放队列中移除指定 `id` 的曲目。如果移除的是当前播放曲目，后端会停止当前曲目；当后面还有曲目且播放器此前处于播放状态时，会自动继续播放下一首。

```json
{"id":11,"method":"queue.remove","params":{"id":1}}
```

### `queue.clear`

清空队列并停止播放。

```json
{"id":12,"method":"queue.clear","params":{}}
```

### `queue.status`

返回队列内容和当前播放索引。

```json
{"id":13,"method":"queue.status","params":{}}
```

队列会保存到当前 SQLite 数据库，包含顺序、重复项、文件路径、优先待播标记和当前索引。后端重启后恢复最近一次队列，但不会自动开始播放；运行时 `queue_id` 会重新分配，客户端应重新获取快照。歌曲资料表中的历史歌曲不会自动重新加入队列。

### 歌单 `playlist.*`

歌单是独立于播放队列的持久歌曲集合，没有文件夹嵌套。同一 `song_id` 可以加入多个歌单，在同一歌单中只保留一次；再次添加会更新文件路径并保留原顺序。歌单中的歌曲使用 `song_id` 标识，不使用临时的队列项 ID。

- `playlist.list`：返回 `data.playlists`，每个歌单包含 `id`、`name`、有序的 `items`；歌曲包含 `song_id`、`song_hash`、`path`、`first_path`、`title`、`custom_title` 和 `artist`。
- `playlist.create`：传入 `name`，成功返回 `data.playlist_id` 和完整歌单列表。名称去除首尾空白后长度为 1–128 字符。
- `playlist.rename` / `playlist.delete`：通过歌单 `id` 改名或删除。删除歌单不影响队列、其他歌单、歌曲资料或本地音乐文件。
- `playlist.add`：传入歌单 `id` 和本地 `path`、已有队列项的 `queue_id`，或曲库歌曲的 `song_id`。按 `song_id` 添加时需有可用文件路径。导入仅影响歌单，不自动入队或播放。
- `playlist.remove`：传入歌单 `id`、`song_id`，仅移除该歌单中的关联。
- `playlist.play`：传入歌单 `id`，按歌单顺序替换队列并播放；可选 `song_id` 指定从哪首开始。空歌单、失效 ID、缺失文件或队列保存失败时返回错误并保留原队列。

```json
{"id":14,"method":"playlist.create","params":{"name":"现场录音"}}
{"id":15,"method":"playlist.add","params":{"id":1,"queue_id":3}}
{"id":16,"method":"playlist.add","params":{"id":1,"path":"/home/user/Music/live.flac"}}
{"id":21,"method":"playlist.add","params":{"id":1,"song_id":2}}
{"id":17,"method":"playlist.play","params":{"id":1}}
{"id":18,"method":"playlist.rename","params":{"id":1,"name":"Live"}}
{"id":19,"method":"playlist.remove","params":{"id":1,"song_id":2}}
{"id":20,"method":"playlist.delete","params":{"id":1}}
```

`player.status` 和首次连接事件包含 `playlists`；歌单改动、歌单歌曲资料更新通过 `playlist.changed` 广播完整 `playlists`。`queue.changed` 仅更新队列。清空队列不清空歌单。

旧版本的队列文件夹会在启动时通过事务自动迁移为歌单，嵌套名称展开为 `父级 / 子级`，每个歌单保留原文件夹直接包含的歌曲和顺序；原队列及其重复项保持不变。旧 `queue.folder.*` 方法及 `folders`、`folder_id` 响应字段已移除。

### 曲库与标签

`library.import` 传入本地音频 `path`，复制音频、按完整文件 hash 去重并保存库内资源关联，只更新曲库，不加入队列或开始播放。返回 `song_id` 和绝对路径；曲库变化广播 `library.changed`。

首次导入在工作线程通过 FFmpeg 同时提取时长与内嵌资料：`title`、`artist`、`album`、`album_artist`、`genre`、`date/year`、`track/tracknumber`、`disc/discnumber` 和 `lyrics/unsyncedlyrics`。写入歌曲对象的 `custom_title`、`artist`、`album`、`album_artist`、`genre`、`year`、`track_number`、`disc_number`、`lyrics`；日期取年份，`2/12` 等编号取分子。数字 `0` 表示未知；没有标签或探测失败时仍按原有文件名／未知时长规则导入。曲库、歌单和队列导入使用相同资料。重复 hash 保留已有文本、编号和手工清空的字段，不自动覆盖旧曲库；已有资料可在编辑器中补全。此功能不改写原音频标签，不提取内嵌图片。

```json
{"id":25,"method":"library.import","params":{"path":"/home/user/Music/NekoTune/song.mp3"}}
```

`library.list` 返回 `data.library`，其中 `songs` 按 `song_id` 升序列出全部历史歌曲，`tags` 列出全局标签。每首歌曲包含 `song_id`、标题、作者、`tags`（`{id,name}` 数组）、`path`、`available` 和 `cover_url`；后端依次从队列、歌单和持久化的导入路径寻找仍存在的文件。不可用歌曲仍会列出，`path` 为空、`available` 为 `false`。曲库变化会广播 `library.changed`，客户端应重新请求 `library.list`。

桌面曲库通过 `LibraryController` 对完整快照排序与组合筛选：默认最早加入，也可选最近加入、歌名、歌手、专辑、时长升降序、播放最多或最少，以及最近播放（`recently_played`，按 `last_played_at` 时间倒序）；相同排序值按 `song_id` 升序，未知时长和空播放时间在对应排序中始终排末尾。播放记录筛选支持全部、已播放（`play_count > 0`）、从未播放（`play_count == 0`），以及近 7 天（`last_7_days`）／30 天（`last_30_days`）播放过；时间窗口为当前 UTC 时刻之前的连续 7／30 × 24 小时，包含边界，排除空值、无效值和未来时间，每分钟刷新过期记录。来源筛选使用 `import_source`，与文字搜索及标签交集同时生效。来源选项来自完整曲库，不因其他筛选隐藏。歌曲行展示播放次数，旧歌曲未记录的历史播放无法追溯。

首页的最近歌曲区可切换「最近入库」与「最近播放」。最近播放只显示有有效 `last_played_at` 的歌曲，按时间倒序展示最多五首，播放时提交当前展示的顺序；该区独立于曲库页面的搜索、筛选与排序。

排序与筛选在当前应用会话内跨页面保留，不写入数据库或更改正在播放的队列；显式播放时才把当前可见顺序作为 `library.play.song_ids` 提交。播放计数刷新可以重新排序或移出“从未播放”列表，已经建立的队列继续保留。首页仍使用自己的歌曲列表，不继承曲库视图条件。

曲库的「专辑」视图从满足当前搜索／来源／播放记录／标签条件的歌曲归组。专辑标识使用去首尾空白、忽略大小写的专辑名与专辑歌手；专辑歌手为空时回退歌曲歌手，无专辑名的记录集中到「未知专辑」。同名不同专辑歌手不会合并，合集可设置共同专辑歌手。进入专辑后默认按碟号、音轨号、`song_id` 排序，未知编号排末尾；搜索也匹配专辑和专辑歌手。返回专辑页或改资料不会重建正在播放的队列。

`song.update_metadata` 额外支持 `album`、`album_artist`、`genre` 字符串和 `year`、`track_number`、`disc_number` 整数（范围 0–9999）。省略保留原值，空字符串／数字 0 清空对应资料。歌曲信息窗口使用相同的稀疏补丁语义。

`library.update_metadata` 批量更新资料：必填非空且无重复的正整数 `song_ids`、非空对象 `patch`；`patch` 支持单曲接口的标题、歌手、歌词、标签及以上字段，不接受播放次数等只读字段。可选 `tag_mode` 为 `replace`（默认）、`add`、`remove`，分别替换全部标签、追加或移除指定名称；不传 `patch.tags` 时不改标签。所有目标和字段先校验，整批写入一个事务；任一歌曲不存在、标签无效或存储失败都会回滚全部修改，不发成功事件。成功返回 `updated_count` 并同步曲库、队列、歌单及当前歌曲资料，保留当前曲目和播放进度。界面打开批量编辑时固定目标 ID，只提交勾选的字段，失败保留草稿以便重试。

```json
{"id":27,"method":"library.update_metadata","params":{"song_ids":[7,9],"patch":{"album":"Moonlight","album_artist":"Various Artists","year":2024,"tags":["收藏"]},"tag_mode":"add"}}
```

曲库、歌单、队列和播放状态中的歌曲对象统一提供 `cover_url`：优先使用同目录同名 `.jpg`、`.jpeg`、`.png`、`.webp` 的 `file:` URL，其次使用按音频 hash 关联的已选歌词／歌词缓存封面，无封面时为空字符串。离线模式仅返回本地封面。歌词封面或离线状态变化后，后端同步广播 `library.changed`、`playlist.changed`、`queue.changed` 和 `player.track_changed`，客户端直接使用歌曲对象的 `cover_url`。

`tag.create` 传入 `name`；`tag.rename` 传入 `id`、`name`；`tag.delete` 传入 `id`。名称去首尾空白后须为 1–64 字符，按大小写折叠后的名称唯一。删除标签只移除歌曲上的关联，不删除歌曲、歌单或文件。

`library.play` 接收可选的 `tag_ids` 整数数组，并以“同时拥有全部所选标签”筛选；未传或空数组表示全部歌曲。可选 `song_id` 指定起播歌曲。后端按 `song_id` 升序生成队列，跳过文件不可用的歌曲，并在成功响应中返回 `data.skipped_song_ids`。无可播放歌曲、指定起播歌曲不可用或不匹配、队列保存失败时返回错误，原队列不变。

`library.play` 和 `playlist.play` 均支持可选的非空 `song_ids` 数组，表示当前可见歌曲及其顺序；数组必须包含互不重复的正整数。曲库歌曲须存在并满足 `tag_ids`，歌单歌曲须属于指定歌单；`song_id` 若提供，须属于这个列表且可播放。缺省时保持原有全曲库／全歌单顺序。曲库继续跳过不可用文件；歌单仍在任一选中歌曲文件缺失时失败。无效列表、起播歌曲不匹配或保存失败均不改变原队列、当前歌曲和播放状态。

```json
{"id":25,"method":"library.play","params":{"tag_ids":[1],"song_ids":[9,7],"song_id":7}}
{"id":26,"method":"playlist.play","params":{"id":2,"song_ids":[9,7],"song_id":9}}
```

`library.delete` 接收非空、无重复的 `song_ids` 和可选布尔值 `clean_files`（默认 `false`）。在同一事务内移除歌曲、标签关联、歌单关联和队列项，返回 `deleted_count` 及 `cleanup_errors`。默认明确保留音频和封面对象；`clean_files: true` 仅删除已无其他引用且未被要求保留的托管对象。外部原文件不参与清理。备份、导出或校验正在保护资源时，清理请求返回暂时不可用。文件暂存与数据库回滚、重启恢复使用清理日志。

清理先暂存文件，再提交数据库删除；任何 ID 无效、暂存失败或数据库写入失败时整批回滚。数据库提交后删除暂存文件，未能删除的路径通过 `data.cleanup_errors` 返回，界面明确提示已移除歌曲但仍有文件待清理。扩展下载正在进行时暂不允许清理。若正在播放的歌曲被删除，尝试继续播放后续队列项；没有后续项则停止。

```json
{"id":22,"method":"library.list","params":{}}
{"id":23,"method":"tag.create","params":{"name":"现场"}}
{"id":24,"method":"library.play","params":{"tag_ids":[1,2],"song_id":7}}
```

### 外部插件服务

外部插件通过 `extensions.call` 暴露独立服务；具体参数和事件由插件自己的文档定义。通用音乐与歌词客户端使用 `music.*` 和 `lyrics.*`，不依赖某个音源的专属协议。主项目不附带第三方音源插件，需自行安装和信任。

### `song.metadata`

读取指定歌曲的自定义元数据。

```json
{"id":14,"method":"song.metadata","params":{"song_id":1}}
```

### `ai.config.get` / `ai.config.set` / `ai.config.clear_key`

AI 配置接口是异步操作，不占用曲库写命令队列。`get` 与 `clear_key` 无参数，`set` 必须提供 `base_url` 和 `model`，可选提供非空 `api_key`。省略 Key 保留该服务原有 Key，清除必须使用独立方法；地址变化后不会使用其他服务的 Key。

```json
{"id":70,"method":"ai.config.set","params":{"base_url":"https://api.example.com/v1","model":"your-model","api_key":"your-key"}}
{"id":70,"status":"ok","data":{"config":{"base_url":"https://api.example.com/v1","model":"your-model","configured":true,"key_saved":true,"credential_error":""}}}
```

`configured` 表示地址和模型已设置，不代表连接测试通过。`key_saved` 表示当前服务保存了密钥引用；密钥库不可用或条目丢失时 `credential_error` 非空，请重存或清除。返回值从不包含明文 Key。Base URL 仅接受 HTTP(S)，不接受内嵌凭据、查询参数或 URL fragment；自动追加 `/chat/completions`，不覆盖已有路径。Keyless 服务可省略 Key。

### `ai.test`

无参数。使用已保存配置发送一次不含歌曲资料的短请求，同时检查鉴权、模型和返回 JSON 结构。成功返回 `{"connected":true}`，不会修改歌曲或配置。

### `song.suggest_metadata`

为单首歌曲生成建议，不修改数据库、文件或标签。必填正整数 `song_id`，可选 `draft` 对象含 `custom_title`、`artist`、`lyrics` 字符串和 `tags` 字符串数组。草稿缺失字段使用已保存内容。歌词优先取非空草稿、已保存自定义歌词、本地歌词、缓存；传给模型的是限长纯文本，不会触发在线歌词搜索。输入包含原始文件名和最多 200 个已有分类，不包含本地绝对路径和歌曲 hash。

```json
{"id":71,"method":"song.suggest_metadata","params":{"song_id":1,"draft":{"custom_title":"原始歌名","artist":"","tags":["收藏"]}}}
{"id":71,"status":"ok","data":{"song_id":1,"custom_title":"夜空","artist":"演唱者","tags":["中文","抒情"],"warning":""}}
```

建议标签为最多 5 个非空、去重的 1～64 字符名称；优先规范到已有分类名称。不确定的歌名、歌手返回空字符串；客户端应保留该字段当前内容，将标签合并到草稿，最后通过 `song.update_metadata` 保存。`warning: "ai_partial_result"` 提示部分名称缺少依据。

`artist` 保持字符串格式。AI 建议中的多人主署名按原顺序用英文逗号和空格连接，例如 `Orangestar, 初音ミク`；主署名中明确列出的制作人与虚拟歌手都应保留，歌词职务行中的作词、作曲等人员不自动加入。界面将逗号、中文逗号、顿号、分号或换行分隔的名字分别显示，名字内部的空格、`&` 和 `/` 保留。

使用 60 秒总超时，最多同时处理 4 个生成或测试请求；窗口关闭、断开连接不重放请求，客户端必须丢弃过期结果。JSON 模式仅在上游明确不支持相应参数时回退一次，网络错误、鉴权失败或非法返回不会自动重试。

AI 操作失败沿用 `status: "error"` 和 `message`，后者为可翻译的 `ai_error_*` 标识，包括 `configuration`、`url`、`model`、`key`、`keyring`、`key_missing`、`settings`、`auth`、`rate_limit`、`timeout`、`network`、`service`、`request`、`response`、`refused`、`busy`、`cancelled`。参数校验错误仍返回说明文字。上游错误正文及凭据不会原样回传。

### `song.update_metadata`

更新指定歌曲的用户自定义元数据。`custom_title` 也可用 `title` 传入，`artist` 也可用 `author` 传入。可选 `tags` 为标签名称数组，提供时在同一事务中创建缺失标签并替换该歌曲的全部标签；未传入的字段会保持原值。歌曲信息和标签更新后广播 `library.changed`，原有歌单与队列更新事件仍会发送。

```json
{"id":15,"method":"song.update_metadata","params":{"song_id":1,"custom_title":"自定义歌名","artist":"作者名","lyrics":"歌词内容"}}
```

## 歌词

后端负责解析、搜索和缓存歌词，Qt/QML 客户端只通过 IPC 获取结果。播放曲目后按“同目录同名 `.krc` → `.lrc` → 歌曲自定义歌词 → 本地歌词缓存 → 在线来源”顺序加载。在线来源优先匹配歌曲的音乐来源或导入来源所属的已注册歌词提供方，无法匹配时使用 LRCLIB。`.krc` 支持酷狗二进制与已解码文本，损坏时回退 `.lrc`。缓存文件位于 `~/Music/NekoTune/config/lyrics-cache`，以音频内容 hash（或标题、歌手、专辑和时长）为键，由 `QSaveFile` 原子写入；旧版缓存仍可读取。刷新跳过缓存和自定义歌词，同名文件仍优先。

`lyrics.changed` 广播当前歌曲的 `track_id`、`revision`、`state` 和可选 `document`、`candidates`。`document.format` 为 `krc`、`lrc` 或 `plain`；`document.lines` 保留 `{time_ms,text}`，KRC 行另有 `duration_ms` 和 `words`，每个词组包含 `{text,offset_ms,time_ms,duration_ms}`，时间单位均为毫秒。普通歌词位于 `document.plain_text`。歌词候选及选定歌词的 `cover_url` 为可选封面地址。状态包括 `loading`、`waiting_metadata`、`searching`、`ready`、`instrumental`、`not_found`、`offline`、`error` 和 `candidates`。

歌词请求携带当前歌曲的 `track_id`（音频内容 hash）：

- `lyrics.refresh`：忽略缓存和自定义歌词，重新获取当前歌曲歌词。
- `lyrics.search`：按 `title`、`artist`、`album` 手动搜索；`source` 从 `lyrics.sources` 读取，包括 `lrclib`（默认）和启用扩展的来源 ID。
- `lyrics.select`：按当前字符串 `revision` 和候选 `index` 选择结果。支持分阶段选择的来源可以先返回歌曲版本，再返回歌词候选；具体阶段由来源实现。
- `lyrics.set_offline`：设置离线模式；仍可读取同名 KRC/LRC、缓存和自定义歌词，不发起歌词网络请求。

自动结果仅在标题、歌手、专辑和时长满足精确匹配（时长误差不超过 2 秒）且候选明显领先时直接应用；其余结果交给客户端选择。旧版数据库的 ASR 表不会被删除，但后端不再读取或写入其中的转写结果。

自动加载分阶段扩展来源时，歌曲版本无法可靠匹配、返回空结果或获取失败可回退到 LRCLIB；歌词阶段的非空候选若未通过可信度校验（包括多个同分版本），保留原来源候选供用户选择，不因此回退。只有一个候选也必须通过匹配校验才能自动选择。手动搜索始终保留用户选择，不自动应用或切换来源。

### `lyrics.sources`

返回当前注册的手动歌词搜索来源。客户端应从此方法生成来源菜单，避免硬编码提供方列表。

歌词操作的 `track_id` 使用歌曲的 `resource_key`：本地曲目等于音频 SHA-256，在线曲目使用稳定的来源身份摘要。扩展启停时通过 `lyrics.sources_changed` 通知客户端刷新来源。

```json
{"id":20,"method":"lyrics.sources","params":{}}
```

```json
{"id":20,"status":"ok","data":{"sources":[{"id":"lrclib","name":"LRCLIB","supports_search":true},{"id":"example.lyrics/demo","name":"Example / 示例歌词","supports_search":true}]}}
```

### 异步完成与元数据补丁

协议格式不变。文件导入可能异步完成，响应仍沿用请求的 `id`，且只有实际入库或入队完成后才返回成功。多个结构性修改按接收顺序执行；导入期间仍可查询状态、暂停、停止、跳转和调节音量。断开连接后客户端不应自动重放修改请求，应先重新获取状态。

`song.update_metadata` 支持部分字段更新：缺少的字段保留，空字符串明确清空文本，空 `tags` 数组明确清空标签。编辑前使用 `song.metadata` 获取完整资料，不应把列表中未包含的歌词字段视为已有空歌词。

队列写入失败返回错误，并保留原来的队列和播放状态；成功响应不再掩盖持久化失败。

## 音乐目录和扫描

默认音乐目录为 `~/Music/NekoTune`，应用数据目录为其下的 `config`。`NEKOTUNE_HOME` 可覆盖音乐根目录，`NEKOTUNE_DB_PATH` 仍可单独覆盖数据库。`player.status` 除原有 `database_path` 外增加 `music_directory` 和 `config_directory`。

- `library.scan`：无参数，异步扫描音乐目录，返回 `data.started`。`true` 表示启动新任务，`false` 表示已有扫描正在进行。后端启动及独立前端首次连接自动发起扫描。
- 扫描同时为已有曲库中可访问但缺少时长的歌曲补齐记录，包括音乐目录之外的旧导入路径；不改动歌曲文件、标签或队列顺序。读取失败或文件丢失时保留未知时长，后续扫描可重试。
- `library.scan_finished`：事件包含 `imported`（当前为 0）、`skipped`、`failed` 和 `errors`。增量校验登记的音频对象并补全已知歌曲缺失时长；内容不符标记为损坏，丢失标记为缺失。扫描不自动导入任意目录文件、不联网获取配套资料，也不修改队列。
- `library.import`、`playlist.add` 的路径导入、`queue.add`、带路径的 `player.play`：参数结构不变，成功后返回独立托管副本的可解码绝对路径。所有入口统一后台复制和校验，不再创建软链接或 JSON 引用；文件发布成功后才提交歌曲／集合关联。
- `library.delete`：默认保留音频及配套文件；`clean_files: true` 清理托管文件。两种操作都会在事务内记录内容 hash 的扫描忽略状态。手动导入或下载可恢复；扫描不会恢复。
- `library.assets_failed`：配套文件保存或封面下载失败事件，包含 `song_hash` 和 `message`；不会中断音频播放。

歌曲对象（曲库、队列、歌单和 `song.metadata`）包含持久化的 `duration_ms`，单位为毫秒，`0` 表示未知。导入和扫描在后台读取本地音频时长；旧数据库自动新增字段，已知时长不会被读取失败的结果清空。

`player.status`、`player.track_changed`、`player.duration_changed` 和 `player.position_changed` 的 `duration` 使用当前歌曲持久化的 `duration_ms`，不使用解码器时长。恢复队列后即可显示已知时长，扫描补齐当前歌曲时会更新播放状态；库内时长未知时仍返回 `0`。已有歌名、歌手和正数时长时，自动歌词匹配无需等待解码器元数据。

歌曲对象还包含只读的 `import_source`、`play_count` 和 `last_played_at`，歌曲信息窗口会显示来源和播放次数。`import_source` 为首次入库的插件 ID（不含来源子 ID）或 `local`；插件下载成文件后仍保留插件 ID，原有 `source` 字段继续描述实际播放来源。相同音频重复导入及资料编辑不会覆盖最初的入库来源、播放次数或最近播放时间。旧在线曲目从 `source.provider_id` 补回插件 ID；没有来源记录的旧本地文件使用 `local`，历史播放次数从 `0` 开始。旧数据库通过补列迁移新增 `last_played_at TEXT NOT NULL DEFAULT ''`，即使已有非零播放次数也保持空时间，不补造历史。

`play_count` 在解码器确认成功进入播放状态时累加一次，同时在同一条 SQL、同一事务内写入 `last_played_at`，任一写入失败则两者都不更新。暂停/继续、缓冲恢复和扩展热重载不重复计数或更新时间；重新选歌、停止后重新播放、切换到重复队列项及单曲循环再次开始都会更新两项。仅加载、页面切换、搜索、排序、资料预览、播放失败或没有音频输出时不更新。统计持久化后通过 `library.changed`、`queue.changed`、`playlist.changed` 和 `player.track_changed` 同步；`song.update_metadata` 不用于修改这些只读字段。

音频对象位于 `media/<哈希前两位>/<哈希>.<扩展名>`，封面位于 `artwork/`，歌词文档和选择保存在 SQLite。成功在线匹配或手动选择经过歌曲身份和 revision 检查后更新资源。旧编号目录仅用于迁移读取；新导入不再依赖它。移走外部原文件不会影响已收取的音乐及配套资料。

设置和缓存集中在 `config`；首次默认目录迁移保留旧数据库和缓存，不覆盖已有目标。账号密钥和会话迁入系统密钥环，回读校验成功后删除旧明文凭据；失败保留原文件并返回 `credential_error`。设置页保存的语言优先于系统语言，`NEKOTUNE_LANGUAGE` 优先于保存语言；`lyrics.set_offline` 持久保存到 `config/settings.json`。

扩展通过宿主凭据接口使用系统安全存储（Windows 凭据管理器、macOS Keychain 或 Linux Secret Service / KWallet）。凭据不会通过公开业务 IPC 返回；系统密钥环访问或旧凭据迁移失败时返回错误说明。保存失败不会回退为明文存储。Linux 升级时会回读校验并迁移旧 libsecret 条目，成功后清理旧条目。

## 动态扩展接口

扩展使用方式和 SDK 见 [扩展开发文档](extensions.md)。以下方法保持原有 `{id,method,params}` / `{status,data}` 响应约定。管理操作只访问本地安装包，安装本身不会启用代码。

| 方法 | 参数 | data / 行为 |
| --- | --- | --- |
| `extensions.list` | 无 | `extensions` 注册状态数组、`selections` 界面选择；`runtimeReady` 表示管理进程已连接且扩展发现已完成；`catalogueReady` 表示目录已读取，`startupComplete` 表示启动激活已结束（含失败），不代表所有扩展均可用 |
| `extensions.install` | `path` 本地 ZIP；开发目录传 `development:true`；覆盖已有版本需 `replace:true` | `extension` 安装记录；更新启动失败回滚旧程序版本 |
| `extensions.enable` | `id`；首次信任需 `trusted:true` | 依赖按序启动，返回注册快照 |
| `extensions.disable` | `id` | 撤销能力，依赖该扩展的其他扩展进入 blocked 状态 |
| `extensions.reload` | `id` | 重新读取 manifest 并重建相关进程和界面贡献 |
| `extensions.uninstall` | `id,clearData?` | 默认保留数据；不删除挂载的开发目录 |
| `extensions.get_config` | `id` | `config` 对象 |
| `extensions.set_config` | `id,config` | 原子替换配置，通知运行中的扩展 |
| `extensions.logs` | `id` | 最近最多 500 条 `logs`，字段为 `time,level,message` |
| `extensions.call` | `id,service,params?` | 调用扩展注册的服务或命令 |
| `extensions.select` | `slot,contribution` 或 `selections` | 选择命名空间贡献 ID，空字符串恢复默认；`selections` 为 `{插槽: 贡献 ID}` 对象，全部校验通过后一起保存，任何一项失败均不生效；值未变化的项不重新校验 |

扩展记录包含 `id,name,version,directory,development,trusted,enabled,state,error,generation,contributes,registrations`。`enabled` 表示持久启用意图，`state` 为实际运行状态；可为 `disabled,starting,running,stopping,failed,blocked`。来源和 UI 贡献只有在运行状态下公开。

后台广播 `extensions.changed`，其字段与注册快照一致。`extensions.error` 表示管理进程级别错误。扩展主动发布的事件使用 `extension.<扩展ID>.<事件名>`，附带 `extensionId` 与 `data`。私有运行时调用、密钥读取和原始音频解析结果不作为通用管理 API 暴露。

## 扩展音乐来源

| 方法 | 参数 | data / 行为 |
| --- | --- | --- |
| `music.sources` | 无 | `sources` 数组，来源 ID 形如 `example.test-source/tones` |
| `music.search` | `source,query,cursor?` | `tracks` 与可选下一页 `cursor`，不写入曲库 |
| `music.track` | `source,id` | 扩展提供的曲目详情 |
| `music.enqueue` | `source,track,play?` | 保存在线身份并追加队列，返回 `queue_id`；默认不切歌 |
| `music.add_to_playlist` | `source,track,playlist_id` | 将在线曲目加入歌单，不更改播放队列 |
| `music.download` | `source,track` | 立即返回 `task`，完成结果通过事件报告 |
| `music.cancel` | `task` | 请求取消尚未结束的下载；最终状态通过 `music.download` 事件报告 |

`track` 必须包含字符串 `id,title`，可包含 `artist,album,duration_ms,cover_url`；时长单位为毫秒且不可为负。搜索、解析和下载在扩展进程中异步执行，不占用数据库写队列。在线身份以来源和曲目 ID 去重，队列仍允许同一歌曲重复出现。

```json
{"id":20,"method":"music.enqueue","params":{"source":"example.test-source/tones","track":{"id":"one","title":"Test tone","artist":"NekoTune","duration_ms":8000},"play":true}}
```

`music.sources_changed` 通知客户端重新获取可用音源。下载状态事件：

```json
{"event":"music.download","task":"...","state":"finished","song_id":42,"message":""}
```

`state` 为 `running,finished,failed,cancelled`。传输进度可包含 `received,total`（字节数，未知总量为 `0`）；完成结果包含 `song_id,path`，可附带来源提供的资源状态或 `asset_warning`。只有文件已校验并成功入库后才发出 `finished`；配套资源失败不撤销音频入库，下载不改变队列或歌单。取消不会删除已成功入库的音频。传输期间不允许同时执行托管文件清理，避免下载与删除竞争。来源失效、解析失败和网络错误保留在线队列项，用户可重新启用来源或重试播放。

### 主题与界面

职责与稳定接口范围见[主题边界规范](theme-boundaries.md)。下列是当前已实现协议，包括外观、界面独立选择与主题壁纸来源；完整默认界面模板仍属于迁移目标。主题 QML 调用已有接口仍须遵守原业务语义，主题职责约定不构成运行时权限沙箱。

- `extensions.select` 的 `slot: "theme"` 接受主题贡献 ID、`builtin/yozakura`、`builtin/ocean`、`builtin/forest`；空字符串选择默认夜樱。持久化失败不改变当前选择。
- `slot: "themeLayout"` 接受运行中且声明了 `shell`、`bottomPlayer`、`background` 或非空 `pages` 的主题贡献 ID；空字符串表示内置界面，键不存在时界面跟随 `theme`。删除主题包时，指向它的 `theme`、`themeLayout` 均重置为空字符串；新前端选择操作总是同时写入两个键。
- `extensions.install` 增加可选 `themeOnly: true`，强制按主题包大小限制解包，并拒绝普通可执行扩展。重复 ID 默认拒绝，确认更新后传 `replace: true`。卡片定向更新可传 `expectedId`：目标必须已安装且与新包 ID 一致，在更改旧包前校验，不匹配返回 `theme_update_id_mismatch`。
- `extensions.export_theme` 参数 `{contribution, path}`：将主题导出到本地 ZIP 文件，返回 `{path, includesQml}`。主题专用包完整导出；内置主题和普通扩展导出独立的配色与静态背景包。支持仍安装的静态主题贡献，以及当前运行时动态注册的主题。
- 主题包沿用 `extensions.enable/disable/reload/uninstall`，启用不需要 `trusted`，仍以 `running` 表示已启用。对已运行主题包调用 `extensions.enable` 并传 `trusted: true` 会记录信任，不会重启；信任按包保存，前端在信任前不生成含代码主题的界面或壁纸入口。普通扩展的授权规则不变；禁用保留选择以便恢复。

前端 `controllers.theme` 提供：

- 状态：`items`、`selectedId`、`activeId`、`layoutSelectedId`、`activeLayoutId`、`layoutUnavailable`、`mode`、`isDark`、`reducedMotion`、`tokens`、`background`、`uiSlots`、`wallpaperSlots`、`busy/error/notice` 等；`items` 每项有 `hasLayout`、`hasWallpaper`。`wallpaperSlots` 以主题贡献 ID 为键，仅包含可用且允许加载的壁纸入口。
- 选择：`selectAppearance(id, trusted = false)`、`selectLayout(id, trusted = false)`；`selectTheme` 对含界面的主题同时设置配色和界面，对不含界面的主题只设置配色。`resetAppearance`、`resetLayout` 分别重置；`reset` 重置配色、界面与深色模式，不重置壁纸偏好。
- 管理：`importTheme/updateTheme/reloadTheme/exportTheme/removeTheme` 走既有扩展管理接口；`setMode` 保存本地 `theme_mode`，`setReducedMotion(bool)` 保存布尔设置 `reduced_motion`（默认 `false`），主题不能覆盖它。
- 信号：`trustRequested(id, target)`，`target` 为 `theme`、`appearance` 或 `layout`，授权后应重试同一操作。主题包的 `selectAppearance` 不请求代码授权；普通扩展仍须先授权。

QML 的 `Theme` 兼容原变量名，并提供 `isDark` 与用户设置的 `reducedMotion`。格式与示例见 [主题制作](themes.md)。

主题可声明 `ui.wallpaper` QML `Item`。用户选择 `wallpaper.style: "theme"` 后加载；`wallpaper.themeWallpaper` 是字符串，空字符串（默认）使用 `uiSlots.wallpaper`，先取界面选择、再取外观选择的壁纸；指定主题贡献 ID 则使用 `wallpaperSlots` 中该来源。入口使用 `ExtensionApi.wallpaper` 的只读播放、歌词、设置及屏幕上下文；无入口或加载失败回退经典卡片并保留选择。`themeStatus` 包括 `loading/active/missing/source-missing/error/safe-mode`，其中 `source-missing` 表示指定来源不可用，恢复后自动接回。它不新增后端 IPC，详见 [主题音乐壁纸](themes.md#主题音乐壁纸) 与 [壁纸状态](wallpaper.md)。


## 听歌历史与统计

记账只由后端确认的播放状态与实际媒体位置驱动，没有客户端上报时长的接口。暂停续播不会创建第二条会话，重复队列项、循环重播和重新点播会创建新的 UUID。播放速度影响媒体位置增长率，`listened_ms` 始终为实际收听毫秒。完整播放要求自然结束且不重复计算的媒体覆盖达到曲目时长的 95%；seek 跳过的进度不计入时长与覆盖。

### `history.list`

参数：`days` 为 `1`、`7` 或 `30`（默认 30，含今天的本地日历日）；`timezone` 为 IANA 时区名（默认后端系统时区，桌面端明确传入自身系统时区）；`search` 为歌名／歌手的字面子串，最多 500 字符；`completed_only` 为布尔值（默认 false）；`limit` 为 1–100（默认 50）；`offset` 为非负整数（默认 0，上限 100000000）。非法类型、范围或时区返回错误。

返回 `items`、`total`、`limit`、`offset`。按 `started_at DESC, id DESC` 排序，各记录包含：

| 字段 | 含义 |
| --- | --- |
| `id` | 会话 UUID，跨重启稳定 |
| `song_id`, `song_key`, `queue_id` | 曲库 ID、稳定歌曲资源身份、当时的队列项 ID；队列项 ID 仅在该后端运行期有意义 |
| `title`, `artist` | 起播时的资料快照，曲库删除后仍保留 |
| `started_at`, `updated_at` | UTC ISO 8601 毫秒时间；进行中为最近确认时间，结束后为结束时间 |
| `listened_ms`, `duration_ms` | 实际收听时长与曲目时长 |
| `completed` | 自然结束且覆盖至少 95% |
| `end_reason` | 空字符串代表进行中；其他为 `ended`、`skipped`、`replaced`、`removed`、`stopped`、`error`、`shutdown`、`interrupted` |

历史按会话开始日期筛选；跨午夜的会话仍是一条，首页每日统计则分配到实际收听日期。数据库使用 UTC epoch 毫秒。查询不会创建会话或重复增加时长。

### `history.stats`

参数仅需可选 `timezone`。返回 `days`（含今天的近 30 天，每项为 `date: YYYY-MM-DD`、`listened_ms`，没有收听的日期补零）、`week_ms`（当地本周一零时至今）、`top_songs`（近 30 天按实际收听时长降序前五首，含 `song_key`、`song_id`、`title`、`artist`、`plays`、`listened_ms`）、`total_sessions`（全部历史会话数）、`timezone`。`plays` 只计入窗口内有实际收听片段的独立会话。统计在数据库聚合，跨日和夏令时按请求时区的实际边界计算。

### `history.clear`

必须传入 `confirm: true`。删除全部会话及收听片段，成功返回空 `data` 并广播 `history.changed`。保留曲库、歌单、`play_count` 与 `last_played_at`；当前轮次也停止写入历史，直到下一次重新起播，避免清空数据被暂停续播重新生成。界面使用确认弹窗，失败保留重试入口。

`history.changed` 表示历史检查点、会话结束或清空已提交，客户端应合并刷新。每 5 秒和播放边界保存实际收听片段；异常退出仅保留最后成功检查点，重启标记为 `interrupted`，从不补算离线时间。首次启用返回空历史，不将旧播放次数折算为历史时长。

## 音乐库维护任务

以下接口仅供本地 IPC 使用；返回 `{task}` 的操作通过 `library.task` 事件报告状态，也可按任务 ID 查询。事件结构为 `{event:"library.task", data:{task,kind,state,phase,files,bytes,total_files?,message?,result?}}`；状态为 `running`、`switching`、`finished`、`failed`、`cancelled` 或 `interrupted`。结果中的逐项失败不会被当作成功导出项。重连后使用 `library.storage_status` 读取正在运行的任务和最近任务。

| 方法 | 参数 | 返回与行为 |
| --- | --- | --- |
| `library.storage_status` | 无 | 根目录、歌曲／对象数量、容量、缺失数量、待迁移数量、迁移问题、任务及回退副本 ID |
| `library.migrate` | 无 | `{task}`；迁移及重试旧引用，成功后清理旧托管文件 |
| `library.verify` | 无 | `{task}`；完整检查登记音频和封面 |
| `library.export` | `song_ids` 或 `playlist_id`，`directory` | `{task}`；独立子目录，原音频、当前歌词、封面，歌单另含 M3U8 |
| `library.backup` | `directory` | `{task}`；目标须在曲库外，结果包含备份路径和缺失资源摘要 |
| `library.restore_preview` | `directory` | `{task}`；完整验证备份，结果含版本、歌曲数、容量、缺失资源 |
| `library.restore` | `preview_task`，`confirmed:true` | `{task}`；重新校验预览对应备份，替换曲库并保留回退副本 |
| `library.task` | `task` | 读取该任务的持久状态 |
| `library.cancel` | `task` | 请求取消当前任务；恢复切换阶段及回退副本清理拒绝取消，事件中 `cancellable:false` |
| `library.remove_rollback` | `id`，`confirmed:true` | `{task}`；清理指定恢复前副本，不影响当前曲库，开始删除后不可取消 |

恢复准备期间拒绝普通写入，仅保留任务查询、取消及状态接口；切换时 IPC 会短暂断开。重连后按原任务 ID 查询最终结果，播放保持停止。备份和导出允许播放；缺失或纯在线歌曲的导出会跳过并报告，恢复不触发联网补下载。详见 [音乐库存储与恢复](library-storage.md)。


### 扩展 PCM 能力

SDK 2.2 的 `IPcmExtensionContext` 是原生可选接口，不新增原始音频 IPC 方法。
主题仍通过 `extensions.call` 调用功能扩展服务，并过滤 `extension.<id>.<event>` 事件。
宿主只提供 PCM，具体频谱服务与帧协议由频谱扩展声明。
