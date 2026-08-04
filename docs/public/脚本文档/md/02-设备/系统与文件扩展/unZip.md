---
params: "zipPath: string, outputDirectory: string, password: string?, charset: string?"
returns: ""
---

**方法名称：** 解压 ZIP 文件。

**语法：** `unZip(zipPath, outputDirectory[, password[, charset]])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `zipPath` | `string` | 是 | ZIP 文件路径。 |
| `outputDirectory` | `string` | 是 | 解压输出目录。 |
| `password` | `string` | 否 | 加密 ZIP 的密码。 |
| `charset` | `string` | 否 | 文件名字符集，默认 `UTF-8`。 |

| 返回值 | 说明 |
|---|---|
| 无 | 解压成功；失败时抛出 Lua 错误。 |

**使用示例：**

```lua
unZip("result.zip", "result")
```

**详细说明：**

相对路径按当前脚本工作目录解析。加密压缩包必须提供密码。
