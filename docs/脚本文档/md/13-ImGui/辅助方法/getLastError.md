---
params: ""
returns: "string"
---

**方法名称：** 获取当前脚本线程最近一次 ImGui 错误。

**语法：** `imgui.getLastError()`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| 无 | - | - | 本方法不接收参数。 |

| 返回值 | 说明 |
|---|---|
| `string` | 最近一次错误；没有错误时返回空字符串。 |

**详细说明：**

```lua
local handle = imgui.createWindow("窗口", 0, 0, -1, 200, false)
if handle == 0 then
    print(imgui.getLastError())
end
```

错误信息按 native 调用线程保存，成功调用通常会清空该线程之前的错误，因此应在失败后
立即读取。回调运行在内部 Lua 子任务时，也应在回调内读取对应错误。
