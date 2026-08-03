# Android Java 互操作（实现说明）

脚本侧 `import`、类型规则与 `LuaEngine.*` 用法见
[脚本文档](../../../public/脚本文档.md)（分类「Java」）。

## 调用路线

```text
Lua import / Java userdata / Lua interface callback
    -> 本次 Root/非 Root Worker 的 libengine.so/runtime/lua/java_bridge.cpp
    -> JNI
    -> JavaInteropBridge 反射、重载匹配和类型转换
    -> Android Framework / Java 类 / APK 插件
```

固定自动化 API 继续使用 `core/api -> system_c_api -> 各语言绑定`。`import` 属于
脚本运行时的动态对象能力，不为每个 Java 方法生成一条固定 C ABI。

Root 模式的 JavaVM、`JavaInteropBridge`、APK/Dex ClassLoader、回调代理与 Lua VM 全部位于
uid=0 的一次性 `EngineWorkerMain`。它通过 `ActivityThread.systemMain()` 取得 system Context，再
创建本包 Context 用于 assets、资源和私有路径；非 Root 模式由 `:worker` Service 提供普通
Application Context。Root package Context 可能产生另一套 APK ClassLoader，因此宿主类统一优先
使用实际承载 `EngineWorkerMain`、`JavaInteropBridge` 和 `LuaEngine` 的 ClassLoader，APK/Dex
插件也以该加载器为父级，避免同一宿主类被加载两次并各自持有一份静态状态。两种模式的
`import`、重载和返回规则相同，区别只在进程 UID。

实际 Android 组件实例不搬进 Root Worker：脚本 UI、ImGui Surface、输入法和无障碍节点通过
App UID 的 Provider/Binder 宿主访问。任意 Java/FFI/用户 SO 代码则仍在 Worker 本地执行。

## 文件职责

- `runtime/lua/java_bridge.cpp`：注册 `import`、Java userdata 元方法、Lua/Java 值转换、回调队列和 JNI 对象生命周期
- `runtime/lua/java_bridge.h`：声明 JavaVM 初始化、LuaRuntime 注册、销毁和回调处理入口
- `interop/JavaInteropBridge.java`：类加载、字段、方法、构造函数、重载、数组、集合和接口代理
- `interop/LuaTableValue.java`：保留 Lua table 键值，等确定 Java 目标类型后再转换
- `interop/LuaCallback.java`：保存 Lua registry 引用并从 Java 接口回调 native
- `com/nx/assist/lua/LuaEngine.java`：懒人精灵包名兼容类
- `com/nx/assist/lua/ApkLoader.java`：`LuaEngine.loadApk` 的插件类加载对象；APK 会暂存为私有
  `base.apk`，其中 `lib/<abi>/*.so` 同时解出供类加载器和 FFI 使用

## 回调线程（实现）

Java 回调不会并发访问 `lua_State`：

- Java 调用前会释放 Lua VM Gate，返回 Lua 栈前重新取得 Gate
- 同步接口回调在取得同一 Gate 后于空闲根状态执行
- 异步监听器先入队，由持有 Gate 的 Lua 任务在 hook 或 `sleep` 等待点处理
- JNI 工作线程按需附加到 JavaVM

## 后续语言复用

Java 反射、重载和类型转换集中在 `JavaInteropBridge`。JS 和 Go 接入时复用该后端，
分别提供符合各自语言习惯的对象包装；Lua 的 userdata 和元方法不强加给其他语言。

已在扩展页导入的文件或目录可通过 `LuaEngine.getExtensionPath(relativePath)` 取得私有绝对路径。
该方法只解析安全相对路径，不猜测文件类型、ABI 或 native 依赖顺序；FFI 调用方自行决定加载次序。

脚本会话结束时退出整个 Worker；JNI GlobalRef、ClassLoader、已加载 SO 和 Java/native 回调状态
由进程退出统一回收，不跨下一次脚本运行复用。
