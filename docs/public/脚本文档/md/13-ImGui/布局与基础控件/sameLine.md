---
params: "handle: integer, spacing: number"
returns: "boolean"
---

**方法名称：** 让指定控件与前一个控件同行。

**语法：** `imgui.sameLine(handle, spacing)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 需要同行显示的控件句柄。 |
| `spacing` | `number` | 是 | 与前一控件的间距；`-1` 使用 Dear ImGui 默认间距。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 设置成功返回 `true`，句柄或间距无效返回 `false`。 |

**详细说明：**

```lua
local first = imgui.createButton(parent, "确定", 160, 54)
local second = imgui.createButton(parent, "取消", 160, 54)
assert(imgui.sameLine(second, 12), imgui.getLastError())
```

水平布局已经自动同行，其中的 `sameLine` 设置不会重复增加间距。
