---
params: ""
returns: "boolean"
---

**方法名称：** 检查 Debug 构建。

**语法：** `checkIsDebug()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `boolean` | 当前 APK 是否为 Debug 构建。 |

**使用示例：**

```lua
if checkIsDebug() then
    print("debug")
end
```

**详细说明：**

该函数只反映当前应用构建类型，不表示 Root 或其他运行环境是否可用。
