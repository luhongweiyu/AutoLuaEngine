---
params: "handle: integer, callback: function|nil"
returns: "无"
---

**方法名称：** 设置或移除表格单元格选择回调。

**语法：** `imgui.setOnSelectEventEx(handle, callback)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 表格句柄。 |
| `callback` | `function` 或 `nil` | 是 | `function(handle, row, column, text)`；传 `nil` 移除。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值。 |

**详细说明：**

```lua
imgui.setOnSelectEventEx(tableView, function(handle, row, column, text)
    print(handle, row, column, text)
end)
```

用户点击单元格会返回实际行列；`setItemSelected(table, row)` 固定选择第 `0` 列并触发回调。
