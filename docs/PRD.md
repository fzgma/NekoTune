# NekoTune 音乐播放器 PRD

本文记录产品定位与早期里程碑，里程碑中的条目不表示全部已实现或尚未实现。当前能力见[项目 README](../README.md)，实施状态与后续优先级见[待办](todo/README.md)，技术实现见[架构文档](architecture.md)。

## 产品定位

NekoTune 是一款本地优先的 Linux／Windows 桌面音乐播放器。

目标：

- 提供流畅、轻量、可扩展的音乐播放体验。
- 使用 C++ 构建核心播放后端。
- 前端与后端完全解耦，便于未来替换 Qt/QML、Web、终端或其他客户端。

产品理念：

> Local first, lightweight, extensible.

## 架构

```text
Frontend Layer
      |
IPC / API Interface
      |
C++ Backend
      |
------------------------------------------
Decoder       Library      Audio Output
FFmpeg / Qt   SQLite       QAudioSink / Qt
```

前端负责 UI 展示、用户交互和状态展示，并承载系统托盘、Linux MPRIS 等桌面集成。后端负责音频播放、队列、音乐库数据与持久化。默认播放链路使用 FFmpeg 解码和 `QAudioSink` 输出，Qt Multimedia 作为可选兼容后端；具体边界见[音频引擎](audio-engine.md)。

播放队列与歌曲资料分开管理：歌曲资料长期保存在 SQLite，队列保存顺序、重复项和当前项，通过歌曲资源关联解析播放路径。后端重启后恢复最近一次队列，用户清空队列后历史歌曲不会自动重新加入。

## 开发阶段

以下保留最初的阶段划分，供理解产品演进方向；继续开发应按当前待办和专题文档复核。例如扩展、主题及 MPRIS 已有实现，Web 前端与远程网关仍待开发。

### Milestone 1

- C++ 后端
- MP3/WAV 播放
- Qt/QML 前端
- 基础播放控制

### Milestone 2

- 本地音乐库
- 目录扫描
- SQLite
- 搜索
- 独立持久歌单：创建、重命名、删除、添加/移除歌曲，歌曲可属于多个歌单
- 歌单播放生成当前播放队列，清空队列不影响歌单

### Milestone 3

- PipeWire 优化
- MPRIS
- 通知
- 媒体快捷键

### Milestone 4

- 插件系统
- Web 前端
- 远程控制
- 主题系统

## 成功标准

- 用户可以自由替换前端。
- C++ 后端可以稳定运行。
- 支持 Linux 主流音频环境。
- 项目可以作为长期维护的开源项目继续发展。
