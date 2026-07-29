---
params: "无"
returns: "table"
---

**方法名称：** 加载 MIME 编码工具模块。

**语法：** `local mime = require("mime")`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `table` | 包含 Base64、quoted-printable 和流式编码辅助函数的 LuaSocket MIME 模块。 |

**详细说明：**

常用函数包括 `mime.b64(text)`、`mime.unb64(text)`、`mime.qp(text)` 和 `mime.unqp(text)`。
该模块遵循 LuaSocket 3.1.0 上游 API，适合处理邮件或 HTTP 协议中的编码文本。

```lua
local mime = require("mime")
local encoded = mime.b64("小鱼精灵")
print(encoded)
print(mime.unb64(encoded))
```
