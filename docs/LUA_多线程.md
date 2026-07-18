# Lua 多线程（实现说明）

脚本侧 `m.thread` API 与运行规则见 [脚本文档](脚本文档.md)（分类「多线程」）。

## 当前实现

Android Lua 使用“真实 native 线程 + 共享 Lua VM 子状态”模型：

```text
LuaRuntime 根 lua_State
  ├─ 共享 _G、registry、模块和 Java 回调上下文
  └─ LuaTaskScheduler
       ├─ LuaVmGate：同一时刻只允许一个任务操作 Lua VM
       └─ LuaTask：std::thread + lua_newthread
```

`lua_newthread` 创建的子状态共享同一个 `_G`，每个子状态由独立 `std::thread` 驱动。
Lua VM 本身不并行执行；任务在 `sleep`、UI 等待或 Java 调用期间释放 Gate，其他任务继续
运行。该实现只使用 Lua 5.4.8 公开 API，没有修改 Lua 源码。

核心文件：

```text
engines/android/app/src/main/cpp/runtime/lua/lua_task_scheduler.h
engines/android/app/src/main/cpp/runtime/lua/lua_task_scheduler.cpp
engines/android/app/src/main/cpp/runtime/lua/lua_thread_api.h
engines/android/app/src/main/cpp/runtime/lua/lua_thread_api.cpp
```

Lua 多线程是语言运行时能力，实现在 `libengine.so/runtime/lua`，不进入语言无关的
`system_c_api`。后续 JS 使用自己的事件循环，Go 使用 goroutine；设备 API 仍统一进入
`libengine.so/core/api`。

## 调度与回收（实现）

下列等待点会在参数复制完成后释放 VM Gate，并在返回 Lua 前重新取得 Gate：

- `sleep`
- `m.ui.waitEvent`、`m.hud.waitEvent`、`m.web.waitEvent`
- Java 构造函数、字段、方法和接口回调边界

截图、找色、点击、按键和日志属于短操作，执行期间保持 Gate。

现有每 1000 条 Lua 指令触发的控制 hook 同时承担公平调度：仅当其他任务已经等待
Gate 时，当前纯 Lua 计算任务才让出一次。

`thread:stopThread()` 设置子任务停止标记，并在释放调用者 Gate 后等待目标线程退出。
禁止使用 `pthread_cancel`。主脚本结束时执行最终全量停止和 join。

调度器区分用户子任务和引擎内部子任务。`beginThread` / `newThread` 仍最多同时运行 10 个；
ImGui 事件泵等内部子任务复用同一 VM Gate、停止和 join 流程，但不占用这 10 个公开名额。

## 跨语言 SO API 串行化（讨论结论，暂未实现）

当前 `LuaVmGate` 只保证同一个 Lua VM 内的普通调用串行进入 `libengine.so`。Lua 任务在
`sleep`、UI/ImGui 等待以及 Java 调用边界释放 Gate 后，其他 Lua 任务可以继续执行；未来
JS、Go、FFI 或插件直接调用 C ABI 时，也不会受到 Lua Gate 约束。因此当前实现不能保证
不同语言之间的脚本业务 API 全局串行。

后续研究倾向是在 `libengine.so` 的脚本公开 C ABI 层增加进程级 `EngineApiGate`：

- 截图、找色、找图、OCR、输入和设备等脚本业务 API，由 Lua、JS、Go、FFI 和插件共用
  同一串行入口。
- `sleep`、HTTP、等待 UI/ImGui 事件等阻塞操作在等待期间不持有 EngineApiGate。
- 停止、暂停、状态查询和日志读取必须绕过 EngineApiGate，确保耗时业务 API 执行期间仍
  能立即控制脚本。
- ImGui 渲染、Android Surface 生命周期和 HTTP 传输线程不进入 EngineApiGate，只通过
  事件队列、原子状态或不可变快照与脚本业务层通信。
- 模块内部与渲染线程、控制线程共享的数据仍保留必要的锁；EngineApiGate 不能替代全部
  内部线程安全措施。

该方案目前只记录设计边界，尚未修改现有调度器或 C ABI。实施前需要完整审计 C ABI 的
嵌套调用、Java 回调和所有阻塞入口，避免重复加锁或等待期间持锁造成死锁。

## 验证工程

```text
engines/android/tests/lua/multithread.lua
engines/android/tests/lua/multithread_alpkg
```
