# Phase 0：准备与基线

## 目标

建立工作分支，确认项目能构建，记录修改前的测试结果和关键代码位置，供后续阶段判断失败是否为新引入的。

## 前置条件

- 已阅读 [README](README.md) 的全部内容，以及[扩展系统重构方案](../extensions-refactor-plan.md)。
- 本机已安装项目根目录 [README](../../README.md) 列出的依赖，并能开启 `NEKOTUNE_ENABLE_WALLPAPER`。

## 步骤

### 0.1 检查工作区

```sh
git status --short
git branch --show-current
```

- 工作区可能有与本计划无关的改动。**不要提交、撤销或修改它们**，记下文件名，之后每次提交前都要确认没有带进去。
- **如果以下任一路径出现在 `git status` 中（已修改或未跟踪），停止并请用户先提交或处理：**
  - `backend/infrastructure/extensions/`、`backend/domain/extensions/`、`backend/application/extensions/`
  - `backend/runtime/backend_session.{h,cpp}`、`backend/ipc/api/extensions_api.cpp`、`backend/CMakeLists.txt`
  - `sdk/`、`examples/extensions/`、`tests/native_extensions*`、`tests/extensions_test.cpp`、`tests/CMakeLists.txt`
  - `frontend/qt-qml/src/controllers/extensions_controller.*`、`frontend/qt-qml/src/controllers/theme_controller.*`
  - `docs/extensions.md`、`docs/API.md`、`docs/architecture.md`
- `docs/phase/` 与 `docs/extensions-refactor-plan.md` 本身有改动是正常的，不影响执行。

### 0.2 创建分支

```sh
git switch -c refactor/extensions
```

分支已存在时停止并报告，不要覆盖。

> Phase 1 可以单独合并：如果用户要求先发布越权修复，在 Phase 1 提交后由用户决定是否把该提交 cherry-pick 到 `main`，执行者不要自行操作。

### 0.3 构建

```sh
cmake -S . -B build -DNEKOTUNE_ENABLE_WALLPAPER=ON
cmake --build build -j2
```

构建失败时停止并报告完整错误。

### 0.4 记录测试基线

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/nekotune-extensions-baseline.txt
```

记下失败的测试名（没有失败就记“无”）。之后各阶段只要求失败列表不比这里多。

再单独记录两个扩展测试的函数级结果，后续阶段对照用：

```sh
QT_QPA_PLATFORM=offscreen ./build/tests/nekotune_native_extensions_test 2>&1 | tail -5
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./build/tests/nekotune_extensions_test 2>&1 | tail -5
```

### 0.5 记录规模

```sh
wc -l backend/infrastructure/extensions/*.cpp backend/infrastructure/extensions/*.h | tee /tmp/nekotune-extensions-size.txt
```

### 0.6 确认关键代码仍在

逐条执行，每条都应该有输出：

```sh
grep -n 'void hostCall(const QString &method, const sdk::Params &params, sdk::Reply done) override' backend/infrastructure/extensions/plugin_manager.cpp
grep -n 'Defer lifecycle mutations until activation has completed' backend/infrastructure/extensions/plugin_manager.cpp
grep -n 'm_extensionBackend.start(' backend/runtime/backend_session.cpp
grep -n 'void PluginManager::providerCall' backend/infrastructure/extensions/plugin_manager.cpp
grep -n 'void PluginManager::resolveAudio' backend/infrastructure/extensions/plugin_manager.cpp
grep -n '"extensions.provider_call"' backend/application/extensions/music_service.cpp backend/infrastructure/extensions/extension_lyrics_provider.cpp
grep -n '"extensions.download_audio"' backend/application/extensions/music_service.cpp
grep -n 'struct PluginManager::Run' backend/infrastructure/extensions/plugin_manager.cpp
grep -n 'class PluginManager::Context final' backend/infrastructure/extensions/plugin_manager.cpp
grep -n 'struct ExtensionManifest' backend/infrastructure/extensions/extension_manifest.h
grep -n 'NEKOTUNE_EXTENSION_IID "org.nekotune.IExtensionPlugin/2"' sdk/include/nekotune/sdk/extension_plugin.h
grep -n 'project(NekoTuneSDK VERSION 2.2.0' sdk/CMakeLists.txt
grep -nF 'string(JSON json SET "${json}" apiVersion 2)' sdk/cmake/NekoTuneExtension.cmake
grep -n 'void servicesStorageEventsAndSources()' tests/native_extensions_test.cpp
grep -n 'request("extensions.select"' frontend/qt-qml/src/controllers/theme_controller.cpp frontend/qt-qml/src/controllers/extensions_controller.cpp
```

任何一条没有输出，就停止并报告。

## 完成标准

- [ ] 位于 `refactor/extensions` 分支。
- [ ] 构建成功。
- [ ] 基线已保存到 `/tmp/nekotune-extensions-baseline.txt`，并记下失败列表。
- [ ] 规模已保存到 `/tmp/nekotune-extensions-size.txt`。
- [ ] 0.6 的检查全部有输出。

本阶段不提交。
