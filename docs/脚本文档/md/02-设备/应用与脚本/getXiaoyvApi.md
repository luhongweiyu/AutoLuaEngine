---
params: "无"
returns: "integer"
---

返回当前 `:engine` 进程中 `EngineApi*` 的内存地址，供 Lua FFI 加载到同一进程的 SO
调用稳定 C ABI。地址不能跨进程传递或保存到脚本结束后使用；独立插件 SO 应自行通过
`dlsym("engine_getApi")` 获取函数表。
