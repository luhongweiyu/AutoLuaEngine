---
params: "无"
returns: "userdata 或 nil, string"
---

**方法名称：** 创建 LuaSocket TCP 对象。

**语法：** `socket.tcp()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `userdata` | TCP socket 对象。 |
| `nil, string` | 创建失败时返回错误。 |

**详细说明：**

对象方法遵循 LuaSocket 3.1.0：常用的有 `settimeout(seconds)`、`connect(host, port)`、`send(data)`、
`receive(pattern)`、`close()`、`getsockname()` 与 `getpeername()`。未设置超时时，网络操作可能长时间
等待；实际脚本应在连接前先调用 `settimeout`。

```lua
local socket = require("socket")
local tcp = assert(socket.tcp())
assert(tcp:settimeout(5))
assert(tcp:connect("example.com", 80))
assert(tcp:send("GET / HTTP/1.0\r\nHost: example.com\r\n\r\n"))
local body, errorMessage, partial = tcp:receive("*a")
tcp:close()
print(body or partial, errorMessage)
```
