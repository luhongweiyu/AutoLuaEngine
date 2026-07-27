---
params: "packageName: string"
returns: "无"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 强制停止指定 Android 应用。

**语法：** `stopApp(packageName)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `packageName` | `string` | 是 | 要查询或操作的 Android 应用包名，例如 com.tencent.mm。 |

| 返回值 | 说明 |
|---|---|
| 无 | 此方法不返回值。 |

**使用示例：**

```lua
stopApp("com.tencent.mm")
```

**详细说明：**

强制停止指定应用。
