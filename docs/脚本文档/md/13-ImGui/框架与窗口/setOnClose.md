---
params: "handle: integer, callback: function|nil"
returns: "无"
---

**方法名称：** 设置或移除窗口关闭回调。

**语法：** `imgui.setOnClose(handle, callback)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | 窗口句柄。 |
| `callback` | `function` 或 `nil` | 是 | `function(handle)`；传 `nil` 移除原回调。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值。 |

**详细说明：**

```lua
imgui.setOnClose(window, function(handle)
    print("请求关闭窗口：", handle)
    return true
end)
```

回调明确返回 `false` 会取消本次关闭；返回其他值或没有返回值会销毁窗口。回调由 Lua
事件泵持有 VM Gate 执行，不在 Android UI 线程或 OpenGL 渲染线程执行。
