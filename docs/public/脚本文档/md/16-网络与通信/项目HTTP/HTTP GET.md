---
params: "url: string, timeout: number? 或 header: table/string, header: table/string?"
returns: "string, integer, table, string 或 nil, string"
---

**方法名称：** 发送同步 HTTP GET 请求。

**语法：** `httpGet(url[, timeout[, header]])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `url` | `string` | 是 | HTTP 或 HTTPS 地址。 |
| `timeout` | `number?` | 否 | 超时秒数，默认 `30`；此位置可直接传 `header`。 |
| `header` | `table 或 string?` | 否 | 请求头 table，或兼容格式的请求头字符串。 |

| 返回值 | 说明 |
|---|---|
| `string, integer, table, string` | 成功发起请求时依次返回正文、HTTP 状态码、响应头 table、状态消息。 |
| `nil, string` | 网络、地址或超时失败时返回原因。 |

**详细说明：**

HTTP 状态码不是 Lua 调用失败：例如服务端返回 404 时仍会得到正文、`404`、响应头和状态消息；
只有请求本身无法完成时才返回 `nil, errorMessage`。该调用同步等待响应。

```lua
local body, code, headers, message = httpGet("https://example.com", 10, {
    ["Accept"] = "text/html",
})
assert(body, code)
print(code, message, headers["Content-Type"])
```
