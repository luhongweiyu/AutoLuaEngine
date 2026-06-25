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
└─ libengine.so / LuaRuntime
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
└─ libengine.so
```

核心 API 层仍然是 `libengine.so`。Lua、后续 JS、FFI 和插件都应绑定这层统一 API；
Java Service 只负责进程、权限、Android 系统桥接和 root helper 启动。

验证记录：

```text
启动 MainActivity 后，EngineService 拉起 HTTP server。
GET http://127.0.0.1:18382/health -> {"ok":true,"port":18380}
```

其中 `18382` 是本机临时 adb forward 端口，Android 端实际端口仍是 `18380`。

## 4. 后续拆分步骤

### 4.1 主进程不再直接调用 NativeEngine

需要逐项替换：

```text
MainActivity.showRecentLogs() -> 已通过 EngineLocalClient 调用 log.drain
MainActivity 设置页状态        -> 已通过 EngineLocalClient 调用 device.info / script.status
```

替换后，主进程只通过协议或 Service 控制引擎。

### 4.2 截图能力跨进程确认

当前 `m.screen.capture()` 走：

```text
Lua -> native _host -> AndroidBridge -> ScreenCaptureBridge
```

拆进程前必须确认：

- MediaProjection 授权数据能否安全传到 `:engine`。
- ImageReader / VirtualDisplay 是否放在引擎进程创建。
- 图片句柄必须只存在于引擎进程，不能跨进程传 native 指针。

第一版建议：截图授权仍由主进程发起，授权结果通过 Intent / Binder 传给 `EngineService`。

### 4.3 EngineService 加独立进程

前两步完成后，再修改 Manifest：

```xml
<service
    android:name=".EngineService"
    android:exported="false"
    android:process=":engine" />
```

已完成，验收：

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
用户手动关闭 -> stopService 后释放 HTTP server / native runtime
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
协议为“文本头 + 原始 RGBA 帧”。后续 root 点击、文件、设备控制等能力会逐步
下沉到这里，保持 root 进程启动一次、后续复用。
