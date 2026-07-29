---
params: "无"
returns: "userdata 或 nil, string"
---

**方法名称：** 创建 LuaSocket UDP 对象。

**语法：** `socket.udp()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `userdata` | UDP socket 对象。 |
| `nil, string` | 创建失败时返回错误。 |

**详细说明：**

对象方法遵循 LuaSocket 3.1.0：常用的有 `settimeout(seconds)`、`setsockname(address, port)`、
`setpeername(address, port)`、`send(data)`、`sendto(data, host, port)`、`receive()`、
`receivefrom()` 与 `close()`。

```lua
local socket = require("socket")
local receiver = assert(socket.udp())
assert(receiver:setsockname("127.0.0.1", 0))
assert(receiver:settimeout(2))

local sender = assert(socket.udp())
assert(sender:sendto("hello", "127.0.0.1", select(2, receiver:getsockname())))
local data, host, port = receiver:receivefrom()
print(data, host, port)

sender:close()
receiver:close()
```
