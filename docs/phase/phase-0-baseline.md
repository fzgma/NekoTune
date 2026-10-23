# Phase 0：准备与基线

## 目标

建立工作分支，确认项目能构建，记录修改前的测试结果，供后续阶段判断失败是否是新引入的。

## 前置条件

- 已阅读 [README](README.md) 的全部内容，以及[主题边界规范](../theme-boundaries.md) §5.3、§6.2。
- 本机已安装项目根目录 [README](../../README.md) 列出的依赖，并能开启 `NEKOTUNE_ENABLE_WALLPAPER`。

## 步骤

### 0.1 检查工作区

```sh
git status --short
git branch --show-current
```

- 工作区可能有与本计划无关的改动。**不要提交、撤销或修改它们**，记下文件名，之后每次提交前都要确认没有带进去。
- **如果以下任一文件出现在 `git status` 中（已修改或未跟踪），停止并请用户先提交或处理这些改动：**
  - `docs/theme-boundaries.md`、`docs/themes.md`、`docs/API.md`、`docs/wallpaper.md`、`docs/extensions.md`、`README.md`
  - `frontend/qt-qml/i18n/zh.json`、`frontend/qt-qml/i18n/en.json`
  - 各阶段“涉及文件”表中列出的任何源码或测试文件
- `docs/phase/` 本身有改动是正常的，不影响执行。

### 0.2 创建分支

```sh
git switch -c feat/theme-wallpaper-category
```

分支已存在时停止并报告，不要覆盖。

### 0.3 构建

```sh
cmake -S . -B build -DNEKOTUNE_ENABLE_WALLPAPER=ON
cmake --build build -j2
```

构建失败时停止并报告完整错误。

### 0.4 记录测试基线

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
ctest --test-dir build --output-on-failure 2>&1 | tee /tmp/nekotune-wallpaper-baseline.txt
```

记下失败的测试名（没有失败就记“无”）。之后各阶段只要求失败列表不比这里多。

### 0.5 确认关键代码仍在

逐条执行，每条都应该有输出：

```sh
grep -n 'const QStringList uiSlots{"background", "shell", "bottomPlayer", "wallpaper"}' backend/infrastructure/extensions/theme_package.cpp
grep -n '{"includesQml", full && theme.contains("ui")}' backend/infrastructure/extensions/theme_package.cpp
grep -n 'bool providesWallpaper' shared/themes/theme_resolution.cpp
grep -n 'wallpaper.value("ui").toMap().value("wallpaper")' frontend/qt-qml/src/controllers/theme_controller.cpp
grep -n 'item.value("ui").toMap().value("wallpaper").toString()' frontend/qt-qml/src/controllers/theme_controller.cpp
grep -n 'bool WallpaperManager::setOption' frontend/qt-qml/src/wallpaper/wallpaper_manager.cpp
grep -n 'void trustRequested' frontend/qt-qml/src/controllers/theme_controller.h
grep -n 'AppearancePanel { controller: page.controllers.theme' frontend/qt-qml/qml/pages/SettingsPage.qml
grep -n '"wallpaper": "Wallpaper.qml"' examples/extensions/theme-qml/extension.json
```

任何一条没有输出，就停止并报告。

## 完成标准

- [ ] 位于 `feat/theme-wallpaper-category` 分支。
- [ ] 构建成功。
- [ ] 基线已保存到 `/tmp/nekotune-wallpaper-baseline.txt`，并记下失败列表。
- [ ] 0.5 的检查全部有输出。

本阶段不提交。
