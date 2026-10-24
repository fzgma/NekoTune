# NekoTune 社区扩展 SDK 3

社区作者使用目标宿主 Release 中对应平台的 `NekoTuneSDK-*.zip`。
解压后包括 `include/`、`lib*/cmake/NekoTuneSDK/` 和 `share/NekoTuneSDK/`。
SDK 只包含头文件、CMake 工具、模板和构建信息，不包含 Qt 或编译器。

1. 按 `share/NekoTuneSDK/build-info.json` 安装 Qt 与编译器。
   原生插件与宿主使用相同 Qt 小版本和编译器主版本；Windows 使用 MSYS2 UCRT64。
2. 将 `share/NekoTuneSDK/templates/native/` 的全部内容复制到自己的仓库。
3. 按模板 README 构建、挂载调试和发布。用 `CMAKE_PREFIX_PATH` 指向 SDK 解压目录。

纯 QML 扩展直接复制 `templates/qml/`，无需安装原生工具链。
模板中的 `.github/` 和 `.gitignore` 也必须复制，另按自己的代码和资源选择许可证。

## SDK 3 接口（3.0.0）

原生插件实现 `sdk::IExtensionPlugin`，在 `activate(sdk::ExtensionContext &, sdk::Done)` 中注册能力。
插件 IID 为 `org.nekotune.IExtensionPlugin/3`，带原生库的清单必须使用 `apiVersion: 3`。
SDK 2.x 插件需要迁移接口并重新构建；纯界面和主题清单继续接受 `apiVersion` 1、2、3。

`context.info()` 提供扩展 ID、版本、只读程序目录及可写数据目录。能力按用途分组：

- `host().call()` 调用宿主 API；生命周期、授信、外观和不可逆操作被拒绝时返回 `PermissionDenied`。
- `services().add/call()`、`commands().add()` 注册或调用服务与命令；命令使用 `CommandInfo`。
- `ui().add()` 注册界面贡献；`events().on/publish()` 订阅和发布事件。
- `music().add()`、`lyrics().add()`、`shuffle().add()` 使用各自的强类型来源描述符。
- `config().get/update()`、`storage().get/set()`、`secrets().get/set/remove()` 管理配置、存储与系统密钥库。
- `tasks().report()` 使用 `TaskProgress` 报告任务；`log().write()` 写入经过凭据脱敏的扩展日志。
- `pcm().capabilities/subscribe()` 提供只读 PCM 观察；不支持时返回能力原因。
- `attributeThread()` 为自建线程和 `QtConcurrent` 任务归因，返回的 RAII 对象必须在创建它的同一线程销毁。

保存注册句柄，停用时释放。provider 由扩展持有，必须存活到注册释放。
宿主将回调投递到后端线程；长任务放到扩展自己的工作线程，停用时取消任务并停止线程。
后端线程上的单次调用超过 5 秒告警、30 秒写入卡死哨兵，随机选曲超过 200 毫秒告警；无法归因的连续两次异常退出会触发自动安全模式。详情见[线程与生命周期约定](../docs/extensions.md#后台-sdk)，接口细节以对应头文件为准。

音源解析使用 `ResolvePurpose::Play` 或 `Download`。歌词提供方使用 `LyricsQuery`、
`LyricsCandidate` 和 `LyricsResolution`；分阶段查询返回更多候选，来源自定义字段放在候选的 `extra` 中。

## 自定义随机算法

实现 `IShuffleProvider`，通过 `context.shuffle().add(id, provider, ShuffleInfo)` 注册。
选曲在后端线程同步执行，必须快速返回，不可等待网络、宿主回调或自行消费随机状态。
使用请求中的种子和上次已提交的状态计算结果，把下一份 JSON 兼容状态随结果返回。
状态只在队列选择保存成功后提交，仅在本次会话保留；试算、预加载和保存失败均不推进状态。

## SDK 维护者：独立安装

不需要配置或构建整个宿主：

```sh
cmake -S sdk -B build-sdk -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --install build-sdk --prefix /path/to/nekotune-sdk --component NekoTuneSDK
```

此方式描述的是本地构建环境。面向正式宿主的 SDK 由宿主 CI 从相同构建环境导出，
并随对应 Release 发布；社区应固定宿主 SDK 版本，避免直接依赖持续变化的主分支。

## 播放器 PCM 观察接口

`context.pcm().capabilities()` 返回当前后端与可用原因；`subscribe(handler)` 返回注册句柄。
这两个方法及回调运行在后端线程。回调必须尽快返回，FFT 等计算放到扩展自己的工作线程。

PCM 为带采样率、双声道、流代次、输出帧位置和媒体时间的交错 float 数据；来源是默认 FFmpeg
后端混音、音效和播放器音量后的分析副本，只在设备消费之后投递。输出线程不执行扩展代码。
队列有上限，可以丢帧；按流代次和帧位置重置分析。取消订阅立即阻止后续回调，停用时自动清理。
Qt 兼容后端不支持此能力。线程与格式细节以头文件注释为准。
