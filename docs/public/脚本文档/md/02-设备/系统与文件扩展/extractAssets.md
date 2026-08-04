---
params: "assetArchive: string, output: string, pattern: string?"
returns: ""
---

**方法名称：** 提取 assets 压缩资源。

**语法：** `extractAssets(assetArchive, output[, pattern])`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `assetArchive` | `string` | 是 | assets 内压缩包资源路径。 |
| `output` | `string` | 是 | 解压输出目录。 |
| `pattern` | `string` | 否 | 条目匹配规则，默认 `*`。 |

| 返回值 | 说明 |
|---|---|
| 无 | 提取成功；失败时抛出 Lua 错误。 |

**使用示例：**

```lua
extractAssets("models.zip", "models", "*.bin")
```

**详细说明：**

输出路径按当前脚本工作目录解析；匹配规则由 Android 运行时处理。
