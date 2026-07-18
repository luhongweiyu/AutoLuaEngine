---
params: "handle: integer"
returns: "boolean"
---

**方法名称：** 检查窗口、控件或图形句柄是否仍然有效。

**语法：** `imgui.isValidHandle(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 要检查的 ImGui 句柄。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 句柄存在返回 `true`，已销毁、已删除或从未存在返回 `false`。 |

**详细说明：**

```lua
if imgui.isValidHandle(window) then
    imgui.destroyWindow(window)
end
```

脚本结束或引擎重置后，上一脚本保存的所有句柄都失效。
