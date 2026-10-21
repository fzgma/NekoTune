# NekoTune 社区扩展 SDK

社区作者使用目标宿主 Release 中对应平台的 `NekoTuneSDK-*.zip`。
解压后包括 `include/`、`lib*/cmake/NekoTuneSDK/` 和 `share/NekoTuneSDK/`。
SDK 只包含头文件、CMake 工具、模板和构建信息，不包含 Qt 或编译器。

1. 按 `share/NekoTuneSDK/build-info.json` 安装 Qt 与编译器。
   原生插件与宿主使用相同 Qt 小版本和编译器主版本；Windows 使用 MSYS2 UCRT64。
2. 将 `share/NekoTuneSDK/templates/native/` 的全部内容复制到自己的仓库。
3. 按模板 README 构建、挂载调试和发布。用 `CMAKE_PREFIX_PATH` 指向 SDK 解压目录。

纯 QML 扩展直接复制 `templates/qml/`，无需安装原生工具链。
模板中的 `.github/` 和 `.gitignore` 也必须复制，另按自己的代码和资源选择许可证。

## SDK 2.1：自定义随机算法

原生扩展可包含 `<nekotune/sdk/shuffle_provider.h>`，实现 `IShuffleProvider`，通过
`sdk::shuffleContext(context)` 获取可选能力后调用 `registerShuffleStrategy()`。
旧宿主返回空指针，扩展应明确提示需要支持 SDK 2.1 随机算法的宿主。
现有 `IExtensionContext` 虚表、`apiVersion: 2` 和插件 IID 保持兼容，旧扩展无需修改。

选曲在后端线程同步执行，必须快速返回，不可等待网络、宿主回调或自行消费随机状态。
使用请求中的种子和上次已提交的状态计算结果，把下一份 JSON 兼容状态随结果返回。
状态只在队列选择保存成功后提交，仅在本次会话保留；试算、预加载和保存失败均不推进状态。
provider 由扩展持有，必须存活到注册释放；停用时先释放注册再销毁 provider。

完整约定及示例见宿主仓库的 `docs/extensions.md` 和 `examples/extensions/shuffle/`。

## SDK 维护者：独立安装

不需要配置或构建整个宿主：

```sh
cmake -S sdk -B build-sdk -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --install build-sdk --prefix /path/to/nekotune-sdk --component NekoTuneSDK
```

此方式描述的是本地构建环境。面向正式宿主的 SDK 由宿主 CI 从相同构建环境导出，
并随对应 Release 发布；社区应固定宿主 SDK 版本，避免直接依赖持续变化的主分支。
