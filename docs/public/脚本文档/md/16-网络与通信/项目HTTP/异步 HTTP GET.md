---
params: "callback: function, url: string, timeout: number? 或 header: table/string, header: table/string?"
returns: "userdata"
---

**方法名称：** 在线程中发送 HTTP GET 请求。

**语法：** `asynHttpGet(callback, url[, timeout[, header]])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `callback` | `function` | 是 | 请求结束后调用 `callback(body, codeOrError)`。 |
| `url` | `string` | 是 | HTTP 或 HTTPS 地址。 |
| `timeout` | `number?` | 否 | 超时秒数，默认 `30`；此位置可直接传 `header`。 |
| `header` | `table 或 string?` | 否 | 请求头。 |

| 返回值 | 说明 |
|---|---|
| `userdata` | 新建的线程对象，可使用线程接口停止或等待。 |

**详细说明：**

回调的第一个参数为正文；请求失败时为 `nil`。第二个参数在成功时是 HTTP 状态码，失败时是错误文本。
回调在请求线程中执行，避免在其中执行长时间阻塞操作。

```lua
local worker = asynHttpGet(function(body, codeOrError)
    if body then
        print("HTTP", codeOrError, #body)
    else
        print("请求失败", codeOrError)
    end
end, "https://example.com", 10)
```
