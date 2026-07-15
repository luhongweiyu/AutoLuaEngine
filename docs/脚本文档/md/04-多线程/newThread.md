---
params: "callback: function, ...: any"
returns: "userdata"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 启动并返回线程。

**语法：** `thread.newThread(callback, ...)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `callback` | `function` | 是 | 具体取值和组合规则见下方详细说明。 |
| `...` | `any` | 否 | 可继续传入任意数量的附加参数。 |

| 返回值 | 说明 |
|---|---|
| `userdata` | 具体字段、特殊值和失败情况见下方详细说明。 |

**详细说明：**

启动并返回线程对象（可调用 `stopThread`）。

小鱼精灵 API：

```lua
m.thread.beginThread(callback, ...)
local thread = m.thread.newThread(callback, ...)
thread:stopThread()
```

懒人精灵兼容 API：

```lua
m.useApi("lr")

beginThread(callback, ...)
local thread = Thread.newThread(callback, ...)
thread:stopThread()
```

规则：

- `callback` 必须是 Lua function，后续参数原样传给回调。
- 同时运行的子线程最多 10 个；超过限制直接产生 Lua 错误。
- `beginThread` 不返回线程对象，`Thread.newThread` 返回可停止对象。
- `_G`、全局 table、`package.loaded` 和已导入 Java 类由所有任务共享。
- 子线程错误只结束该子线程并写入引擎错误日志，不直接结束主脚本。
- 主脚本结束或 App 请求停止时，引擎停止并 `join` 全部子线程后才销毁 Lua VM。

`thread:stopThread()` 设置子任务停止标记，并在释放调用者 Gate 后等待目标线程退出。
目标任务会在指令 hook、`sleep` 或 UI 等待中收到停止请求。禁止使用 `pthread_cancel`，
避免截断 Lua 栈、JNI 引用和 C++ 对象析构。

自然结束的子线程会释放对应 Lua registry 引用；下一次创建线程时回收已经结束的
`std::thread` 句柄。主脚本结束时执行最终全量停止和 join。
