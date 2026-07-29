---
params: "url: string 或 request: table, body: string?"
returns: "string/number, integer, table, string 或 nil, string"
---

**方法名称：** 使用 HTTPS 请求兼容入口。

**语法：** `require("ssl.https").request(urlOrRequest[, body])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `urlOrRequest` | `string 或 table` | 是 | HTTPS URL，或带 `url`、`method`、`headers`、`body`、`source`、`sink` 的请求 table。 |
| `body` | `string?` | 否 | URL 字符串形式下的 POST 正文。 |

| 返回值 | 说明 |
|---|---|
| `string, integer, table, string` | 未指定 `sink` 时返回正文、状态码、响应头和状态消息。 |
| `number, integer, table, string` | 指定 `sink` 并成功写入时通常首项为 `1`。 |
| `nil, string` | 请求、source 或 sink 处理失败时返回原因。 |

**详细说明：**

这是为兼容常见 LuaSocket HTTPS 脚本提供的请求入口，不是 LuaSec。它只提供 `.request` 形状的
HTTPS 请求；不提供 `ssl.https.tcp`、证书上下文或 TLS socket 对象。

```lua
local https = require("ssl.https")
local body, code = https.request("https://example.com")
assert(body, code)
print(code, body)
```
