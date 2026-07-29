# 0007：Android LuaSocket 上游接入

- 状态：已接受
- 日期：2026-07-29

## 背景

此前运行时只用 Android 网络桥接模拟 `ltn12`、`socket.http` 和少量 `socket` 时间函数，不能
提供 LuaSocket 的 TCP、UDP、DNS、`select`、MIME 及标准上层模块。旧项目解出的二进制和 Lua
字节码仅能作为历史线索，不能成为当前实现来源。

## 决定

1. Android 引擎静态编入官方 LuaSocket `v3.1.0` tag 中 `socket.core` 与 `mime.core` 所需源码，
   保留其 MIT 许可证和来源说明。
2. 上游 `ltn12`、`mime`、`socket`、`socket.http`、`socket.url`、`socket.ftp`、`socket.smtp`、
   `socket.tp`、`socket.headers` 作为 UTF-8 runtime assets 注册到 `package.preload`。脚本继续按
   LuaSocket 原有 `require(...)` 名称加载，不新增 `m` 层别名。
3. 现有 `httpGet`、文件传输和 WebSocket 仍通过 `NetworkPlatformBridge`。LuaSocket 不含 TLS，
   因此既有 `require("ssl.https").request(...)` 以及 `socket.http.request(...)` 的直接 HTTPS
   请求继续由 Android 网络层完成；不声称提供 LuaSec 的 `ssl.https.tcp` 或完整 TLS socket API。
4. 原始 TCP/UDP 调用遵循上游 LuaSocket 的同步超时模型。脚本需要在连接、收发前用
   `settimeout` 给出合理边界；不能把它们描述为项目 HTTP 桥接的异步调用。

## 后果

- Android 包体增加 LuaSocket C 核心和标准 Lua 模块，但不依赖旧项目的二进制 SO。
- 公共文档以 LuaSocket 的标准模块/API 为准；项目自身 HTTP/WebSocket API 继续独立维护。
- 后续若要提供真正的 TLS socket 或 LuaSec API，必须另立决策、引入可审计的 TLS 实现并补充
  对应测试，不能把当前 HTTPS request 适配误写成完整 LuaSec。
