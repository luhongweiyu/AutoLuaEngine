# Android 引擎独立进程拆分方案

本文档记录 Android 引擎从“同进程服务化”演进到“脚本独立进程”的拆分方案。

## 1. 目标

参考旧项目新版结构：

```text
主进程       MainService / UI / 悬浮窗 / 权限入口
脚本进程 :sc NativeService / so / Lua 执行
插件进程 :ps PluginService / 扩展能力
```

AutoLuaEngine 后续目标：

```text
主进程
├─ MainActivity
├─ FloatingControlService
└─ 权限授权入口

引擎进程 :engine
├─ EngineService
├─ EngineHttpServer
├─ NativeEngine
└─ libengine.so / EngineCommand / LuaRuntime
```

第一版暂不做插件进程。

## 2. 为什么不能直接加 android:process

不能只在 `EngineService` 上直接加：

```xml
android:process=":engine"
```

原因：

1. `MainActivity` 和 `FloatingControlService` 以前也会直接启动 `EngineHttpServer` 和 `NativeEngine`。
2. 如果直接拆进程，主进程和引擎进程会各自加载一套 `libengine.so`，脚本、日志、图片句柄会分裂。
3. 截图授权和无障碍能力属于 Android 系统权限链路，需要明确跨进程访问方式。
4. App 内日志入口不能再直接读主进程 native 日志，否则看到的不是引擎进程日志。

## 3. 当前已完成

已完成：

```text
MainActivity             -> EngineService.ensureStarted()
FloatingControlService   -> EngineService.ensureStarted()
EngineService.onCreate() -> NativeEngine.init() + EngineHttpServer.start()
MainActivity 日志入口    -> EngineLocalClient -> log.drain
MainActivity 设置入口    -> EngineLocalClient -> device.info / script.status
EngineHttpServer         -> NativeEngine.callJson -> libengine.so
EngineService 控制脚本   -> NativeEngine.callJson -> libengine.so
```

也就是说，native 初始化和 HTTP JSON-RPC 服务启动已经收敛到 `EngineService`。
App 主界面查看日志和引擎状态时，也已经通过本地 JSON-RPC 访问引擎，不再直接读取主进程 native 状态。

2026-06-25 已按旧项目方向完成第一版独立进程拆分：

```text
主进程 com.autolua.engine
├─ MainActivity
├─ FloatingControlService
└─ 权限入口

引擎进程 com.autolua.engine:engine
├─ EngineService
├─ EngineHttpServer
├─ NativeEngine
├─ ScreenCaptureBridge
└─ libengine.so / engine_command.cpp
```

核心 API 层仍然是 `libengine.so`。Lua、后续 JS、FFI 和插件都应绑定这层统一 API；
Java Service 只负责进程、权限、Android 系统桥接和 root helper 启动。

2026-07-09 已完成第二次边界收口：

```text
App 主进程
├─ MainActivity：UI、脚本选择、权限入口、状态展示
├─ FloatingControlService：悬浮按钮和控制面板
└─ EngineLocalClient：通过本地 JSON-RPC 查询引擎

:engine 进程
├─ EngineService：进程壳、脚本文件读取、状态广播、强停进程
├─ EngineHttpServer：HTTP/JSON-RPC 网络壳
├─ NativeEngine：JNI 统一入口
└─ libengine.so：脚本运行、任务状态、统一命令分发、系统 API 调度
```

关键规则：

- Java HTTP 层不再分发 `device.*`、`root.*`、`script.*`、`screen.*` 等业务命令。
- `EngineHttpServer` 只把 JSON-RPC 的 `method/params` 传给 `NativeEngine.callJson(...)`。
- `EngineService` 运行、停止、暂停、继续、切换 Root 模式时，也走同一个 native 命令入口。
- Root 授权准备只在 `:engine` 启动和切换 Root 模式时做，运行脚本时不重复申请。
- 强停进程是硬控制，收到强停命令后直接释放服务资源并 kill `:engine`，不等待脚本协作停止。

验证记录：

```text
启动 MainActivity 后，EngineService 拉起 HTTP server。
GET http://127.0.0.1:18382/health -> {"ok":true,"port":18380}
```

其中 `18382` 是本机临时 adb forward 端口，Android 端实际端口仍是 `18380`。

2026-07-09 在雷达模拟器 `emulator-5560` 验证：

```text
adb forward tcp:18380 tcp:18380
GET /health -> {"ok":true,"port":18380}
device.info -> platform=android, luaVersion=Lua 5.4, rootAvailable=true
script.run -> print("你好 from native command") 成功输出中文日志和 Lua 版本
log.drain -> 可读取 native 日志通道
script.stop -> 可停止长循环脚本，任务状态变为 failed: script stopped
```

## 4. 当前运行路线

### 4.1 主进程不直接调用 NativeEngine

已完成：

```text
MainActivity.showRecentLogs() -> 已通过 EngineLocalClient 调用 log.drain
MainActivity 设置页状态        -> 已通过 EngineLocalClient 调用 device.info / script.status
MainActivity 运行/停止脚本     -> 发送 Intent 给 EngineService
FloatingControlService 控制脚本 -> 发送 Intent 给 EngineService
```

主进程只通过协议或 Service 控制引擎，不直接加载脚本运行状态。

### 4.2 截图能力跨进程边界

当前 `m.screen.capture()` 走：

```text
Lua -> native _host -> AndroidBridge -> ScreenCaptureBridge
```

当前规则：

- MediaProjection 授权由主进程发起，授权结果通过 Intent 传给 `EngineService`。
- ImageReader / VirtualDisplay 放在 `:engine` 进程创建。
- 图片句柄只存在于 `:engine` 进程，不跨进程传 native 指针。
- HTTP 只返回图片句柄元信息，不返回大像素数据。

### 4.3 EngineService 独立进程

Manifest：

```xml
<service
    android:name=".EngineService"
    android:exported="false"
    android:process=":engine" />
```

验收：

```text
adb shell ps -A | findstr autolua
```

应能看到主进程和 `:engine` 进程。

当前验证：

```text
u0_a51 ... com.autolua.engine
u0_a51 ... com.autolua.engine:engine
```

### 4.4 脚本结束后的进程回收

旧项目的 `NativeService` 脚本结束后会回收脚本进程。我们后续可以评估：

```text
脚本短任务结束 -> 保持 EngineService 存活，方便 IDE 连续运行
严重崩溃恢复 -> 重启 :engine 进程
用户手动强停进程 -> 直接 kill :engine，并释放 HTTP server / native runtime
```

第一版先不在脚本结束后主动杀进程，避免 IDE 连接频繁断开。

## 5. 当前不做

- 插件进程
- 多脚本并发进程池
- 每次脚本运行都新建进程

## 6. root helper

当前已新增 root helper：

```text
su -c app_process /system/bin com.autolua.engine.RootHelperMain
```

该进程 uid=0，第一版用于 root 截图。App 引擎进程通过 stdin/stdout 与它通讯，
协议为“文本头 + 原始 RGBA 帧”，保持 root 进程启动一次、后续复用。
