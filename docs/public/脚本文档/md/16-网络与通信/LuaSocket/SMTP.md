---
params: "无"
returns: "table"
---

**方法名称：** 加载 LuaSocket SMTP 模块。

**语法：** `local smtp = require("socket.smtp")`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `table` | LuaSocket 3.1.0 SMTP 模块。 |

**详细说明：**

SMTP 模块保留 LuaSocket 上游 `smtp.send(requestTable)` 和 `smtp.message(messageTable)` 形状。
它使用普通 SMTP socket，不等同于带 TLS 登录流程；需要 Android Java 邮件兼容入口时见 Java 分类。

```lua
local smtp = require("socket.smtp")
local ok, errorMessage = smtp.send({
    from = "sender@example.com",
    rcpt = { "receiver@example.com" },
    source = smtp.message({ body = "hello" }),
})
print(ok, errorMessage)
```
