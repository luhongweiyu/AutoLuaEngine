# Android Java 互操作

## 目标

Android Lua 第一版实现与懒人精灵 `import` 相同的脚本调用方式。Java 类和对象在
Lua 中表现为可直接访问的对象，不向脚本暴露数字句柄。

```lua
import("java.lang.*")
import("android.content.Context")
import("com.nx.assist.lua.LuaEngine")

print(Math.sin(1.2))

local context = LuaEngine.getContext()
print(context.getPackageName())
print(Context.ACTIVITY_SERVICE)
```

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

- `runtime/lua/java_bridge.cpp`：注册 `import`、Java userdata 元方法、Lua/Java 值转换、回调队列和 JNI 对象生命周期。
- `runtime/lua/java_bridge.h`：声明 JavaVM 初始化、LuaRuntime 注册、销毁和回调处理入口。
- `interop/JavaInteropBridge.java`：类加载、字段、方法、构造函数、重载、数组、集合和接口代理。
- `interop/LuaTableValue.java`：保留 Lua table 键值，等确定 Java 目标类型后再转换。
- `interop/LuaCallback.java`：保存 Lua registry 引用并从 Java 接口回调 native。
- `com/nx/assist/lua/LuaEngine.java`：懒人精灵包名兼容类。
- `com/nx/assist/lua/ApkLoader.java`：`LuaEngine.loadApk` 的插件类加载对象。

## 已支持语义

- 完整类名导入：`import("android.content.Context")`。
- 包通配导入：`import("java.lang.*")`，首次访问 `Math` 时延迟加载并缓存到 `_G`。
- 静态字段、静态方法和公开内部类。
- Java 构造函数、实例字段和实例方法。
- 点号调用：`context.getPackageName()`，不要求 Lua 冒号语法。
- Java 方法链式返回：`builder.append("a").append("b")`。
- 方法和构造函数重载匹配，候选反射结果按类和方法名缓存。
- Java 数组、`List`、`Map` 下标及 `#object` 长度。
- Lua function 转单抽象方法接口，例如 `Runnable`、`Comparator`。
- Lua table 按方法名转多方法 Java 接口监听器。
- Java 异常转换为 Lua 错误，可用 `pcall` 捕获。
- `nil` 与 Java `null` 双向转换；Java `void` 方法在 Lua 中没有返回值。

## 类型转换

| Lua | Java 参数 |
|---|---|
| `nil` | 非 primitive 的 `null` |
| boolean | `boolean` / `Boolean` |
| integer | 按范围匹配 `int`、`long` 等整数类型 |
| number | 优先匹配 `double`，也可转换为 `float` |
| string | `String` / `CharSequence` / 单字符 `char` |
| Java userdata | 原 Java 对象 |
| table | Java 数组、`List`、`Collection`、`Map` 或接口代理 |
| function | 单抽象方法 Java 接口代理 |

Java `String`、boolean、整数和浮点数返回 Lua 基础类型。其他 Java 返回对象继续
保持 userdata，因此可以继续读取成员或作为下一个 Java 方法的参数。

Lua table 转 Java 数组或集合时读取 `1..n` 连续部分；Java 数组和 `List` 在 Lua
中保持 Java 下标规则，从 `0` 开始。

## 对象生命周期

每个 Java userdata 内部保存 JNI `GlobalRef`，由 userdata 的 `__gc` 释放。Lua
接口函数保存到当前 `LuaRuntime` 的 registry，脚本结束时统一解除引用。脚本结束
后仍被 Java 持有的代理再次触发时会明确报“LuaRuntime 已结束”。

## 回调线程

Java 回调不会并发访问 `lua_State`：

- Java 方法在当前脚本线程同步触发接口时，直接执行 Lua 回调。
- Lua 暂停在某次 Java 调用中、其他 Java 线程同步回调时，串行执行后再让原调用返回。
- 普通异步监听器先进入队列，由 Lua 指令 hook 或 `sleep` 等待点在脚本线程执行。
- Java `void` 异步回调不阻塞 Android 主线程；需要返回值的接口等待脚本线程处理结果。

## LuaEngine 兼容方法

```lua
LuaEngine.getContext()
LuaEngine.httpGet(url, headers [, timeout])
LuaEngine.httpPost(url, params, headers [, timeout])
LuaEngine.httpPostData(url, data, contentType, timeout)
LuaEngine.loadApk(nameOrAbsolutePath)
```

- HTTP 超时单位为秒，成功返回响应字符串，失败返回 `nil`。
- `loadApk` 支持绝对路径、assets 根目录名称和 `assets/plugins` 名称。
- `loadApk` 成功返回 `ApkLoader`，失败返回 `nil`；`loader.loadClass(className)` 成功返回 Java Class，失败返回 `nil`。

## 后续语言复用

Java 反射、重载和类型转换集中在 `JavaInteropBridge`。JS 和 Go 接入时复用该后端，
分别提供符合各自语言习惯的对象包装；Lua 的 userdata 和元方法不强加给其他语言。
