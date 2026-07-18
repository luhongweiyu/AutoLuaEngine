---
params: ""
returns: "boolean"
---

**方法名称：** 检测当前 Android 环境是否支持 ImGui。

**语法：** `imgui.isSupport()`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| 无 | - | - | 本方法不接收参数。 |

| 返回值 | 说明 |
|---|---|
| `boolean` | `true` 表示设备支持当前 OpenGL ES 3 渲染后端；`false` 表示不支持。 |

**详细说明：**

```lua
assert(imgui.isSupport(), imgui.getLastError())
```

该方法只检查渲染后端能力，不会创建 Surface、渲染线程或申请悬浮窗权限。
