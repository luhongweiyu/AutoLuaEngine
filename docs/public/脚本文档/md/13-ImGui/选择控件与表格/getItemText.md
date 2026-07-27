---
params: "handle: integer, index: integer"
returns: "string 或 nil"
---

**方法名称：** 获取组合框或单选组的选项文本。

**语法：** `imgui.getItemText(handle, index)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 组合框或单选组句柄。 |
| `index` | `integer` | 是 | 从 `0` 开始的选项索引。 |

| 返回值 | 说明 |
|---|---|
| `string` | 指定选项的原始文本，空文本返回空字符串。 |
| `nil` | 控件类型或索引无效。 |

**详细说明：**

```lua
local text = imgui.getItemText(combo, 0)
print(text or imgui.getLastError())
```

表格单元格请使用 `getTableItemText`。
