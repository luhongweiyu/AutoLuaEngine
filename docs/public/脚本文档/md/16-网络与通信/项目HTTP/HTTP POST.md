---
params: "url: string, postdata: string, timeout: number? 或 header: table/string, header: table/string?"
returns: "string, integer, table, string 或 nil, string"
---

**方法名称：** 发送同步表单 HTTP POST 请求。

**语法：** `httpPost(url, postdata[, timeout[, header]])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `url` | `string` | 是 | HTTP 或 HTTPS 地址。 |
| `postdata` | `string` | 是 | 要发送的正文；调用方自行完成表单编码。 |
| `timeout` | `number?` | 否 | 超时秒数，默认 `30`；此位置可直接传 `header`。 |
| `header` | `table 或 string?` | 否 | 请求头 table，或兼容格式的请求头字符串。 |

| 返回值 | 说明 |
|---|---|
| `string, integer, table, string` | 成功发起请求时依次返回正文、状态码、响应头和状态消息。 |
| `nil, string` | 网络、地址或超时失败时返回原因。 |

**详细说明：**

默认正文类型为 `application/x-www-form-urlencoded; charset=utf-8`。需要 JSON 等其他正文类型时，
在 `header` 中覆盖 `Content-Type`，并直接传入对应文本。HTTP 非 2xx 状态仍按正常响应返回。

```lua
local body, code = httpPost(
    "https://example.com/login",
    "name=xiaoyu&token=abc",
    10,
    { ["Content-Type"] = "application/x-www-form-urlencoded; charset=utf-8" }
)
assert(body, code)
print(code, body)
```
