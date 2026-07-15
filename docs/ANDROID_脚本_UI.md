# Android 脚本 UI（实现说明）

脚本侧 dialog / HUD / HTML / `m.ui` 的完整用法、字段与事件见 [脚本文档](脚本文档.md)（分类「界面」）。

## 目标（实现边界）

脚本可按用途选择三类界面：原生弹窗/表单、HUD、HTML。这三类 UI 均由 App 主进程绘制。
Lua 运行在 `:engine` 进程，UI 事件通过 native 会话队列回到原脚本线程，因此 Android UI
线程不会直接进入 Lua VM。

## 统一会话

`libengine.so` 中的 `core/api/ui_api` 为每个界面分配一个整数会话 ID。语言绑定只转换
参数，不保存 Android `View` 或 `Activity`。

```text
Lua API
  -> engine_uiOpen(surface, specJson)
  -> AndroidBridge
  -> App UI 组件

用户操作 / 页面 JS
  -> EngineHttpServer: ui.event
  -> deliverUiEvent(sessionId, type, data)
  -> engine_uiWaitEventInterruptible
  -> Lua event table
```

会话在以下情况关闭：

- 脚本主动调用 `m.ui.close`、`m.hud.hide` 或 `m.web.close`
- 脚本执行结束、请求停止或引擎服务销毁
- HUD 定时关闭、Activity 返回或 HTML 调用 `xiaoyv.close()` 后，脚本会收到 `closed`；
  脚本可再调用关闭方法回收会话

## C ABI

对应跨语言 C ABI 位于：

```text
engines/android/app/src/main/cpp/core/system_c_api.h
```

`EngineApi` 函数表同样包含 UI API，供以后 JS、Go 与插件 so 复用。
