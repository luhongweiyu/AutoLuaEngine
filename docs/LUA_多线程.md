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

## 验证工程

```text
engines/android/tests/lua/multithread.lua
engines/android/tests/lua/multithread_alpkg
```
