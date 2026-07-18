---
params: "callback: function"
returns: "boolean"
---

**方法名称：** 将一次性 Lua 函数投递到 ImGui 事件泵。

**语法：** `imgui.post(callback)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `callback` | `function` | 是 | 无参数的一次性回调函数。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | 已进入事件队列返回 `true`；框架未显示或已经关闭返回 `false`。 |

**详细说明：**

```lua
assert(imgui.post(function()
    imgui.setInputText(input, "异步结果")
end), imgui.getLastError())
```

函数只执行一次，执行后引用立即释放。它不会在 Android UI 或 OpenGL 线程执行；回调开始
前会取得 Lua VM Gate。投递失败时函数引用不会残留。
