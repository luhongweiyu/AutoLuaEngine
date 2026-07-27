---
params: "handle: integer, width/height: number"
returns: "无"
---

**方法名称：** 设置内容窗口的大小。

**语法：** `imgui.setWindowSize(handle, width, height)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 窗口句柄。 |
| `width` / `height` | `number` | 是 | 新尺寸，单位为物理像素且必须大于 `0`。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；失败原因通过 `imgui.getLastError()` 获取。 |

**详细说明：**

```lua
imgui.setWindowSize(window, 640, 760)
```

该方法不调整 `showWindow` 外层 Surface 的尺寸；外层尺寸由用户拖动缩放柄修改。
