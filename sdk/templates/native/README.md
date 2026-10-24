# NekoTune 社区原生扩展模板

复制本目录的**全部内容（包括 `.github/`）**到自己的插件仓库。

模板通过 `find_package(NekoTuneSDK 3 REQUIRED)` 使用 SDK 3.0.0：`activate()` 接收 `sdk::ExtensionContext`，通过 `context.services().add()` 等
能力接口注册功能。原生清单使用 `apiVersion: 3`，SDK 2.x 插件需要按新接口迁移并重新构建。

1. 在 `extension.json` 修改 `id`、名称和版本；ID 发布后保持不变。
2. 在 `plugin.cpp` 编写后台能力，在 `Page.qml` 编写界面。
3. 从目标宿主的 Release 下载对应平台的 `NekoTuneSDK-*.zip` 并解压。
   按其中 `share/NekoTuneSDK/build-info.json` 准备 Qt 和编译器。
4. 配置、构建和测试：

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/sdk
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Qt 不在默认搜索路径时，将其前缀也加入 `CMAKE_PREFIX_PATH`（用分号分隔）。Windows
使用 MSYS2 UCRT64 终端。SDK 会拒绝 Qt 小版本或编译器主版本不匹配的构建。

在 NekoTune「扩展」中挂载 `build/extensions/community.hello/`（改 ID 后路径随之改变），
确认信任并启用。修改 C++ 或 QML 后重新构建，宿主自动重载。
`ctest` 只验证原生库及接口可加载；实际页面、服务和反复启停还应在隔离的宿主中验收。

## 自动构建和发布

在 GitHub 仓库 Settings → Secrets and variables → Actions → Variables 设置
`NEKOTUNE_SDK_TAG`，值为**已经提供 SDK 3 附件**的宿主 Release 标签。下载后确认
`share/NekoTuneSDK/build-info.json` 的 `sdkVersion` 为 `3.0.0`，并核对工具链；不要使用仅有 SDK 2 的旧附件。
将 workflow 的 `@main` 固定到包含 SDK 3 构建流程的宿主发布标签或提交 SHA；SDK 标签与工作流版本应对应。

提交后，Actions 自动构建 Linux/Windows 包，产物在该次运行的 Artifacts 中。
修改清单版本后推送对应的 `v<version>` 标签，自动生成带 ZIP 和 SHA-256 文件的
**草稿 Release**；实际安装验收后再公开。构建和发布不需要宿主源码。

Windows CI 使用 MSYS2 UCRT64 包；如果其滚动更新造成版本不匹配，构建会明确失败，
需要恢复 `build-info.json` 对应的工具链或等待新的宿主 SDK，不能直接发布不匹配的包。
SDK 不包含编译器、Qt 或第三方依赖。额外依赖库需要自行部署到插件 `lib/` 并验证加载。
