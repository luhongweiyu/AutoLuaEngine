---
params: "handle: integer"
returns: "无"
---

**方法名称：** 销毁窗口及其全部子布局和子控件。

**语法：** `imgui.destroyWindow(handle)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `handle` | `integer` | 是 | `createWindow` 返回的窗口句柄。 |

| 返回值 | 说明 |
|---|---|
| 无 | 本方法没有返回值；失败原因通过 `imgui.getLastError()` 获取。 |

**详细说明：**

```lua
imgui.destroyWindow(window)
assert(not imgui.isValidHandle(window))
```

销毁后窗口及所有后代句柄立即失效，关联的 Lua 回调引用也会同步清理。重复销毁属于错误。
