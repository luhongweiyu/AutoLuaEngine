---
params: "url: string 或 request: table, body: string?"
returns: "string/number, integer, table, string 或 nil, string"
---

**方法名称：** 使用 LuaSocket 形状发送 HTTP 请求。

**语法：** `require("socket.http").request(urlOrRequest[, body])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `urlOrRequest` | `string 或 table` | 是 | URL 字符串，或包含 `url`、`method`、`headers`、`source`、`sink` 等字段的上游请求 table。 |
| `body` | `string?` | 否 | URL 字符串形式下的 POST 正文。 |

| 返回值 | 说明 |
|---|---|
| `string, integer, table, string` | 未指定 `sink` 时返回正文、状态码、响应头和状态行。 |
| `number, integer, table, string` | 指定 `sink` 并成功写入时通常首项为 `1`，其余为状态信息。 |
| `nil, string` | 网络或请求处理失败时返回原因。 |

**详细说明：**

HTTP URL 使用上游 `socket.http` 行为。HTTPS URL 会自动交给本项目的 HTTPS 请求兼容层，但请求
table 的常用形状和返回值保持不变。`source`、`sink` 可配合 `ltn12` 处理流式数据。

```lua
local http = require("socket.http")
local body, code, headers, status = http.request("http://example.com")
assert(body, code)
print(code, status, headers["content-type"])
```
