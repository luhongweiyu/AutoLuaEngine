---
params: "enabled: boolean"
returns: ""
---

**方法名称：** 设置 Root 环境模式。

**语法：** `setRootEnvMode(enabled)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `enabled` | `boolean` | 是 | `true` 请求启用 Root 环境，`false` 请求关闭。 |

| 返回值 | 说明 |
|---|---|
| 无 | 请求切换当前脚本的运行环境；失败时抛出 Lua 错误。 |

**使用示例：**

```lua
setRootEnvMode(true)
```

**详细说明：**

该函数只切换运行环境策略，不会为脚本重复申请或保存 `su` 会话。
