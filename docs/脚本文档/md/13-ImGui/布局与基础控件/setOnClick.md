---
params: "handle: integer, callback: function|nil"
returns: "无"
---

**方法名称：** 设置或移除按钮点击回调。

**语法：** `imgui.setOnClick(handle, callback)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 按钮句柄。 |
| `callback` | `function` 或 `nil` | 是 | `function(handle)`；传 `nil` 移除。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值。 |

**详细说明：**

```lua
imgui.setOnClick(button, function(handle)
    print("点击按钮：", handle)
end)
```

同一按钮重复设置会覆盖旧回调。回调在 Lua 事件泵中串行执行，可以直接更新其他控件。
