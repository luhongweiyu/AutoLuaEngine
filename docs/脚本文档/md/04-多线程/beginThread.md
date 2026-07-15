---
params: "callback: function, ...: any"
returns: "无"
---

启动 native 子线程，不返回线程对象。

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

下列等待点会在参数复制完成后释放 VM Gate，并在返回 Lua 前重新取得 Gate：

- `sleep`
- `m.ui.waitEvent`、`m.hud.waitEvent`、`m.web.waitEvent`
- Java 构造函数、字段、方法和接口回调边界

截图、找色、点击、按键和日志属于短操作，执行期间保持 Gate。网络等后续阻塞能力接入
时必须遵守同一规则：释放 Gate 后不能读取 Lua 栈，重新取得 Gate 后才能写返回值。

现有每 1000 条 Lua 指令触发的控制 hook 同时承担公平调度：仅当其他任务已经等待
Gate 时，当前纯 Lua 计算任务才让出一次。没有子线程时只读取原子等待计数，不执行
额外加锁。
