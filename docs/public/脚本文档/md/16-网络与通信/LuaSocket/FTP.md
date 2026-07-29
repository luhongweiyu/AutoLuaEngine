---
params: "无"
returns: "table"
---

**方法名称：** 加载 LuaSocket FTP 模块。

**语法：** `local ftp = require("socket.ftp")`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `table` | LuaSocket 3.1.0 FTP 模块。 |

**详细说明：**

FTP 模块保留 LuaSocket 上游请求 table 形状，可用于上载、下载和命令请求。其 API 与普通 HTTP 上传
不同；需要简单 HTTP 文件上传时优先使用 `uploadFile`。

```lua
local ftp = require("socket.ftp")
local body, errorMessage = ftp.get("ftp://ftp.example.com/pub/file.txt")
print(body, errorMessage)
```
