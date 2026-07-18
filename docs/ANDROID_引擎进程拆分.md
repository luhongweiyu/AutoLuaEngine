# Android 引擎独立进程拆分方案

本文档记录 Android 引擎从“同进程服务化”演进到“脚本独立进程”的拆分方案。

## 1. 目标

参考旧项目新版结构：

```text
主进程       MainService / UI / 悬浮窗 / 权限入口
脚本进程 :sc NativeService / so / Lua 执行
插件进程 :ps PluginService / 扩展能力
```

小鱼精灵 后续目标：

```text
主进程
├─ MainActivity
├─ FloatingControlService
└─ Root 模式和悬浮窗权限入口

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
3. Root 运行层和无障碍能力属于 Android 系统权限链路，需要明确跨进程访问方式。
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
主进程 com.xiaoyv.engine
├─ MainActivity
├─ FloatingControlService
├─ RootDaemonService / RootDaemonManager
└─ Root 模式和悬浮窗权限入口

RootDaemon（uid=0）
└─ RootDaemonMain：受认证的本机 Root 命令服务

引擎进程 com.xiaoyv.engine:engine
├─ EngineService
├─ EngineHttpServer
├─ NativeEngine
├─ RootHelperBridge：到 RootDaemon 的已认证 socket 客户端
└─ libengine.so / engine_command.cpp
```

核心能力层仍然是 `libengine.so`。Lua/JS/Go 等语言绑定只做参数转换和返回值封装，
脚本 API 的真实逻辑统一进入 `libengine.so/core/api`，对外复用通过 `system_c_api`
C ABI。Java Service 只负责进程、权限、Android 系统桥接和 RootDaemon 生命周期。

2026-07-09 已完成第二次边界收口：

```text
App 主进程
├─ MainActivity：UI、脚本选择、Root 模式、悬浮窗权限、状态展示
├─ FloatingControlService：悬浮按钮和控制面板
├─ EngineLocalClient：通过本地 JSON-RPC 查询引擎
└─ RootDaemonService：只在 Root 模式下准备或关闭 RootDaemon

:engine 进程
├─ EngineService：进程壳、脚本文件读取、状态广播、强停进程
├─ EngineHttpServer：HTTP/JSON-RPC 网络壳
├─ NativeEngine：JNI 统一入口
└─ libengine.so：脚本运行、任务状态、控制命令分发、core/api 和 C ABI 门面
```

关键规则：

- Java HTTP 层不分发脚本 API 业务命令。
- `EngineHttpServer` 只把 JSON-RPC 的 `method/params` 传给 `NativeEngine.callJson(...)`。
- `EngineService` 运行、停止、暂停、继续、切换 Root 模式时，也走同一个 native 命令入口。
- Root 授权和 `su -c app_process` 只由 App 主进程的 RootDaemonService 执行；`:engine` 从不执行 `su`。
- 强停进程是硬控制，收到强停命令后直接释放 `:engine` 的服务资源并 kill 该进程；主进程和 RootDaemon 保持存活。

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

### 4.2 脚本 API 能力边界

物理屏幕来源下，`m.getScreenPixels()` 走：

```text
Lua -> HostApi -> system_c_api C ABI -> core/api/screen_api -> AndroidBridge -> RootScreenCaptureBridge -> RootHelperBridge
    -> RootDaemonMain -> RootHelperMain
```

当前规则：

- `screen_api` 位于 `libengine.so/core/api`，负责截图缓存、锁帧和 Root 截图分发。
- `engine_getScreenPixels` 位于 `system_c_api`，只做 C ABI 参数检查和转发。
- `m.setScreenPixels(path)` 经过 HostApi、C ABI 和 `image_api` 解码图片，再复制到固定屏幕
  缓冲区；激活后读帧停在 `screen_api`，不会进入 Root 路线。
- `m.restoreScreenPixels()` 使物理帧失效，后续第一次读取会把实时 Root 截图写回同一地址。
- `RootHelperBridge` 只复用 `:engine` 到 RootDaemon 的已认证 socket；它不会探测 Root、启动 `su` 或创建特权进程。
- 截图缓存由 `libengine.so` 按时间和锁帧状态管理。
- HTTP 不传输大像素数据。

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
adb shell ps -A | findstr xiaoyv
```

应能看到主进程和 `:engine` 进程。

当前验证：

```text
u0_a51 ... com.xiaoyv.engine
u0_a51 ... com.xiaoyv.engine:engine
```

### 4.4 脚本结束后的进程回收

旧项目的 `NativeService` 脚本结束后会回收脚本进程。当前规则：

```text
脚本短任务结束 -> 保持 EngineService 存活，方便 IDE 连续运行
严重崩溃恢复 -> 重启 :engine 进程
用户手动强停进程 -> 直接 kill :engine，并释放 HTTP server / native runtime；RootDaemon 不退出
```

## 5. 当前不做

- 插件进程
- 多脚本并发进程池
- 每次脚本运行都新建进程

## 6. RootDaemon

Root 特权边界已从引擎进程移到 App 主进程：

```text
MainActivity 打开 / Root 模式切换 / device.setRootModeEnabled
    -> RootDaemonService（主进程）
    -> RootDaemonManager
    -> su -c app_process ... RootDaemonMain（uid=0）

:engine 的 RootHelperBridge
    -> 127.0.0.1:应用 UID 派生端口的认证 socket
    -> RootDaemonMain
    -> RootHelperMain 命令分发器
```

RootDaemon 只监听回环地址，客户端必须先提交随机令牌。令牌保存在 App 私有目录，RootDaemon
会监视主进程 PID；主进程消失时主动退出，避免遗留特权服务。
悬浮窗重建、音量键开关和 RootDaemonService 的系统重建只会重连已有 socket，不会创建
daemon 或执行 `su`。

截图仍使用“文本头 + 原始 RGBA 帧”协议；触摸、按键和输入法切换使用短文本命令。RootDaemon
由截图和 Root 输入注入共同复用，`imeLib.lock()` / `imeLib.unlock()` 才按系统要求执行输入法
切换命令，`imeLib.setText()` 不创建外部进程。

在 RootDaemon 已就绪后，强停 `:engine` 只会断开该进程的 socket。下次运行脚本会重连同一个
RootDaemon，不会再次触发 `su` 授权。
