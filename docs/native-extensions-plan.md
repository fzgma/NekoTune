# C++ 原生扩展迁移方案

> 状态：草案。目标是移除捆绑的 Node.js 运行时，扩展后台改为 Qt 插件（共享库），由后端进程直接加载。QML 界面、清单贡献、主题和信任模型保持不变。

## 1. 背景与范围

当前链路是 `ExtensionBackend`（`backend/infrastructure/extensions`）启动 Node 管理进程 `extensions/runtime/supervisor.mjs`，管理进程再为每个有 `main` 的扩展启动 Worker。两级通信各用一个本地 Socket，在线音频经 `runtime/audio.mjs` 的本地 HTTP 代理交给 Qt Multimedia。

迁移后：

- 后端进程内的 `PluginManager` 实现 `IExtensionBackend`，用 `QPluginLoader` 加载扩展；`ExtensionService`、`MusicService`、`extensions_api.cpp` 与前端 `ExtensionsController` 的协议保持不变。
- 扩展 QML 仍运行在前端进程，通过现有扩展服务调用访问后台；插件共享库只加载到后端进程，不向 QML 注册 C++ 类型。
- 纯主题和纯界面扩展（无 `main`）的加载逻辑不变。

支持平台：Linux x86_64 与 Windows x86_64；不支持 macOS。插件标准只有一套（同一 SDK、清单和源码），每个平台各编译一份共享库，打进同一个包。

不在范围内：安全沙箱、跨编译器 ABI、macOS。

## 2. 现有扩展的 Node 依赖

### 2.1 主仓库示例（`extensions/examples`）

| 扩展 | Node 依赖 | C++ 替代 |
| --- | --- | --- |
| test-source | `node:http` 提供 WAV、`Buffer` | 生成到 `dataDirectory()` 下的临时 WAV，`resolve` 返回 `file://` |
| lyrics | 无 | 继承 `LyricsProvider` |
| automation | `setTimeout`、事件订阅 | `QTimer`、`events().on()` |
| custom-ui | 服务、命令 | `services()`、`commands()` |
| theme-palette / theme-qml | 无代码 | 不变 |

### 2.2 外部插件（`/home/yang/project/NekoTune-extensions`）

外部插件是真正的迁移重点，共约 1800 行后台代码，另有 Project SEKAI 依赖的 `psjk/sekai-music` 库约 720 行 TypeScript。

| 插件 | 主要依赖 | C++ 替代 | 难点 |
| --- | --- | --- | --- |
| 酷狗 `nekotune.kugou` | `fetch`、`fs/promises`、`zlib.inflateSync`（KRC 解压）、`crypto.randomUUID`、`storage`、`host.call('music.download'/'music.cancel'/'music.enqueue')` | `QNetworkAccessManager`、`QSaveFile`、zlib（宿主已链接）、`QUuid` | Worker 会话与登录状态、KRC 解码、封面格式探测 |
| 网易云 `nekotune.netease` | `crypto.createCipheriv('aes-128-cbc')`、`createHash`、`randomBytes`、`BigInt` 模幂（weapi RSA，无填充）、`setTimeout` 预算 | AES 与 1024 位模幂需要 OpenSSL `libcrypto`（`EVP_*`、`BN_mod_exp`），MD5 用 `QCryptographicHash` | Qt 不提供 AES 和大数运算；插件自行链接 `OpenSSL::Crypto`，宿主不新增依赖 |
| 世界计划 `nekotune.sekai` | `sekai-music` 库、`child_process.spawn`（ffmpeg/ffprobe 裁剪静音）、本地 HTTP 服务（下载预处理）、流式写文件 | 移植 `sekai-music` 为 C++ 模块；`QProcess`；下载直接返回预处理后的本地文件 | 工作量最大；本地 HTTP 预处理服务可以删除，见第 6 节 |
| 星穹主题 `starcape` | 无后台代码 | 不变 | 无 |

外部仓库的 `tests/*.test.mjs`（加密测试向量、模拟网络响应）需改写为 QtTest；`tests/plugin_integration_test.cpp` 等已有 C++ 测试改为加载插件共享库。

## 3. 插件 ABI

### 3.1 原则

- 插件只依赖 SDK 头文件，不链接宿主可执行文件中的符号。SDK 只包含纯虚接口、值类型和内联辅助函数，宿主实现全部在 `PluginManager` 内。这样不需要 `-rdynamic`，也避开 `c4212bb` 修过的插件加载作用域符号问题。
- 承诺范围：与宿主同一工具链（Linux 为同一 GCC 主版本；Windows 为与宿主相同的 MSVC 或 MinGW，且同为 Release）、同一 Qt 小版本（例如 6.11.x）、x86_64。不提供 `extern "C"` 层，接口中已有 `QString`、`std::function`、`QObject`，C 层无法解决跨编译器问题。
- IID 带主版本号：`org.nekotune.IExtensionPlugin/2`。接口不兼容时提升版本号，宿主拒绝加载旧插件并在管理页显示原因。

### 3.2 SDK 头文件

SDK 位于 `sdk/include/nekotune/`，是仅含头文件的 CMake 目标 `NekoTune::SDK`，接口说明以头文件注释为准：

| 文件 | 内容 |
| --- | --- |
| `result.h` | `Result<T>`、`AppError`、`failure()`；由 `backend/domain/result.h` 迁入，宿主与插件共用 |
| `sdk/extension_plugin.h` | `IExtensionPlugin::activate(context, done)` / `deactivate()`，IID `org.nekotune.IExtensionPlugin/2` |
| `sdk/extension_context.h` | `IExtensionContext`（宿主调用、服务、命令、UI、来源、事件、存储、凭据、任务、日志）与 `Registration` 句柄 |
| `sdk/providers.h` | `Track`、`SearchPage`、`ResolvedAudio`、`IMusicProvider`、`ILyricsProvider` |

与草案相比的调整：

- 事件发布命名为 `publish`（`emit` 是 Qt 宏）。
- 歌词来源不再继承宿主的 `LyricsProvider`（`QObject`，且 `LyricsDocument::validate()` 实现在宿主内）。SDK 提供与 JS SDK 一致的 `ILyricsProvider::search/resolve`（参数和结果为 `QVariantMap`），宿主继续用 `ExtensionLyricsProvider` 适配。这样 SDK 不含任何 `QObject` 接口，插件也不需要链接宿主符号。
- `registerXxx` 在后端线程上同步校验：ID 非法、重复或 `source` 越出扩展目录时返回 `nullptr`，原因写入扩展日志。从其他线程注册时只能异步校验，失败仅记录日志。
- 服务和命令必须回复对象；`registerService` 不再接受描述，命令描述支持 `title`、`shortcut`。

## 4. 生命周期与安全

### 4.1 信任前不执行代码

1. 扫描和安装只读取 `extension.json`。IID 用 `QPluginLoader::metaData()` 校验，它直接解析 ELF 段，不会 `dlopen`。
2. 用户确认信任并启用后才调用 `load()`，静态构造函数此时才执行。测试 `trustIsRequiredBeforeLoading` 覆盖。
3. 安全模式（`--safe-mode`）不调用 `load()`，行为与现在相同。

### 4.2 停用与库的生命周期

实现时选择**进程内永不 `dlclose`**，原因是插件可能留下 Qt 元对象、定时器或线程局部数据，`unload()` 后任何残留都会变成悬空代码指针，而且无法可靠检测。具体规则：

1. `stop()` 先把运行实例标记为 `stopping`，再调用 `deactivate()`。
2. 随后丢弃该实例保存的所有插件函数对象和 provider 指针，并以“Extension stopped”结束所有待完成的宿主请求。
3. 插件回调、注册句柄都只持有运行实例的 `weak_ptr`，实例销毁后到达的回调直接丢弃。
4. 库保持映射。再次启用时复用同一个插件实例，因此 `deactivate()` 必须把插件恢复到初始状态。
5. 开发目录中的库不会在原路径加载，而是把库所在的**整个目录**按内容哈希复制到 `extensions/runtime/<id>-<hash>/` 后加载：同目录的依赖库仍能找到（Linux 用 `$ORIGIN`，Windows 用 DLL 所在目录），重新编译不会破坏正在运行的映像（Windows 也不会因 DLL 被锁而无法重新编译），重载也能得到新实例。`runtime/` 在下次启动时清空。
6. Windows 不会在 DLL 自身目录查找其依赖。加载期间宿主临时调用 `SetDllDirectoryW(<库目录>)`，`load()` 返回后恢复。延迟加载（delay-load）的依赖不在此范围内，插件应避免使用。

### 4.3 线程模型

- `activate`、`deactivate`、服务、命令、事件和 provider 方法都在后端线程调用。
- 上下文的所有方法线程安全：从其他线程调用时，上下文内部用 `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` 转到后端线程。
- 宿主传给插件的 `done` 可以在任意线程调用，宿主负责切回后端线程，并保证只处理一次。
- 网络请求、解压、ffmpeg 等耗时工作由插件放到自有线程或 `QtConcurrent`；在后端线程阻塞会冻结整个后端。

### 4.4 故障处理

信号处理器捕获 `SIGSEGV` 后无法安全继续运行，因此不做“捕获后标记失败”。改为：

- 崩溃哨兵：调用插件代码前，在配置目录写入当前插件 ID，调用返回后清除。由于后端线程只有一个，哨兵记录的是正在执行的插件。下次启动时若发现哨兵，自动停用该插件，并在管理页显示“上次运行时崩溃”。
- 激活超时：`activate` 的 `done` 在 30 秒内未回调则标记失败并停用。
- 卡死检测：独立线程监测后端事件循环心跳。超过阈值时只记录日志和当前哨兵 ID，不能中断插件。（未实现，见第 9 节）
- 哨兵只在内存中记录（`crash_guard`）。Linux 上由致命信号处理器用 `open/write(2)` 落盘，Windows 上由 `SetUnhandledExceptionFilter` 安装的过滤器用 `CreateFileW/WriteFile` 落盘并交还给先前的过滤器；Linux 上落盘后恢复原处理器并重新抛出信号，Qt 和 sanitizer 的处理器仍会执行。
- 插件代码抛出的 C++ 异常会被捕获，并按故障处理停用该扩展。

## 5. 清单格式

```json
{
  "id": "nekotune.netease",
  "name": "网易云音乐 / NetEase Music",
  "version": "2.0.0",
  "apiVersion": 2,
  "library": {
    "linux-x86_64": "lib/libnekotune-netease.so",
    "windows-x86_64": "lib/nekotune-netease.dll"
  },
  "qt": "6.11",
  "config": { "bitrate": "320" },
  "contributes": { "pages": [], "settings": [] }
}
```

| 字段 | 说明 |
| --- | --- |
| `apiVersion` | `2` 表示原生插件；宿主不再接受 `1`，管理页提示需要更新扩展 |
| `library` | 平台到共享库相对路径的映射；当前平台缺失时扩展不可启用 |
| `qt` | 构建所用 Qt 小版本，与宿主不一致时拒绝加载 |
| `main` | 删除 |
| `nativeModules` | 保留，仅用于前端 QML 原生模块 |

其他字段、依赖图、SemVer 校验和贡献格式不变。纯主题与纯界面扩展可以继续使用 `apiVersion: 1`，因为它们不含后台代码；宿主只拒绝带 `main` 的 `apiVersion: 1` 包。

## 6. 在线音频

现状：`MusicService::resolve` 请求 `extensions.resolve_audio`，Node 代理返回 `http://127.0.0.1:<port>/<token>`，代理负责附加鉴权头、校验 `allowedHosts`、`contentTypes`、`maxBytes`，并探测音频格式。下载同样经过代理。

Qt Multimedia 不能为媒体 URL 附加自定义请求头，因此宿主仍需要本地代理，改为 C++ 实现：

- `infrastructure/extensions/audio_proxy.{h,cpp}`：`QTcpServer` 监听回环地址，`QNetworkAccessManager` 转发请求，支持 `Range`；校验规则、格式探测和 30 分钟空闲回收与 `audio.mjs` 一致。
- `ResolvedAudio.url` 为 `file://` 时不经过代理，直接交给播放器，但路径必须位于该扩展的 `dataDirectory()` 内。
- 下载：`http(s)` 由宿主下载器流式写入托管临时文件；`file://` 由宿主复制或移动后入库。

这样可以删掉世界计划的本地 HTTP 预处理服务：插件在 `resolve(id, "download")` 中下载并调用 ffmpeg 裁剪，返回 `file://`。由于这一过程可能持续数分钟，`resolve` 的超时按 `purpose` 区分，下载默认 10 分钟。test-source 也改为返回 `file://`。

## 7. SDK 与构建

主仓库新增 `sdk/`，安装为 CMake 包：

```cmake
find_package(NekoTuneSDK 2 REQUIRED)
nekotune_add_extension(nekotune-netease
    MANIFEST extension.json
    SOURCES plugin.cpp client.cpp weapi.cpp lyrics.cpp)
target_link_libraries(nekotune-netease PRIVATE OpenSSL::Crypto)
```

`nekotune_add_extension` 负责：创建 `MODULE` 库、链接 `NekoTune::SDK`（仅头文件）、开启 `AUTOMOC`、设置 `-fvisibility=hidden`、写入 Qt 版本到清单、生成 `.nekotune.zip`。

外部仓库改为 CMake 工程，`pnpm build` 改为 `cmake --build`；QML 测试保留，通过 `NEKOTUNE_EXTERNAL_TESTS_DIR` 接入宿主测试工程的方式不变。提供与 CI 相同 GCC、Qt 版本的构建镜像。

ZIP 结构：

```text
nekotune.netease.zip
├── extension.json
├── lib/libnekotune-netease.so
├── lib/nekotune-netease.dll
└── qml/...
```

## 8. 宿主改动

| 位置 | 改动 |
| --- | --- |
| `backend/domain/extensions/extension_backend.h` | 不变 |
| `backend/infrastructure/extensions/extension_backend.*` | 删除，由 `plugin_manager.*` 替代 |
| `backend/infrastructure/extensions/` | 新增 `plugin_manager`、`extension_context_impl`、`package_store`（清单、注册表、安装、回滚、清理）、`theme_validator`、`audio_proxy` |
| `backend/infrastructure/extensions/extension_lyrics_provider.*` | 删除，插件直接注册 `LyricsProvider` |
| `backend/application/extensions/music_service.cpp` | 请求协议不变；`PluginManager` 内部直接调用 `IMusicProvider`，不再序列化 |
| `backend/runtime/backend_session.cpp` | 装配 `PluginManager`；关闭顺序：停用插件 → 停止代理 → 释放服务 |
| `backend/CMakeLists.txt`、打包脚本 | 移除 Node 运行时下载与安装 |
| `extensions/runtime/`、`extensions/sdk/`、`extensions/tools/`、`extensions/tests/`、pnpm workspace | 删除；示例迁移到 `examples/extensions/`（CMake） |
| `docs/extensions.md`、`docs/architecture.md`、`docs/themes.md`、`AGENT.md` | 更新运行时、SDK、构建与验证说明 |

`packages.mjs` 与 `themes.mjs` 的逻辑（清单校验、ZIP 解包、版本目录、回滚、旧包清理、主题校验与导出）用 C++ 重写。ZIP 读写使用宿主已有的 zlib；若需要完整 ZIP 支持，引入 minizip-ng 作为宿主依赖。

## 9. 实施阶段

进度（2026-10-20）：阶段 1 已完成，默认构建仍使用 Node；配置 `-DNEKOTUNE_NATIVE_EXTENSIONS=ON` 切换为原生运行时，且该配置下不会下载 Node。

- 已完成：SDK 头文件与 `nekotune_add_extension`；`PluginManager` 的清单校验、semver、注册表兼容读写、信任、加载、依赖排序与阻塞、重载、开发目录监视、配置、存储、凭据（与 Node 后端共用 `extension_secrets`）、事件、服务、命令、音乐与歌词来源调用、`file://` 音频、崩溃哨兵、安全模式；lyrics、automation、custom-ui 三个示例（`examples/extensions/`）；`nekotune_native_extensions_test`（ASan/UBSan 下通过）。
- 原生模式下尚未支持（调用时返回明确错误）：ZIP 安装、主题校验与导出、HTTP 音频代理和下载，分别属于阶段 2、3；卡死检测。
- 原生模式下 `nekotune_extensions_test` 和 `nekotune_refactor_test` 中有 5 个用例仍依赖 Node 示例或 `main.mjs` fixture，阶段 4 改写。

1. **SDK 与加载**：SDK 头文件、`PluginManager` 的扫描、信任、加载、停用、依赖排序、崩溃哨兵、安全模式；迁移 lyrics、automation、custom-ui 示例。
2. **包管理与主题**：C++ 版安装、更新、回滚、清理、主题校验与导出、开发目录监视与重载。开发目录重载已实现（整个库目录按内容哈希复制到 `runtime/` 后加载）。Windows 上已加载的 DLL 被锁定，清理旧包时须跳过当前会话加载过的目录，下次启动再删。
3. **音频**：C++ `AudioProxy` 与下载器，`file://` 路径；迁移 test-source。
4. **外部插件**：网易云（加密向量测试先行）→ 酷狗 → 世界计划（先移植 `sekai-music`）。
5. **清理**：删除 Node 运行时与 JS 工具链，更新文档与 CI。

阶段 1–3 完成前保留 Node 运行时，两套后端用构建选项 `NEKOTUNE_NATIVE_EXTENSIONS` 切换；阶段 5 删除旧实现和该选项。

## 10. 风险

| 风险 | 缓解 |
| --- | --- |
| 插件崩溃导致后端崩溃 | 崩溃哨兵自动停用、安全模式；这是进程内模型的固有代价，在文档中说明 |
| ABI 与 Qt 版本绑定 | 清单声明 `qt`，宿主拒绝不匹配版本；官方构建镜像；宿主升级 Qt 小版本时需要重新构建所有插件 |
| 第三方开发门槛上升 | 从“写 JS”变为“配置 C++ 工具链”；提供模板和镜像。若以后需要轻量脚本扩展，可另行评估 `QJSEngine` 方案 |
| 卸载时悬空回调 | 每插件注册表与代次令牌；无法确认安全时延迟到重启再释放 |
| 网易云加密依赖 OpenSSL | 插件私有链接系统 `libcrypto`，与 Qt Network 使用的是同一系统库 |
| 世界计划移植量大 | `sekai-music` 先移植并补齐测试，再接入插件 |

## 11. 待定事项

1. 是否接受只支持 `apiVersion: 2` 后台代码，不保留 Node 兼容期（建议一次性切换，迁移期间用构建选项并存）。
2. 世界计划下载裁剪继续依赖系统 ffmpeg/ffprobe，还是改用宿主已有的解码能力。
3. 开发重载复制库目录后，旧映射累积到重启才释放，是否可接受。
4. 官方构建镜像与 CI 的 GCC、Qt 版本基线。
