---
params: "handle: integer"
returns: "number 或 nil"
---

**方法名称：** 获取进度条位置。

**语法：** `imgui.getProgressBarPos(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 进度条句柄。 |

| 返回值 | 说明 |
|---|---|
| `number` | 当前 `0.0..1.0` 浮点进度。 |
| `nil` | 句柄无效或不是进度条。 |

**详细说明：**

```lua
local value = imgui.getProgressBarPos(progress)
print(value)
```

返回核心模型的实际值，不需要等待渲染帧。
