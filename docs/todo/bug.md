# bug · 缺陷修复

[返回待办总览](README.md) · 更新：2026-10-25

来源：2026-10-22 后端代码审查与本次验收。行号为审查时位置，修复前先复核；修复时补充能复现原问题的回归测试。

保留原条目编号，优先级与工作量说明见[总览](README.md)。

## 清单

| 状态 | 优先级 | 项目 | 分类 | 原估算 |
| --- | --- | --- | --- | --- |
| 待修复 | P0 | Crossfade 尾部 0／1 帧边界 | 缺陷 | S |
| 待修复 | P0 | 包类型变更沿用信任 | 安全 | S |
| 待修复 | P0 | `AudioProxy::revoke` 遍历中删除 | 缺陷 | S |
| 待修复 | P0 | 恢复回滚与新会话顺序 | 数据 | S–M |
| 待修复 | P1 | 音频代理重定向与下载上限 | 安全 | S–M |
| 待修复 | P1 | 导出目标路径规范化校验 | 安全 | S |
| 待修复 | P1 | 导入／恢复孤儿对象与事务边界 | 数据 | M |
| 待修复 | P1 | 切换输出设备丢失预加载 | 播放 | S |
| 待修复 | P1 | 扩展崩溃归因使用全局状态 | 缺陷 | S |

## 详细说明

9.1 解码线程取消丢失唤醒已随 10.4 环形缓冲修复，见[验收记录](acceptance.md#2026-10-25-内存优化)。

### 9.2 Crossfade 尾部 0／1 帧边界 — P0 · S，已复核

- 位置：`backend/infrastructure/playback/pcm_renderer.cpp:162`、`:227-259`。
- 问题：只判断 `length > 1`，`fadeFrames` 却取 `current->available()`，可能为 0 或 1。为 1 时相位计算除以 0 得到 NaN 并停播；为 0 时走无缝切换分支，`next` 被移走而 `fading` 未清，下一轮空指针。
- 修复：触发条件增加 `current->available() >= 2`；无缝切换分支同时清除 `fading`。
- 本次验收实测：两首各 100 帧的 48 kHz WAV，先渲染当前曲至剩 1／0 帧，再预加载并授权 1 ms Crossfade；分别输出 NaN／以退出码 139 崩溃。现有常规 Crossfade 测试通过，未覆盖该迟到预加载边界。

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
