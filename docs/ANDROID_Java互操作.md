# Android Java 互操作（实现说明）

脚本侧 `import`、类型规则与 `LuaEngine.*` 用法见 [脚本文档](脚本文档.md)（分类「Java」）。

## 调用路线

```text
Lua import / Java userdata / Lua interface callback
    -> libengine.so/runtime/lua/java_bridge.cpp
    -> JNI
    -> JavaInteropBridge 反射、重载匹配和类型转换
    -> Android Framework / Java 类 / APK 插件
```

固定自动化 API 继续使用 `core/api -> system_c_api -> 各语言绑定`。`import` 属于
脚本运行时的动态对象能力，不为每个 Java 方法生成一条固定 C ABI。

## 文件职责

- `runtime/lua/java_bridge.cpp`：注册 `import`、Java userdata 元方法、Lua/Java 值转换、回调队列和 JNI 对象生命周期
- `runtime/lua/java_bridge.h`：声明 JavaVM 初始化、LuaRuntime 注册、销毁和回调处理入口
- `interop/JavaInteropBridge.java`：类加载、字段、方法、构造函数、重载、数组、集合和接口代理
- `interop/LuaTableValue.java`：保留 Lua table 键值，等确定 Java 目标类型后再转换
- `interop/LuaCallback.java`：保存 Lua registry 引用并从 Java 接口回调 native
- `com/nx/assist/lua/LuaEngine.java`：懒人精灵包名兼容类
- `com/nx/assist/lua/ApkLoader.java`：`LuaEngine.loadApk` 的插件类加载对象

## 回调线程（实现）

Java 回调不会并发访问 `lua_State`：

- Java 调用前会释放 Lua VM Gate，返回 Lua 栈前重新取得 Gate
- 同步接口回调在取得同一 Gate 后于空闲根状态执行
- 异步监听器先入队，由持有 Gate 的 Lua 任务在 hook 或 `sleep` 等待点处理
- JNI 工作线程按需附加到 JavaVM

## 后续语言复用

Java 反射、重载和类型转换集中在 `JavaInteropBridge`。JS 和 Go 接入时复用该后端，
分别提供符合各自语言习惯的对象包装；Lua 的 userdata 和元方法不强加给其他语言。
