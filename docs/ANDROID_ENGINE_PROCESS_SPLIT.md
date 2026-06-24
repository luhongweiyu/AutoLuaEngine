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

## 3. 当前已完成的安全步骤

已完成：

```text
MainActivity             -> EngineService.ensureStarted()
FloatingControlService   -> EngineService.ensureStarted()
EngineService.onCreate() -> NativeEngine.init() + EngineHttpServer.start()
MainActivity 日志入口    -> EngineLocalClient -> log.drain
```

也就是说，native 初始化和 HTTP JSON-RPC 服务启动已经收敛到 `EngineService`。
App 主界面查看日志时，也已经通过本地 JSON-RPC 访问引擎，不再直接读取主进程 native 日志。

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
MainActivity 设置页状态        -> 待改为通过 device.info / script.status 查询
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

验收：

```text
adb shell ps -A | findstr autolua
```

应能看到主进程和 `:engine` 进程。

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
- root 引擎进程
- 多脚本并发进程池
- 每次脚本运行都新建进程
