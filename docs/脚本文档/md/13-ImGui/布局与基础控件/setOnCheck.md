---
params: "handle: integer, callback: function|nil"
returns: "无"
---

**方法名称：** 设置或移除复选框、开关状态回调。

**语法：** `imgui.setOnCheck(handle, callback)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 复选框或开关句柄。 |
| `callback` | `function` 或 `nil` | 是 | `function(handle, checked)`；传 `nil` 移除。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值。 |

**详细说明：**

```lua
imgui.setOnCheck(enabled, function(handle, checked)
    print(handle, checked)
end)
```

重复设置会覆盖原回调。用户点击和 `setChecked` 修改都会投递事件。
