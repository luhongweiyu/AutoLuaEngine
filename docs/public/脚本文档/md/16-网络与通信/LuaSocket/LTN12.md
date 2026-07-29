---
params: "无"
returns: "table"
---

**方法名称：** 加载 LTN12 数据流工具模块。

**语法：** `local ltn12 = require("ltn12")`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `table` | 包含 `source`、`sink`、`filter`、`pump` 等上游 LTN12 工具表。 |

**详细说明：**

LTN12 用可组合的 source/sink 函数处理流式数据，常与 `socket.http.request` 的请求 table 一起使用。
函数名、参数和返回值保持 LuaSocket 3.1.0 上游语义。

```lua
local http = require("socket.http")
local ltn12 = require("ltn12")
local chunks = {}

local ok, code = http.request({
    url = "http://example.com",
    sink = ltn12.sink.table(chunks),
})
assert(ok, code)
print(table.concat(chunks))
```
