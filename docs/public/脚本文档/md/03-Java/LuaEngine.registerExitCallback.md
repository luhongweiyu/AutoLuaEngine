---
params: "callback: IOnExitCallback"
returns: "无"
---

**方法名称：** 通过兼容 `LuaEngine` 注册脚本结束回调。

**语法：** `LuaEngine.registerExitCallback(callback)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `callback` | `IOnExitCallback` | 是 | 单方法接口；Lua 函数会自动转换为接口代理。 |

| 返回值 | 说明 |
|---|---|
| 无 | 注册成功时不返回值；`callback` 为空时抛出参数错误。 |

回调参数和结束码与 `setStopCallBack` 相同：`error` 表示是否异常结束，`exitCode` 为
`0` 正常、`1` 主动停止、`2` 运行错误。

**详细说明：**

同一脚本重复注册时以后一次为准。引擎在 Lua 运行时仍然有效时同步调用并立即清除引用，
不会把 Lua 函数代理保留到下一次脚本运行。若同时注册 `setStopCallBack`，Lua 入口先执行，
随后执行 `LuaEngine` 入口。

**使用示例：**

```lua
import("com.nx.assist.lua.LuaEngine")

LuaEngine.registerExitCallback(function(hasError, exitCode)
    print("Java 兼容结束回调", hasError, exitCode)
end)
```
