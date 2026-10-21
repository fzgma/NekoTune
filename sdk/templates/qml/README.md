# NekoTune 纯 QML 扩展模板

复制本目录的全部内容（包括 `.github/`）到自己的仓库，修改 `extension.json` 的
ID、名称和版本，在 `Page.qml` 编写页面。无需 C++ 编译器、Qt 开发包或原生 SDK。

```sh
cmake -S . -B build
cmake --build build
```

挂载 `build/extensions/community.qml-hello/` 调试；修改源码后重新构建。
ZIP 位于 `build/packages/`，同一份包可供 Linux/Windows 使用。
增加文件时同步 CMakeLists.txt 的复制规则和 cmake/Package.cmake 的打包列表。

GitHub Actions 提交后自动打包。推送与清单版本一致的 `v<version>` 标签会创建
草稿 Release；用目标宿主验收页面和反复启停后再公开。
纯 QML 仍依赖宿主提供的界面接口，发布说明应列出验收过的宿主版本。
