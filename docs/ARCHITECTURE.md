# 架构设计

## 1. 总体结构

长期目标采用三层架构：

```text
脚本语言层
├─ LuaRuntime
├─ JsRuntime
└─ GoRuntime

统一 API 层
├─ HostApi
├─ ScriptTask
├─ LogChannel
└─ EngineProtocol

平台实现层
├─ AndroidPlatform
├─ WindowsPlatform
└─ IosPlatform
```

第一版只实现：

```text
LuaRuntime -> HostApi -> AndroidPlatform
```

当前 Lua 版本：

```text
Lua 5.4.8
```

Lua 源码补丁：

```text
engines/android/third_party/lua-5.4.8/src/llex.c
```

当前只改了词法器标识符识别：允许 UTF-8 非 ASCII 字节序列作为变量名、函数名和字段名的一部分，用于支持 `_G.中文` 这类写法。

## 2. Android 第一版结构

```text
Android APK
├─ Kotlin/Java 层
│  ├─ MainActivity
│  ├─ EngineService（后续）
│  ├─ FloatingControl（后续）
│  ├─ NativeEngine
│  ├─ AndroidHostBridge
│  ├─ AutomationAccessibilityService
│  └─ ScreenCaptureBridge
│
├─ Native C++ 层：libengine.so
│  ├─ engine_jni
│  ├─ Engine
│  ├─ ScriptTask
│  ├─ LuaRuntime
│  ├─ HostApi
│  └─ AndroidPlatformApi
│
└─ Lua 层
   ├─ runtime/api_m.lua
   ├─ runtime/compat_lr.lua
   ├─ runtime/compat_cd.lua
   ├─ runtime/bootstrap.lua
   └─ 用户脚本
```

## 3. 关键模块说明

### 3.1 Engine

职责：

- 初始化 native 引擎
- 管理脚本任务
- 管理日志通道
- 持有平台能力接口

当前已实现最小 `Engine` 封装：

```text
engine_jni -> Engine -> LuaRuntime
```

第一版只需要支持单任务运行。后续再扩展多任务。

后续 App 控制界面会从纯调试页调整为脚本列表、运行设置和悬浮控制入口。脚本运行应逐步下沉到后台 `EngineService`，悬浮图标只负责发控制命令，不直接持有脚本执行逻辑。是否使用独立 Android 进程 `:engine` 需要在服务化后再评估。

当前 ScriptTask 状态：

```text
已实现同步任务模型：idle、running、finished、failed。
已实现 Lua debug hook 协作取消。
待实现：stopping 状态、native 内部异步执行、任务状态查询。
当前 UI 层使用 Java Thread 调用 native 同步接口，避免阻塞 Android 主线程。
```

### 3.2 ScriptRuntime

职责：

- 抽象不同脚本语言
- 提供统一运行入口
- 注册 HostApi

建议接口：

```cpp
class ScriptRuntime {
public:
    virtual bool runText(const char* code) = 0;
    virtual void requestStop() = 0;
    virtual void registerHostApi() = 0;
    virtual ~ScriptRuntime() = default;
};
```

第一版实现：

```text
LuaRuntime : ScriptRuntime
```

当前源码位置：

```text
engines/android/third_party/lua-5.4.8
```

当前测试脚本位置：

```text
engines/android/app/src/main/assets/scripts/main.lua
```

后续可实现：

```text
JsRuntime : ScriptRuntime
GoRuntime : ScriptRuntime
```

### 3.3 PlatformApi

职责：

- 抽象不同系统能力
- 为 HostApi 提供实际能力

建议接口：

```cpp
class PlatformApi {
public:
    virtual void sleepMs(int ms) = 0;
    virtual void logPrint(const char* text) = 0;
    virtual const char* getDeviceInfoJson() = 0;
    virtual ~PlatformApi() = default;
};
```

第一版实现：

```text
AndroidPlatformApi : PlatformApi
```

后续可实现：

```text
WindowsPlatformApi : PlatformApi
IosPlatformApi : PlatformApi
```

### 3.4 HostApi

职责：

- 定义脚本用户看到的统一 API
- 把 Lua/JS/Go 调用转发到 PlatformApi
- C++ 只暴露 native `_host`，Lua runtime 负责整理 `m/lr/cd` 命名空间

当前 Android Lua 绑定位置：

```text
engines/android/app/src/main/cpp/runtime/host_api.h
engines/android/app/src/main/cpp/runtime/host_api.cpp
```

示例：

```lua
m.log.print("hello")
m.sleep(1000)
local info = m.device.info()
```

Lua runtime 层位置：

```text
engines/android/app/src/main/assets/runtime/api_m.lua
engines/android/app/src/main/assets/runtime/compat_lr.lua
engines/android/app/src/main/assets/runtime/compat_cd.lua
engines/android/app/src/main/assets/runtime/bootstrap.lua
```

当前 Android 平台能力：

```text
m.touch.tap / m.touch.swipe -> AccessibilityService
m.key.back / m.key.home -> AccessibilityService performGlobalAction
m.screen.capture -> MediaProjection + ImageReader -> native 内存图片句柄
```

### 3.5 EngineProtocol

职责：

- 给 IDE/PC 工具调用引擎
- 后续 VS Code 插件、Qt 工具共用

第一版可以先不做复杂协议，先保留接口设计。

后续协议示例：

```json
{
  "method": "script.run",
  "params": {
    "language": "lua",
    "code": "print('hello')"
  }
}
```

## 4. 线程原则

第一版建议单线程执行脚本，先跑通链路。

后续实现 `Thread.newThread` 时必须遵守：

- 不让多个系统线程同时执行同一个 `lua_State`
- 每个脚本线程使用独立 Lua state 或独立 runtime
- 停止线程优先使用取消标记，不强杀线程
- 平台 UI / 无障碍调用需要回到 Android 主线程或服务线程

## 5. 错误处理原则

API 不要过度兜底，也不要静默失败。

建议规则：

- 可恢复错误：返回 `nil, message`
- 不支持的平台能力：返回 `nil, "unsupported"`
- 脚本语法/运行错误：记录到任务状态，并输出日志
- native 严重错误：立即返回失败，不伪装成功

## 6. 后续扩展边界

### 支持 JS

新增：

```text
JsRuntime : ScriptRuntime
```

HostApi 语义不变，只增加 JS 绑定。

### 支持 Windows

新增：

```text
WindowsPlatformApi : PlatformApi
```

ScriptRuntime 可以复用。

### 支持 iOS

新增：

```text
IosPlatformApi : PlatformApi
```

需要单独处理 iOS 权限和系统限制。

### 支持 IDE

IDE 只连接 EngineProtocol，不直接依赖 Android 代码。
