# ux · 界面与体验

[返回待办总览](README.md) · 更新：2026-10-22

来源：2026-10-22 前端代码审查。聚焦翻译回退、键盘操作与读屏支持。

保留原条目编号，优先级与工作量说明见[总览](README.md)。

## 清单

| 状态 | 优先级 | 项目 | 分类 | 原估算 |
| --- | --- | --- | --- | --- |
| 待开发 | P2 | i18n 回退与统一翻译入口 | 界面 | S |
| 待开发 | P2 | 键盘与读屏可访问性 | 界面 | S–M |

## 详细说明

### 10.4 i18n 回退与统一翻译入口 — P2 · S

- 现状：`zh.json`／`en.json` 键集合一致。`src/i18n.cpp:62-66` 缺键时返回空字符串，界面空白；约 17 组动态拼接键（如 `lyrics_error_` + 代码）静态检查无法覆盖；25 个 QML 文件各自定义 `t()`／`tr()`，并混用 `i18n.text`、`translator.text`。
- 方案：缺键回退英文，再回退键名并记录警告；提供统一翻译入口；增加测试校验两份文件键集合一致及字面量键存在。

### 10.5 键盘与读屏可访问性 — P2 · S–M

- 现状：`LrcLyrics.qml:163`（点击歌词跳转）、`LyricsPanel.qml:312`（歌词候选）仅有 `TapHandler`，无焦点、按键处理与 `Accessible.role`；`ActionMenu.qml`、`ChoiceField.qml`、`InputField.qml`、`Sidebar.qml` 无 `Accessible.*`。
- 方案：改用按钮类控件，或补充 `activeFocusOnTab`、Enter 处理与 `Accessible.name`；遵循 [UI 设计规范](../ui-design-guidelines.md)。
