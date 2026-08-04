---
params: ""
returns: ""
---

**方法名称：** 启用无障碍环境。

**语法：** `setAccessibilityEnvMode()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| 无 | 请求打开 Android 无障碍服务；失败时抛出 Lua 错误。 |

**使用示例：**

```lua
setAccessibilityEnvMode()
```

**详细说明：**

调用该函数后仍需由系统和用户完成无障碍授权，脚本不能绕过系统授权流程。
