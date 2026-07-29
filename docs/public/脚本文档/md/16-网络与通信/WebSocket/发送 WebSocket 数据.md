---
params: "handle: integer, text: string"
returns: "boolean"
---

**方法名称：** 向 WebSocket 连接提交一条文本消息。

**语法：** `sendWebSocket(handle, text)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | `startWebSocket` 返回的连接句柄。 |
| `text` | `string` | 是 | 要发送的文本；其他 Lua 值会先转换为字符串。 |

| 返回值 | 说明 |
|---|---|
| `true` | 消息已被连接接受并提交发送。 |
| `false` | 句柄不存在、连接未就绪或已经关闭。 |

**详细说明：**

该接口发送 WebSocket 文本帧。需要发送二进制帧或更底层协议时，使用服务器支持的文本编码，或改用
LuaSocket TCP/UDP 接口实现协议。

```lua
local ok = sendWebSocket(handle, "ping")
if not ok then print("连接不可用") end
```
