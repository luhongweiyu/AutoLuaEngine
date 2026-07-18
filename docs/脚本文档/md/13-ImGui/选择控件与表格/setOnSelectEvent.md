---
params: "handle: integer, callback: function|nil"
returns: "无"
---

**方法名称：** 设置或移除组合框、单选组选择回调。

**语法：** `imgui.setOnSelectEvent(handle, callback)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 组合框或单选组句柄。 |
| `callback` | `function` 或 `nil` | 是 | `function(handle, index, text)`；传 `nil` 移除。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值。 |

**详细说明：**

```lua
imgui.setOnSelectEvent(combo, function(handle, index, text)
    print(handle, index, text)
end)
```

用户选择和 `setItemSelected` 都会触发回调。索引从 `0` 开始，文本是选择发生时的快照。
