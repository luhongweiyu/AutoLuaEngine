---
params: "disabled: boolean"
returns: ""
---

**方法名称：** 关闭或恢复普通日志。

**语法：** `setLogOff(disabled)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `disabled` | `boolean` | 是 | `true` 关闭普通 `print` 输出，`false` 恢复输出。 |

| 返回值 | 说明 |
|---|---|
| 无 | 更新当前 Lua 运行时的普通日志开关。 |

**使用示例：**

```lua
setLogOff(true)
print("这条普通日志不会输出")
setLogOff(false)
```

**详细说明：**

该开关只影响普通 `print` 兼容入口；其他明确的日志实现是否输出由其自身接口决定。
