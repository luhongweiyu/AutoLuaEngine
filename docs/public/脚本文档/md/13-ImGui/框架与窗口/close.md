---
params: ""
returns: "无"
---

**方法名称：** 关闭当前 ImGui 框架。

**语法：** `imgui.close()`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| 无 | - | - | 本方法不接收参数。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值，可重复调用。 |

**详细说明：**

```lua
imgui.setOnClick(closeButton, function()
    imgui.close()
end)
```

关闭会移除 Surface 并让阻塞中的 `show(true)` 返回，但不会销毁控件模型和句柄。同一脚本
可以再次调用 `show` 或 `showWindow` 显示原有控件；脚本结束时才会统一释放全部资源。
