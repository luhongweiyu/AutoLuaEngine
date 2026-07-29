---
params: "handle: integer"
returns: "boolean"
---

**方法名称：** 请求关闭一个 WebSocket 连接。

**语法：** `closeWebSocket(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | `startWebSocket` 返回的连接句柄。 |

| 返回值 | 说明 |
|---|---|
| `true` | 已向连接提交正常关闭请求。 |
| `false` | 句柄不存在或连接已经不可用。 |

**详细说明：**

关闭是异步过程。若注册了 `onClosed`，最终关闭事件仍会通过该回调报告；关闭后的句柄不能再用于发送。

```lua
if closeWebSocket(handle) then
    print("已请求关闭")
end
```
