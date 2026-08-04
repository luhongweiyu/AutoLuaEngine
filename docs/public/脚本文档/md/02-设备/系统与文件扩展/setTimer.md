---
params: "callback: function, delay: integer, ...: any"
returns: ""
---

**方法名称：** 延时执行回调。

**语法：** `setTimer(callback, delay, ...)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `callback` | `function` | 是 | 延时后执行的 Lua 函数。 |
| `delay` | `integer` | 是 | 等待时间，单位为毫秒。 |
| `...` | `any` | 否 | 传递给回调函数的参数。 |

| 返回值 | 说明 |
|---|---|
| 无 | 创建一个独立 Lua 子线程执行等待和回调。 |

**使用示例：**

```lua
setTimer(function(message)
    print(message)
end, 1000, "timer fired")
```

**详细说明：**

回调在独立 Lua 子线程中执行；脚本结束时由运行时统一回收该线程。
