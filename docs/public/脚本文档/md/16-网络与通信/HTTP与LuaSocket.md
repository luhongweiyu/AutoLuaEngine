---
params: "见方法表"
returns: "见方法表"
---

**方法名称：** HTTP、文件传输、WebSocket 与 LuaSocket 请求。

**语法：** `httpGet(...)`、`httpPost(...)`、`require("socket.http").request(...)`

**参数说明：**

| 方法 | 参数 | 说明 |
|---|---|---|
| `httpGet(url[, timeout[, header]])` | URL、超时秒数、header 表或头字符串 | `timeout` 位置也可直接传 header |
| `httpPost(url, postdata[, timeout[, header]])` | URL、正文、超时和 header | 正文按表单内容类型发送 |
| `asynHttpGet(callback, url[, timeout[, header]])` | 回调和 GET 参数 | 回调接收 `body, code` |
| `asynHttpPost(callback, url, postdata[, timeout[, header]])` | 回调和 POST 参数 | 回调接收 `body, code` |
| `downloadFile(url, savePath[, progress])` | URL、目标路径、可选函数 | 成功后调用一次 `progress(100)` |
| `uploadFile(url, filePath[, timeout])` | URL、本地文件、超时秒数 | 使用 multipart 字段 `file` |
| `startWebSocket(url, onOpened, onClosed, onError, onRecv)` | URL 和四个可选回调 | 返回连接句柄 |
| `sendWebSocket(handle, text)` / `closeWebSocket(handle)` | 句柄和文本 | 返回是否已提交发送或关闭 |
| `socket.http.request(urlOrOptions)` | URL 字符串或 LuaSocket 请求表 | 同时注册为 `ssl.https` |

| 返回值 | 说明 |
|---|---|
| `string, integer, table, string` | 同步 HTTP 成功返回 `body, code, headers, message`。 |
| `nil, string` | 同步 HTTP 或上传失败返回错误信息。 |
| `userdata` | 异步 HTTP 返回可停止的线程对象。 |
| `boolean` 或 `boolean, string` | 下载、WebSocket 发送和关闭的状态。 |
| `integer` | `startWebSocket` 返回连接句柄。 |

**详细说明：**

`require("socket.http")` 和 `require("ssl.https")` 返回同一请求模块；同时提供常用的
`ltn12.sink.table`、`ltn12.source.string`、`ltn12.pump.all`、`socket.gettime` 和
`socket.sleep`。`ltn12.sink.table([target])` 与标准用法一致，返回 `sink, target`。
这是文档所需的请求兼容层，不等同于完整 LuaSocket TCP/UDP 实现。

LuaSocket 请求统一通过 `require("socket.http")` 或 `require("ssl.https")` 取得模块。

WebSocket 回调分别为：

- `onOpened(handle)`
- `onClosed(handle[, code, reason])`
- `onError(handle[, message, code])`
- `onRecv(handle, textOrBinaryString)`

```lua
local body, code = httpGet("https://example.com", 15, {
    ["User-Agent"] = "Xiaoyv",
})
print(code, body)

local http = require("socket.http")
local body2, code2 = http.request("https://example.com")
print(code2, body2)
```
