---
params: "handle: integer"
returns: "integer"
---

**方法名称：** 获取组合框、单选组的选项数或表格行数。

**语法：** `imgui.getItemCount(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 组合框、单选组或表格句柄。 |

| 返回值 | 说明 |
|---|---|
| `integer` | 成功返回项目数或行数；句柄无效或类型不支持返回 `-1`。 |

**详细说明：**

```lua
for index = 0, imgui.getItemCount(combo) - 1 do
    print(index, imgui.getItemText(combo, index))
end
```

空控件返回 `0`。返回 `-1` 时可立即调用 `imgui.getLastError()` 获取原因。
