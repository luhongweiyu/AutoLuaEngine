# Android 引擎进程拆分

本文记录当前已经落地的“常驻控制端 + 一次性脚本 Worker”结构。长期决定见
[0013：Android 一次性脚本 Worker 与 Root 权限边界](../../decisions/0013-Android一次性脚本Worker与Root权限边界.md)。

## 进程结构

```text
App 主进程（App UID，常驻）
├─ MainActivity / FloatingControlService
├─ AutomationAccessibilityService
├─ Dialog / HUD / WebView 脚本 UI
└─ RootDaemonService / RootDaemonManager
       └─ su -c app_process -> RootDaemonMain（uid=0，常驻）
                              ├─ 截图、触控、输入、系统命令、音量键
                              └─ RootWorkerSupervisor

:engine 控制进程（App UID，常驻）
├─ EngineService：运行会话、状态广播、停止/强停
├─ EngineHttpServer：IDE/PC 的 HTTP/JSON-RPC
├─ EngineWorkerCoordinator：选择外壳、Binder、日志和生命周期
├─ EngineWorkerBridgeProvider：Root Worker 一次性握手及 App UID 宿主桥
└─ ScriptImGuiService：透明 Surface、触摸、键盘和输入法代理

本次 Worker（二选一，不并存）
├─ Root：RootDaemon -> app_process -> EngineWorkerMain（uid=0）
└─ 非 Root：LocalEngineWorkerService :worker（App UID）
       └─ EngineWorkerEndpoint
          ├─ NativeEngine / libengine.so
          ├─ Lua 5.4；后续 JS / Go 使用同一外壳
          ├─ core/api / system_c_api / 各语言绑定
          ├─ JavaInterop / FFI / APK、Dex 与用户 SO
          ├─ OpenCV / OCR / YOLO 可选扩展
          └─ Dear ImGui EGL/渲染线程
```

## 固定边界

- App 主进程不加载 `libengine.so`，只持有 Android 页面、Service 和无障碍对象。
- `:engine` 不执行脚本、不加载用户 SO，也不执行 `su`；它是可连续连接的控制端。
- RootDaemon 只加载项目内稳定 Root 实现，不加载脚本运行时、模型或任意扩展。
- Root 与非 Root Worker 进入完全相同的 `EngineWorkerEndpoint -> NativeEngine`。公开函数、
  参数、返回类型、C ABI 和语言绑定不因权限模式分叉。
- 每次脚本结束、停止、崩溃或强停后退出 Worker。RootDaemon、App 主进程和 `:engine` 不退出。

## 启动路线

### Root

```text
EngineWorkerCoordinator
  -> RootDaemonClient（既有认证 socket）
  -> RootWorkerSupervisor
  -> /system/bin/app_process ... EngineWorkerMain
  -> ActivityThread.systemMain().getSystemContext()
  -> createPackageContext(packageName)
  -> 绝对加载 nativeLibraryDir/libengine.so
  -> bootstrap Runtime 加载系统 libjavacrypto.so
  -> EngineWorkerBridgeProvider.registerWorker(runId, oneTimeToken, binder)
```

RootDaemon 是 uid=0，子 Worker 直接继承 uid=0，不再次执行 `su`，因此重复运行脚本不会重复弹
Root 授权。Provider 只接受 root 或本应用 UID，并且 Root Binder 必须同时匹配当前待连接的
`runId` 和一次性随机令牌；旧 Worker 或其他进程不能覆盖当前会话。
Root `app_process` 不属于 AMS 注册的应用进程，访问宿主 Provider 时由 `ContentProviderBridge`
申请并及时释放 external provider 引用，避免依赖普通应用进程的 `ContentResolver` 身份。
它也不会继承 Zygote 已完成的 Conscrypt JNI 注册。RootDaemon 启动 Worker 时同时保留 APK native
目录和当前系统 Java native 搜索路径；Worker 先加载 `libengine.so`，再由 bootstrap
`java.lang.Runtime` 在系统 linker namespace 中加载 `libjavacrypto.so`，完成后才注册 Binder。

### 非 Root

```text
EngineWorkerCoordinator
  -> bindService(LocalEngineWorkerService :worker)
  -> EngineWorkerEndpoint
  -> NativeEngine / libengine.so
```

Local 外壳只改变 Linux UID 和启动方式，不删 API。Root 类调用仍进入原实现，由系统或底层桥按
既有返回类型给出实际结果；控制层不统一改写错误、不重试、不切换备用路线。

## 命令和大数据

- `EngineService` 文件运行、HTTP JSON-RPC、停止、暂停和继续最终都调用当前
  `IEngineWorker`。
- JSON 参数与返回结果通过 `ParcelFileDescriptor` 管道传输，避免普通 Binder 事务的大小上限。
- `/tool/screenshot` 的 XYVF RGBA 帧同样通过 FD 管道返回；HTTP 层只搬运数据。
- ImGui 的 `Surface` 可直接作为 Parcelable 交给 Worker；触摸、键盘和文本事件通过 Binder
  送回同一个 native 运行时。
- Dialog、HUD、Web、输入法与无障碍节点保留在 App UID 进程，由受限 Provider/Intent 桥接。
  Android UI 线程不能直接执行语言回调，事件仍进入 native 会话队列。

## 日志与结束

Worker 的 `log.drain` ID 每个进程从头开始，`EngineWorkerCoordinator` 在控制进程为日志重新分配
连续 ID，并最多保留最近 1000 条。脚本返回后先收取最后日志，再关闭 Worker，因此 IDE 可以在
进程退出后继续读取刚结束脚本的输出。

正常结束调用 Worker 清理入口后退出进程；强停或 Binder 死亡由对应外壳回收：Root Worker 交给
RootDaemon 的 `Process` 监督，本地 Worker 直接结束 `:worker`。进程退出后由内核统一回收 Java
堆、语言堆、native 分配、SO 全局状态、线程、FD、模型、EGL 和纹理。

JNI 的 `Engine` 使用函数内静态实例，在 runtime 全局同步状态完成构造后才首次创建。正常进程
退出时因此会先析构 `Engine`，再析构它依赖的 mutex，不能改回跨编译单元的全局 `Engine`
实例，否则静态析构顺序可能重新引入退出崩溃。

## 控制命令语义

- 普通“停止”仍先调用 native `script.stop`，让脚本按既有中断点结束。
- “强停引擎进程”保留原用户入口名称，但只强停当前 Worker；HTTP 控制端不掉线。
- Root 模式切换后关闭当前空闲 Worker，下一次命令按新模式创建新进程。
- 同一时刻只允许一个 Worker 会话，不实现多脚本并发进程池。

## 最低验收

1. Java/AIDL 与 arm64 Debug APK 可构建。
2. Root 模式脚本 Worker 的 `/proc/<pid>/status` 或脚本 FFI 观察到 uid=0。
3. 非 Root 模式使用 `:worker` 且 UID 为应用 UID；相同普通 API、UI 和日志路径可用。
4. Root FFI 可加载需要 uid=0 的测试 SO；脚本结束后 Worker PID 消失，RootDaemon PID 不变。
5. 连续运行/停止至少三次，没有遗留 Worker、ImGui Surface 或脚本 UI。
