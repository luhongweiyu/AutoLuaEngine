---
params: "callback: function, ...: any"
returns: "无"
---
<!-- 标准化脚本文档：保留并扩展原有正文，不删除既有说明。 -->

**方法名称：** 启动子线程。

**语法：** `thread.beginThread(callback, ...)`

**参数说明：**

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `callback` | `function` | 是 | 具体取值和组合规则见下方详细说明。 |
| `...` | `any` | 否 | 可继续传入任意数量的附加参数。 |

| 返回值 | 说明 |
|---|---|
| 无 | 此方法不返回值。 |

**详细说明：**

启动脚本子线程，不返回线程对象。

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
- 主脚本结束或 App 请求停止时，引擎会停止并等待全部子线程退出。
- `sleep`、界面事件等待和 Java 调用期间，其他可运行的脚本子任务可以继续执行。
