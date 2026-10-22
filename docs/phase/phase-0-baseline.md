# Phase 0：准备与基线

## 目标

建立工作分支，确认项目能构建，并记录修改前的测试结果，供后续阶段判断失败是不是新引入的。

## 前置条件

- 已阅读 [README](README.md) 的全部内容，以及 [主题边界规范](../theme-boundaries.md) 的 §5.3。
- 本机已安装 README 中列出的依赖（Qt 6.6+、CMake 等），项目根目录 [README](../../README.md) 有完整说明。

## 步骤

### 0.1 检查工作区

```sh
git status --short
git branch --show-current
```

- 工作区可能已有与本计划无关的改动（例如文档修改）。**不要提交、撤销或修改它们。**
- 记下这些文件名，之后每次提交前都要确认没有把它们带进去。
- **如果以下任一文件出现在 `git status` 中（已修改或未跟踪），停止并请用户先提交或处理这些改动**，因为后续阶段会修改并提交它们，否则会把用户自己的改动混进本计划的提交：
  - `docs/theme-boundaries.md`、`docs/themes.md`、`docs/API.md`、`docs/wallpaper.md`、`docs/extensions.md`
  - `frontend/qt-qml/i18n/zh.json`、`frontend/qt-qml/i18n/en.json`
  - 各阶段“涉及文件”表中列出的任何源码或测试文件
- `docs/phase/` 目录本身未跟踪是正常的，不影响执行。

### 0.2 创建分支

```sh
git switch -c feat/theme-split-selection
```

如果分支已存在，停下来询问是否继续使用该分支。

### 0.3 构建

```sh
cmake -S . -B build
cmake --build build -j2
```

构建失败时停止，并报告完整错误。

### 0.4 记录测试基线

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/nekotune-baseline.txt
```

记下输出最后的汇总（通过数、失败数和失败测试名）。**已经失败的测试不是本计划的问题，不要去修。**

### 0.5 确认关键代码存在

逐条执行，每条都应有输出：

```sh
grep -n "QVariantMap resolveTokens" backend/infrastructure/extensions/theme_package.cpp
grep -n "QVariantMap ThemeController::resolve" frontend/qt-qml/src/controllers/theme_controller.cpp
grep -n "Result<QVariantMap> PluginManager::select" backend/infrastructure/extensions/plugin_manager.cpp
grep -n "bool needsTrust() const" backend/infrastructure/extensions/extension_manifest.h
grep -n '"style", "classic"' frontend/qt-qml/src/wallpaper/wallpaper_manager.cpp
grep -n "void themePackagesAndAppearance" tests/extensions_test.cpp
grep -n "void zipPackagesAndThemes" tests/native_extensions_test.cpp
```

任何一条没有输出，就停止并报告。

## 完成标准

- [ ] 位于 `feat/theme-split-selection` 分支。
- [ ] 构建成功。
- [ ] 基线测试结果已保存到 `/tmp/nekotune-baseline.txt`，并记下失败列表。
- [ ] 0.5 的检查全部有输出。

本阶段不提交。
