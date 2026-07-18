---
params: "handle: integer, progress: number"
returns: "无"
---

**方法名称：** 设置进度条位置。

**语法：** `imgui.setProgressBarPos(handle, progress)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 进度条句柄。 |
| `progress` | `number` | 是 | 新进度，低于 `0` 按 `0`，高于 `1` 按 `1`。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；句柄无效时写入错误信息。 |

**详细说明：**

```lua
imgui.setProgressBarPos(progress, 0.75)
```

值会直接进入核心模型，并在下一渲染帧生效。
