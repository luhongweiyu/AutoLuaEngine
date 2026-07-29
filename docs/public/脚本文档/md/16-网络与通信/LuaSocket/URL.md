---
params: "无"
returns: "table"
---

**方法名称：** 加载 URL 解析与转义工具模块。

**语法：** `local url = require("socket.url")`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `table` | 包含 `parse`、`build`、`escape`、`unescape`、`absolute` 等 LuaSocket URL 工具。 |

**详细说明：**

模块用于拆解、组合和转义 URL，不会发起网络请求。函数名、参数和返回值遵循 LuaSocket 3.1.0 上游 API。

```lua
local url = require("socket.url")
local parts = assert(url.parse("https://example.com/a?x=1"))
print(parts.scheme, parts.host, parts.path)
print(url.escape("a value"))
```
