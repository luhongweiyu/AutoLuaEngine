# Android 平台文档索引

本目录解释内部契约在 Android 上如何实现。先按任务选择对应说明，不要把这里的 Java、JNI、
Root 或 Worker 名称直接当成公开脚本 API。

| 任务 | 说明 |
|---|---|
| 进程、Worker、Root/App UID、日志与退出 | [Android 引擎进程拆分](ANDROID_引擎进程拆分.md)、[Root 模式](ANDROID_ROOT_模式.md) |
| 截图缓存、Root/非 Root 截图与 RGBA 点阵 | [Android SO 截图核心](ANDROID_SO_截图核心.md) |
| Lua runtime asset、`require`、兼容命名空间与平台 JSON 路由 | [Android Lua 兼容层](ANDROID_Lua_兼容层.md) |
| Lua native 线程、VM Gate、等待与停止 | [Lua 多线程](LUA_多线程.md) |
| `import`、Java 对象、回调与线程边界 | [Android Java 互操作](ANDROID_Java互操作.md) |
| 应用、设备、系统控制、剪贴板与 OAID 路由 | [Android 设备 API](ANDROID_设备_API.md) |
| Dialog、HUD、WebView 会话 | [Android 脚本 UI](ANDROID_脚本_UI.md) |
| Dear ImGui Surface、渲染和事件 | [Android ImGui](ANDROID_ImGui.md) |
| 可选 YOLO 运行时、C ABI、Lua `m.yolo` 与模型验收边界 | [Android YOLO 可选运行时调研](ANDROID_YOLO_可选运行时调研.md) |

行为和函数签名仍以[统一 API 契约](../../contracts/API_契约.md)为准。
