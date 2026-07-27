---
params: "无"
returns: "integer"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 获取EngineApi 地址。

**语法：** `getXiaoyvApi()`

**参数说明：** 无。

| 返回值 | 说明 |
|---|---|
| `integer` | 返回整数；单位和特殊值见下方详细说明。 |

**使用示例：**

```lua
local result = getXiaoyvApi()
print(result)
```

**详细说明：**

返回当前 `:engine` 进程中 `EngineApi*` 的内存地址，供 Lua FFI 加载到同一进程的 SO
调用稳定 C ABI。地址不能跨进程传递或保存到脚本结束后使用；独立插件 SO 应自行通过
`dlsym("engine_getApi")` 获取函数表。
