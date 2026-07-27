---
params: "handle: integer"
returns: "integer"
---

**方法名称：** 获取选择控件或表格的当前选择索引。

**语法：** `imgui.getSelectedItemIndex(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 组合框、单选组或表格句柄。 |

| 返回值 | 说明 |
|---|---|
| `integer` | `0` 以上为项目或表格行索引，`-1` 表示未选择，`-2` 表示调用失败。 |

**详细说明：**

```lua
local index = imgui.getSelectedItemIndex(combo)
if index >= 0 then print(imgui.getItemText(combo, index)) end
```

表格只返回选中行，不返回列；列索引在 `setOnSelectEventEx` 回调中提供。
