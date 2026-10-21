# 扩展开发与使用

NekoTune 的扩展由 C++ 原生库（Qt 插件）和可选 QML 界面组成。后端进程在用户信任后直接加载扩展库，不需要 Node.js 或其他运行时。扩展支持 Linux 和 Windows x86_64；同一份源码按平台分别编译，可合并为一个安装包。不支持 macOS。

## 社区开发：从模板到发布

社区扩展维护在作者自己的仓库，不需要下载或编译整个宿主。

1. 从目标宿主的 Release 下载对应平台的 `NekoTuneSDK-<宿主版本>-<linux|windows>-x64.zip`
   和 `.sha256`，核对校验值后解压。该附件由新的 CI 流程生成，旧 Release 可能没有。
2. 根据 `share/NekoTuneSDK/build-info.json` 准备 Qt 和编译器。SDK 包只含头文件、
   CMake 工具、模板和环境信息，不包含 Qt 或编译器；安装的 SDK 会在配置阶段拒绝
   平台、Qt 小版本或编译器主版本不匹配的原生工程。
3. 复制 `share/NekoTuneSDK/templates/native/` 的全部内容到自己的仓库，包括 `.github/`
   和 `.gitignore`；修改清单 ID、名称、版本，编写插件和页面。纯 QML 使用
   `templates/qml/`，无须 Qt 开发包和 C++ 编译器。
4. 按模板 README 配置、构建和测试，在宿主挂载 `build/extensions/<id>/` 调试。
   修改源码后重新构建，宿主自动重载；用独立 `NEKOTUNE_HOME` 和 `NEKOTUNE_SOCKET`
   隔离测试。原生模板的加载测试不替代真实宿主的功能、界面及启停验收。
5. 原生插件仓库设置 Actions 变量 `NEKOTUNE_SDK_TAG` 为**已经包含 SDK 附件**的宿主
   Release 标签，并将模板引用的公共 workflow 固定到对应标签或提交 SHA。
   提交后自动构建 Linux/Windows ZIP，供下载验收。
6. 更新清单版本，推送一致的 `v<version>` 标签，模板工作流会创建带 ZIP 和 SHA-256
   的草稿 Release；实际安装和更新验收后再公开。用户下载 ZIP 并从扩展管理页安装。

公共原生构建流程位于 [build-extension.yml](../.github/workflows/build-extension.yml)，
只 checkout 调用它的社区仓库，再从指定宿主 Release 下载 SDK。Linux 按 SDK 信息安装
GCC 与 Qt；Windows 使用 MSYS2 UCRT64，并由 SDK 拒绝不兼容的滚动更新版本。
本流程不提供完整工具链快照；MSYS2 更新到不兼容版本后，需要恢复匹配工具链或等待
新的宿主 SDK，不能将这种失败当成已通过的构建。

SDK 独立安装说明和模板分别见 [SDK README](../sdk/README.md)、
[原生模板](../sdk/templates/native/README.md)和[纯 QML 模板](../sdk/templates/qml/README.md)。
宿主 CI 会从实际宿主构建导出 SDK，再在临时目录移动、解压和构建模板，验证安装包
可独立使用；宿主 Release 同时发布两平台 SDK。插件版本由作者独立管理，发布说明
应列出目标宿主、平台、已验收功能和额外依赖。

## 安装、启用和恢复

打开侧栏「扩展」，导入 ZIP 包，或选择「挂载开发目录」。安装完成后扩展处于停用状态，首次启用需要确认信任。扩展拥有当前用户的完整权限，可以读取文件、访问网络、启动外部程序及替换界面；这不是安全沙箱。

开发目录中的文件变更会触发重载，也可以手动重载。停用会移除扩展的来源、命令、菜单和页面。卸载默认保留配置与数据，开发目录原文件不会被删除。更新采用新的程序目录，启动失败会恢复上一程序版本；扩展自行修改的数据不属于程序版本回滚范围。

更新及依赖扩展重启全部成功后，宿主自动删除不再使用的旧程序包；更新回滚后删除失败的新包。停用期间更新会保留回滚包，直到新版本启用成功或卸载。启动和扩展管理操作结束时也会回收历史遗留包与解压临时目录，仅处理 `packages` 下符合宿主命名规则且未被引用的目录，保留开发目录、配置与数据，并跳过目录链接。清理失败会报告错误并在后续操作或下次启动时重试，不撤销已经成功的更新。当前会话已加载过原生库或 QML 原生模块的程序目录保留到下次启动再清理（Windows 会锁定已加载的 DLL）。

完整界面、首页、曲库、歌单、歌词页、播放栏和主题的替代实现需要在管理页显式选择。`Ctrl+Shift+E` 打开独立管理窗口，其中「恢复默认界面」清除所有替换选择。该窗口使用内置主题。

如果 QML 代码使 GUI 线程无法响应，退出应用后运行：

```sh
./build/nekotune --safe-mode
```

安全模式保留扩展及其数据，不加载扩展库。分离运行时，`nekotune-backend --safe-mode` 控制后端扩展，`nekotune-qt-qml --safe-mode` 跳过该前端的扩展视图；已经运行的另一后端不会被前端启动参数停止。包含 Qt 原生模块的扩展应声明 `nativeModules: true`，其更新需要重启。

如果某个扩展的原生代码让后端崩溃，下次启动时宿主会自动停用它，并在管理页显示「上次运行时崩溃」。原因确认后可重新启用。

主项目只提供通用扩展运行时、SDK、工具与示例，不包含或自动分发第三方音源插件。外部插件单独维护、打包和安装；已有安装、配置和凭据会保留。

## 目录与清单

开发目录与 ZIP 包结构相同：

```text
my-extension/
├── extension.json
├── lib/
│   ├── libmy-extension.so        # Linux
│   └── my-extension.dll          # Windows（可选）
└── Page.qml
```

```json
{
  "id": "my.extension",
  "name": "My extension",
  "version": "1.0.0",
  "apiVersion": 2,
  "qt": "6.11",
  "library": {
    "linux-x86_64": "lib/libmy-extension.so",
    "windows-x86_64": "lib/my-extension.dll"
  },
  "config": { "enabledFeature": true },
  "dependencies": {},
  "contributes": {
    "pages": [
      { "id": "hello", "title": { "zh": "你好", "en": "Hello" }, "source": "Page.qml" }
    ]
  }
}
```

`id` 使用小写字母、数字、点、横线和下划线，以字母开头，长度 2–101。`version` 使用 SemVer。`dependencies` 将扩展 ID 映射到 SemVer 范围（支持 `^`、`~`、比较符、`x` 通配、连字符范围和 `||`），宿主检测版本不匹配、缺失与循环依赖；依赖必须分别被用户信任。

| 字段 | 说明 |
| --- | --- |
| `apiVersion` | 带原生库的扩展必须为 `2`。纯界面扩展与主题包可写 `1` 或 `2`。旧版 Node.js 扩展（带 `main`）不再加载，管理页提示需要更新 |
| `library` | 平台到库文件相对路径的映射。当前平台缺少条目时扩展可以安装，但不能启用 |
| `qt` | 构建所用的 Qt 小版本，必须与宿主一致 |
| `nativeModules` | 扩展 QML 带 Qt 原生模块时声明，更新需要重启 |

没有 `library` 的纯界面扩展只发布清单贡献，仍参与启停、更新与开发重载。QML 文件路径相对于扩展根目录，不能越出目录。所有 UI 根组件使用 `Item` 或其子类；宿主提供顶层窗口。

### ABI 约定

扩展库只依赖 SDK 头文件，不链接宿主程序的符号。由于接口使用 `QString`、`QVariantMap` 和 `std::function`，扩展必须与宿主使用同一套工具链：Linux 上为同一 GCC 主版本，Windows 上与发行版相同的 MSYS2 UCRT64 MinGW；Qt 小版本也必须一致。宿主升级 Qt 小版本后需要重新构建扩展。接口不兼容时 IID（`org.nekotune.IExtensionPlugin/2`）升级，旧库会被拒绝加载。

## 构建与打包

SDK 是仅含头文件的 CMake 包，随宿主安装（`lib/cmake/NekoTuneSDK`），也可以直接使用宿主源码中的 `sdk/` 目录：

```cmake
cmake_minimum_required(VERSION 3.28)
project(MyExtension LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 6.6 REQUIRED COMPONENTS Core)
find_package(NekoTuneSDK 2 REQUIRED)   # 或 add_subdirectory(/path/to/NekoTune/sdk nekotune-sdk)

nekotune_add_extension(my_extension
    MANIFEST extension.json
    SOURCES plugin.cpp
    FILES Page.qml)
```

`nekotune_add_extension` 创建 `MODULE` 库，隐藏非导出符号，开启 `AUTOMOC`，并生成两样东西：

- `build/extensions/<id>/`：填好 `apiVersion`、`qt`、`library` 的清单、`lib/` 下的库和 `FILES`，可在管理页直接挂载。
- 目标 `<target>_package`：生成 `build/packages/<id>-<平台>.nekotune.zip`。

需要同时支持 Linux 和 Windows 时，分别在两个平台构建后合并：

```sh
cmake -DOUTPUT=my.extension.nekotune.zip \
  "-DPACKAGES=linux.zip;windows.zip" -P <SDK>/NekoTuneMergePackages.cmake
```

合并要求 `id`、`version`、`qt` 一致，并合并两份 `library` 映射。开发目录中的库不会在原路径加载：宿主按内容哈希把库所在目录复制到 `config/extensions/runtime/` 后加载，因此可以在扩展运行时重新编译，同目录的依赖库也能被找到。Windows 上加载期间宿主会把库目录加入 DLL 搜索路径；不要依赖延迟加载的 DLL。

ZIP 根目录必须包含 `extension.json`，不额外嵌套目录。宿主拒绝越界路径、链接、加密条目、ZIP64 和超限包：最多 20,000 个条目、总解压大小 512 MiB。

外部项目可以提供自己的 CMake 测试入口，并按需接入宿主目标：

```sh
cmake -S . -B build-external -DNEKOTUNE_EXTERNAL_TESTS_DIR=/path/to/extension-project/tests
cmake --build build-external -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build-external --output-on-failure
```

该选项默认留空，主项目构建和测试不依赖外部目录。QML 扩展测试可复用 `nekotune_slider_quick_test -input /path/to/tests/qml`，其中包含宿主资源和测试夹具。

## 后台 SDK

接口以头文件注释为准：[`extension_plugin.h`](../sdk/include/nekotune/sdk/extension_plugin.h)、[`extension_context.h`](../sdk/include/nekotune/sdk/extension_context.h)、[`providers.h`](../sdk/include/nekotune/sdk/providers.h)、[`shuffle_provider.h`](../sdk/include/nekotune/sdk/shuffle_provider.h)。

```cpp
#include <nekotune/sdk/extension_plugin.h>
#include <QObject>

using namespace nekotune;

class MyExtension final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    void activate(sdk::IExtensionContext &context, std::function<void(Result<void>)> done) override {
        m_pause = context.registerCommand("pause", [&context](const sdk::Params &, sdk::Reply reply) {
            context.hostCall("player.pause", {}, reply);
        }, {{"title", "Pause playback"}, {"shortcut", "Ctrl+Shift+P"}});
        m_hello = context.registerService("hello", [](const sdk::Params &params, sdk::Reply reply) {
            reply(sdk::Params{{"greeting", "Hello, " + params.value("name").toString()}});
        });
        m_track = context.on("player.track_changed", [&context](const sdk::Params &event) {
            context.log(QtInfoMsg, "Now playing: " + event.value("song").toMap().value("title").toString());
        });
        done({});
    }
    void deactivate() override {
        m_pause.reset();
        m_hello.reset();
        m_track.reset();
    }

  private:
    sdk::RegistrationPtr m_pause, m_hello, m_track;
};

#include "plugin.moc"
```

| 能力 | 接口 |
| --- | --- |
| 调用宿主业务 | `hostCall(method, params, done)`，方法见 [API](API.md) |
| 事件 | `on(name, handler)`（`*` 订阅全部），`publish(name, data)`；自定义事件以 `extension.<id>.<name>` 广播 |
| 命令、快捷键 | `registerCommand(id, handler, {title, shortcut})` |
| 扩展间调用 | `registerService`、`callService(extensionId, service, params, done)` |
| 随机算法（SDK 2.1） | `sdk::shuffleContext(context)->registerShuffleStrategy(id, provider, {name})`，先检查能力是否可用 |
| 动态界面贡献 | `registerUi(kind, id, descriptor)`，字段与清单相同 |
| 配置 | `config()`、`updateConfig(config, done)`；变更会作为 `extension.config_changed` 事件送达 |
| 小型 JSON 存储 | `storageGet/storageSet`；总量上限 8 MiB，大文件使用 `dataDirectory()` |
| 凭据 | `secretGet/secretSet/secretDelete`，按扩展命名空间保存到系统密钥库，无明文回退 |
| 任务进度 | `reportTask(taskId, {state, progress, message})` |
| 日志 | `log(level, message)`，进入该扩展的日志环形缓冲区 |
| 系统能力 | Qt 与标准 C++（`QNetworkAccessManager`、`QProcess`、`QFile` 等） |

`registerXxx` 返回 `RegistrationPtr`；释放句柄即撤销注册。在后端线程上注册时，宿主立即校验：ID 非法、重复，或 `source` 越出扩展目录时返回 `nullptr`，原因写入扩展日志。

线程与生命周期约定：

- `activate`、`deactivate`、服务、命令、事件和来源方法都在后端线程调用，必须尽快返回。Qt 的异步对象（网络请求、`QProcess`、定时器）可以直接在后端线程使用；会阻塞的计算和 I/O 放到自己的线程或 `QtConcurrent`，否则整个后端会卡住。
- 上下文的所有方法都可以从任意线程调用，宿主会转到后端线程执行。传给扩展的 `done`/`reply` 也可以从任意线程调用一次，宿主保证结果在后端线程处理。
- 扩展停用后，迟到的回调会被宿主丢弃，不会再调用扩展代码。
- `activate` 必须在 30 秒内调用 `done`，普通调用超时 30 秒，下载解析 10 分钟，`downloaded` 两分钟。
- 库在本次运行中不卸载。再次启用会复用同一个插件对象，因此 `deactivate()` 必须停止自建线程、取消网络请求、释放注册句柄并恢复初始状态。
- 扩展代码抛出的 C++ 异常会被捕获，该扩展按故障停用；随机算法的选曲异常只禁用对应注册并回退至内置洗牌。进程级崩溃无法拦截，由下次启动的自动停用兜底。宿主不检测后台卡死。

服务返回对象；标量或数组应包装为 `{value: ...}`。ZIP 更新在新版本激活失败时恢复旧程序；开发目录自动重载无法还原作者已经覆盖的源文件，修改错误后手动重试即可。

每个扩展保留最近 500 条日志。宿主不输出凭据，SDK 已知的密钥值会在日志中替换；扩展作者仍应避免主动输出未知的敏感数据。

### 自定义随机算法

SDK 2.1 为原生扩展提供独立的可选接口 `IShuffleExtensionContext`，不改变原有 context
虚表、插件 IID 或清单 `apiVersion: 2`。用 `sdk::shuffleContext(context)` 检查宿主能力；
返回空指针时，应提示需要支持 SDK 2.1 随机算法的宿主。纯 QML 扩展不能注册原生算法。

实现 `IShuffleProvider::select(const ShuffleRequest &)`，再注册并保存句柄：

```cpp
auto *shuffle = sdk::shuffleContext(context);
if (!shuffle) {
    done(failure("Requires a host with SDK 2.1 shuffle support"));
    return;
}
m_shuffle = shuffle->registerShuffleStrategy("artist", &m_provider,
                                           {{"name", "Avoid consecutive artists"}});
```

算法完整 ID 为 `扩展ID/算法ID`；描述必须包含非空名称。用户在队列页或抽屉的播放模式菜单
选择“随机”，随后选择算法。算法选择全局保存，不改变播放模式，也不打断当前歌曲。
完整可构建示例见 [`examples/extensions/shuffle/`](../examples/extensions/shuffle/)。

请求的 `queue` 包含条目 ID、歌曲 ID、标题、歌手、专辑、时长、播放次数和最近播放时间；
`currentId` 是当前条目 ID，无选择时为 0。**返回 `queueId`，不能返回歌曲 ID 或数组下标**，
同一歌曲的多个条目是不同候选。允许返回当前条目，即重复播放；非空队列不能返回 0。
空队列不调用扩展。扩展可改变权重和重复规则，宿主仍优先处理“下一首播放”，并负责
上一首与下一首的实际历史回放，这些操作可能不调用算法。

`select` 在后端线程同步执行，必须快速返回，不可等待网络、AI 或宿主回调。
用 `request.seed` 初始化局部随机数生成器，读取 `request.state`，返回
`ShuffleSelection{queueId, nextState}`；状态必须为 JSON 兼容值。相同请求应得到相同结果，
不要修改 provider 自身的随机进度。宿主会为按钮状态和预加载提前试算；只有队列选择保存
成功才提交算法状态和随机进度，保存失败可重复试算。状态仅保留于当前会话，算法切换、
队列替换和扩展重新注册时重置；普通队列编辑会提供更新后的候选，算法需忽略状态中的旧条目。

扩展停用、注销或暂不可用时，保留用户偏好并使用内置 `shuffle_bag`，恢复后自动切回。
返回错误、无效条目或抛出异常会把当前注册标为不可用，原因进入扩展日志；需重新注册或
重载才能恢复。宿主不为同步原生计算提供超时抢占。provider 必须存活到注册释放，
`deactivate()` 中先释放注册再销毁 provider。

程序保存在音乐目录下 `config/extensions/packages`，数据保存在 `config/extensions/data/<id>`，注册状态与界面选择保存在 `config/extensions/registry.json`。这些位置均遵循 `NEKOTUNE_HOME`。

## QML 界面、菜单和主题

```qml
import QtQuick
import QtQuick.Controls
import NekoTune 1.0

Item {
    ExtensionApi { id: api }
    Button {
        text: "Call extension"
        onClicked: api.invoke("hello", {name: "NekoTune"})
            .then(result => text = result.greeting)
            .catch(error => text = String(error))
    }
}
```

`ExtensionApi` 提供 `call(method, params)`、`invoke(service, params)`、宿主事件信号、`app` 业务控制器、`window` 窗口对象、`theme` 主题变量和本地化辅助函数 `t`。`wallpaper` 在主题音乐壁纸中提供只读播放、歌词、设置与屏幕上下文，在其他视图中为 `null`，详见 [主题音乐壁纸](themes.md#主题音乐壁纸)。`ownerId` 是当前扩展 ID。后台不直接持有 QML 对象；多前端可各自加载视图，共享同一扩展后台。

每个扩展视图用独立 QML 引擎管理；重载销毁旧视图与引擎。耗时任务应放在后台，QML 代码仍共享 GUI 线程，不能保证任意 QML 代码的故障隔离。

同目录的 QML 类型可直接使用，子目录用相对路径导入（例如 `import "components"`）。宿主不将每个视图目录额外注册为模块搜索根，避免 Qt 6.8 把多个扩展的同名类型（如 `Translator.qml`）解析为同一个旧式模块。

完整 Shell 可在根 Item 实现 `function focusSearch(): void`，宿主将 `Ctrl+F` 转交给它，让 Shell 聚焦当前页面或抽屉的搜索框。宿主经 C++ 调用该动作，不跨引擎保存页面 JavaScript 对象。主题应从 `ExtensionApi.app` 的 C++ 控制器读取歌曲、队列和模型，避免直接复用另一个 QML 引擎中的 JavaScript 数组或函数对象。

`contributes` 支持以下数组，同类贡献的 `id` 不得重复；公开身份为 `<extensionId>/<id>`：

| 类别 | 字段与用途 |
| --- | --- |
| `pages` | `id,title,source,group`；group 默认 `primary`，也可为 `utility` |
| `settings` | `id,title,source`；显示在「设置 → 扩展设置」，点击进入子页面，不占用主侧栏；默认 Shell 可通过 `ExtensionApi.window.navigate("<extensionId>/<id>")` 打开 |
| `slots` | `id,title,source,slot`；slot 为 `shell`、`bottomPlayer` 或 `page:<内置页面ID>` |
| `themes` | `id,title,tokens`，可带 `schemaVersion:1`、`variants.light/dark`、`author` 与 `ui`；支持配色、背景、QML 界面及可选 `ui.wallpaper` 音乐壁纸，详见 [主题制作](themes.md) |
| `menus` | `id,title,context,command`；context 为 `song` 或 `playlist`，命令收到歌曲或歌单上下文 |
| `toolbars` | `id,title,source,height`；插入默认 Shell 顶部工具栏 |

`title` 可用字符串或 `{zh,en}`。主题通过「设置 → 外观」选择；独立界面替换仍通过扩展管理页选择。当前主题声明的插槽优先，切走主题后恢复独立选择；不可用时回退到内置实现。完整 Shell 自行决定布局；默认 Shell 提供的工具栏和菜单不会自动出现在作者完全替换的界面中。

随机算法的设置贡献可增加 `shuffleStrategy`，值为同一扩展注册的算法局部 ID。
该算法正在生效时，队列页和队列抽屉的播放模式菜单会显示“调整随机算法…”，在按钮附近打开该设置界面。
弹层最大为 420 × 460，可用空间不足时缩小；设置界面应支持滚动，并可在根 Item 发出 `dismissRequested()` 信号关闭弹层。
建议在保存成功后再发出该信号；保存失败时保留草稿和错误信息。扩展撤销或重载时宿主自动关闭并卸载弹层。

## 音乐和歌词来源

```cpp
class Music final : public sdk::IMusicProvider {
  public:
    void search(const QString &query, const QString &cursor,
                std::function<void(Result<sdk::SearchPage>)> done) override {
        sdk::SearchPage page;
        page.tracks.append({"song-1", query, "Example", {}, {}, 180000});
        done(page);
    }
    void track(const QString &id, std::function<void(Result<sdk::Track>)> done) override {
        done(sdk::Track{id, "Example song"});
    }
    void resolve(const QString &id, const QString &purpose,
                 std::function<void(Result<sdk::ResolvedAudio>)> done) override {
        sdk::ResolvedAudio audio;
        audio.url = QUrl("https://example.org/audio.mp3");
        audio.expiresAt = QDateTime::currentMSecsSinceEpoch() + 60'000;
        done(audio);
    }
};
// activate():
m_music = context.registerMusicProvider("music", &m_provider, {{"name", "Example music"}, {"download", true}});
```

搜索返回 `SearchPage{tracks, cursor}`，`cursor` 是扩展自己的分页游标，空字符串表示没有下一页。曲目必须有稳定字符串 `id` 和 `title`，可带 `artist`、`album`、`durationMs`、`coverUrl`。不要把凭据或签名播放地址放入曲目描述。

音源自带浏览页面时，在注册描述中指定 `page`，其值为本扩展 `contributes.pages` 的局部 ID，例如 `{{"name", "Example music"}, {"page", "browse"}}`。未指定时自动匹配与音源局部 ID 相同的页面。页面必须位于默认或 `primary`、`utility` 导航组；设置页和无关页面不算音乐页面。存在对应页面的音源使用自己的入口，不重复出现在通用「扩展音源」中。只有仍有无专属页面的音源时，默认侧栏才显示通用入口；页面撤销后音源重新出现在通用列表。`music.sources` 和 SDK 的完整来源列表保持不变，默认界面使用控制器的 `browserSources` 过滤结果。

`resolve(id, purpose)` 返回 `ResolvedAudio`：`url`、可选 `headers`、毫秒 Unix 时间戳 `expiresAt`，以及下载文件扩展名 `extension`。`purpose` 为 `play` 或 `download`，下载时可以返回更慢但更完整的结果。`url` 可以是：

- `https://`/`http://`：宿主通过本地代理播放、下载，并附加 `headers`。
- `file://`：必须位于该扩展的 `dataDirectory()` 内。播放时直接打开，下载时宿主复制后入库。适合需要预处理（转码、裁切）的来源；扩展应自行清理旧文件。

媒体编码能力由播放后端决定。

来源可通过 `allowedHosts` 限制音频及重定向的 HTTPS 主机，通过 `contentTypes` 限制下载 MIME 类型，通过 `maxBytes` 限制下载大小。可选 `downloaded(id, path, songId, existing, done)` 在音频完成校验和入库后调用，适合补充歌词、封面；调用期间不占用曲库写队列。返回的字段附在 `music.download` 完成事件中，失败只产生 `asset_warning`，已入库音频保留。配套处理限时两分钟。

`music.download` 返回任务 ID，进度事件包含 `received/total`，通过 `music.cancel({task})` 可取消传输。取消后不发布不完整文件；已入库的音频仍保留。

宿主使用本地流代理转发鉴权头和 Range 请求，使用流式背压而非把音频整体读入内存。临时地址和鉴权头不写入数据库，也不出现在普通播放状态中。过期或收到 401/403/410 时重新解析一次，跨域重定向不携带 Authorization/Cookie。下载具有 10 分钟总时限。

在线曲目加入队列或歌单后才保存，以来源和来源曲目 ID 去重；本地音频继续以真实 SHA-256 去重。本地曲库默认不显示纯在线记录。来源故障或停用不会删除队列/歌单项。下载完成后返回本地 `song_id`，不自动改动原有在线引用或播放队列。

歌词实现 `sdk::ILyricsProvider` 并调用 `registerLyricsProvider(id, provider, {{"name", ...}})`。`search(query, done)` 收到 `title`、`artist`、`album`、`duration_ms`、`track_id`，返回 `{candidates:[...]}`，候选可带 `title`、`artist`、`score` 和来源自行定义的字段。`resolve(candidate, done)` 返回 `synced_lyrics`、`krc_lyrics`、`plain_lyrics`、`instrumental`、`cover_url` 等字段。返回现有 LRC/KRC 文本，宿主负责解析、选择和缓存。动态歌词来源出现在既有歌词搜索入口中，不改变本地歌词优先级。

分阶段歌词来源可以让 `resolve` 再次返回 `{candidates:[...]}`；歌曲版本候选标记 `song_result:true`，随后的歌词候选标记为 false 或省略。候选中的访问凭据只保存在后端解析句柄中，不序列化给前端。

## 示例与验证

示例位于 `examples/extensions`，随主项目构建到 `build/extensions/<id>/`，可直接挂载：

- `automation`：睡眠定时器命令和播放事件订阅。
- `lyrics`：可安装的示例 LRC 来源。
- `test-source`：在回环地址现场生成 WAV，要求自定义请求头并支持 Range，用于验证代理、搜索和下载；无需真实音乐账号。
- `custom-ui`：独立页面、设置页、菜单、工具栏、播放栏、主题和完整 Shell。
- `theme-palette`、`theme-qml`：纯配色主题和 QML 主题，见 [主题制作](themes.md)。

验证命令：

```sh
cmake --build build -j2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build --output-on-failure
```

`nekotune_native_extensions_test` 覆盖信任、加载失败、依赖、存储、事件、ZIP 安装、主题导出、音频代理、下载和崩溃记录。`cmake --install build --prefix <directory>` 安装应用、后台和扩展 SDK；Qt 等系统依赖仍由部署环境提供。

主题包使用 `type: "theme"`，仅贡献 `themes`，没有扩展库。纯数据主题可以直接启用；含 QML/JavaScript 时按扩展信任流程启用。`extensions.list` 的条目包含 `type`、`hasCode` 和 `themes`（包含未启用主题的展示描述），`contributes` 仍只包含运行中的贡献。切勿根据 `themes` 列表直接加载 QML。
