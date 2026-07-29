---
params: "url: string, onOpened: function?, onClosed: function?, onError: function?, onRecv: function?"
returns: "integer"
---

**方法名称：** 创建 WebSocket 连接并注册事件回调。

**语法：** `startWebSocket(url[, onOpened[, onClosed[, onError[, onRecv]]]])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `url` | `string` | 是 | `ws://` 或 `wss://` 地址。 |
| `onOpened` | `function?` | 否 | 连接打开后调用 `onOpened(handle)`。 |
| `onClosed` | `function?` | 否 | 关闭后调用 `onClosed(handle[, code, reason])`。 |
| `onError` | `function?` | 否 | 失败后调用 `onError(handle[, message, code])`。 |
| `onRecv` | `function?` | 否 | 收到文本或二进制数据后调用 `onRecv(handle, textOrBinaryString)`。 |

| 返回值 | 说明 |
|---|---|
| `integer` | WebSocket 连接句柄，用于发送和关闭。 |

**详细说明：**

函数创建连接后立即返回句柄；真正连接成功或失败通过回调通知。二进制消息会作为 Lua 二进制字符串
传给 `onRecv`。事件回调由连接的事件处理线程执行，应尽快返回。

```lua
local handle = startWebSocket(
    "wss://example.com/socket",
    function(id) print("已连接", id) end,
    function(id, code, reason) print("已关闭", code, reason) end,
    function(id, message) print("错误", message) end,
    function(id, data) print("收到", data) end
)
```
