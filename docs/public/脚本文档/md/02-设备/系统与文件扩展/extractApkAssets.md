---
params: "asset: string, output: string"
returns: ""
---

**方法名称：** 提取 APK assets 文件。

**语法：** `extractApkAssets(asset, output)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `asset` | `string` | 是 | APK `assets/` 内的资源路径。 |
| `output` | `string` | 是 | 输出文件路径，按脚本工作目录解析。 |

| 返回值 | 说明 |
|---|---|
| 无 | 提取成功；失败时抛出 Lua 错误。 |

**使用示例：**

```lua
extractApkAssets("runtime/config.json", "config.json")
```

**详细说明：**

该函数读取当前 APK 自带的 assets 资源，不读取脚本包外部的任意路径。
