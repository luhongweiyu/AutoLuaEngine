---
params: "账号、密码、收件人、SMTP、认证、主题、正文及可选附件/回调"
returns: "无"
---

**方法名称：** 异步发送邮件和附件。

**语法：** `LuaEngine.sendMail(...)` / `sendMailWithFile(...)` /
`sendMailWithMultiFile(...)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `account` / `password` | `string` | 是 | SMTP 登录账号和密码。 |
| `recipient` | `string` | 是 | 收件地址；多个地址用英文逗号分隔。 |
| `server` | `string` | 是 | SMTP 服务器主机名。 |
| `authentication` | `boolean` | 是 | 是否启用 SMTP AUTH。 |
| `subject` / `content` | `string` | 是 | 主题和 UTF-8 正文。 |
| `attachment` | `string` | 附件方法是 | 单个附件路径。 |
| `attachments` | `string[]` | 多附件方法是 | Java 字符串数组。 |
| `callback` | `IOnMailResult?` | 否 | 实现 `onSuccess()`、`onFailed(message)`。 |

| 返回值 | 说明 |
|---|---|
| 无 | 方法立即返回；发送结果通过可选回调通知。 |

**详细说明：**

三个方法均在后台线程发送，不阻塞当前 Lua 调用。未传回调时失败只写入 Android 日志。
SMTP 连接、读写默认 30 秒超时；端口和加密方式使用 JavaMail 当前 SMTP 默认配置。

```lua
import("com.nx.assist.lua.LuaEngine")

LuaEngine.sendMail(
    "user@example.com",
    "password",
    "receiver@example.com",
    "smtp.example.com",
    true,
    "小鱼精灵",
    "脚本已完成"
)
```
