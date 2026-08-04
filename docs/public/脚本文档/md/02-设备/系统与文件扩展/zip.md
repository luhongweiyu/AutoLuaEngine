---
params: "source: string, zipPath: string"
returns: ""
---

**方法名称：** 创建 ZIP 压缩包。

**语法：** `zip(source, zipPath)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `source` | `string` | 是 | 要压缩的文件或目录。 |
| `zipPath` | `string` | 是 | 输出 ZIP 文件路径。 |

| 返回值 | 说明 |
|---|---|
| 无 | 压缩成功；失败时抛出 Lua 错误。 |

**使用示例：**

```lua
zip("result", "result.zip")
```

**详细说明：**

相对路径按当前脚本工作目录解析。目标 ZIP 不能与源路径相同，也不能放在待压缩目录内部。
