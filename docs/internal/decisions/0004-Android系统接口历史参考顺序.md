# 0004：Android 系统接口历史参考顺序

- 状态：已接受
- 日期：2026-07-27

## 背景

`T:\老项目` 同时保留了不同年代的实现。它们可以帮助确认 Android Framework 调用方式，
但旧版本往往带有已经不适合当前架构的兼容分支。若同时拼接两套实现，容易把过时的
Root、无障碍或历史 API 路线重新带回当前引擎。

## 决定

1. 为新增 Android 脚本命令选择系统接口时，先且只先阅读 `T:\老项目` 中较新的项目实现。
2. 只有较新实现不足以判断接口、参数或行为时，才回看较旧项目作为补充证据。
3. 不从较旧项目选择性复制兼容分支；最终实现仍必须遵守当前的
   `core/api -> system_c_api -> AndroidBridge -> Android 平台` 分层和现行 API 契约。
4. 参考完成后，把真正采用的行为写入本项目契约和公开文档，不要求后续 AI 再取得聊天记录
   或重新猜测历史项目优先级。

## 后果

- 当前文本剪贴板能力以较新的 `script_1` 的 Application Context、`ClipboardManager`、
  `getPrimaryClip()` 与 `setPrimaryClip(ClipData.newPlainText(...))` 为参考。
- 老项目仍是补充证据，而不是当前架构、权限路线或公开 API 的权威来源。
