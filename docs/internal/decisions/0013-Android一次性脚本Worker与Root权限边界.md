# 0013：Android 一次性脚本 Worker 与 Root 权限边界

- 状态：已接受
- 日期：2026-08-01

## 背景

原来的 `:engine` 同时承担 HTTP 控制、脚本执行和 `libengine.so` 生命周期。它属于普通 App
UID，因此 Lua FFI 和用户 SO 即使能加载，也不能像旧项目一样取得 uid=0；同时脚本、模型或
第三方 SO 的 native 全局状态会留在常驻进程，多次运行无法依靠语言层清理彻底回收。

现有 RootDaemon 已经由用户一次授权后常驻 uid=0，适合继续承担稳定截图、输入和系统控制，
但不能把用户脚本或任意 SO 装进该常驻进程，否则一次 native 崩溃会同时带走全部 Root 能力。

## 决定

1. App 主进程继续承载页面、悬浮控制、无障碍和脚本 Android UI；`:engine` 改为常驻控制进程，
   只承载 `EngineService`、HTTP/JSON-RPC、运行会话、日志保留、ImGui Surface 和 Worker 管理。
2. 每次脚本会话使用一次性 Worker。Root 模式由 RootDaemon 直接启动
   `app_process ... EngineWorkerMain`，进程 uid=0；非 Root 模式由 Android 启动
   `LocalEngineWorkerService :worker`，进程保持 App UID。两种外壳进入同一个
   `EngineWorkerEndpoint -> NativeEngine -> libengine.so`，公开 API、C ABI、语言绑定和扩展
   加载方式不分叉。
3. `libengine.so`、Lua/后续 JS/Go、JavaInterop、FFI、用户 SO、OpenCV/OCR/YOLO 和 ImGui EGL
   都位于 Worker。脚本结束、停止或强停时退出整个 Worker，由内核回收语言堆、native 内存、
   已加载 SO、线程、FD、模型和 EGL 状态。
4. RootDaemon 只保留稳定 Root 能力及 Worker 的启动、监督和强停；它永不加载
   `libengine.so`、脚本、模型或用户 SO。关闭 Worker 不关闭 RootDaemon，也不会再次触发 `su`。
5. Root Worker 使用一次性随机令牌通过导出的 `EngineWorkerBridgeProvider` 交付 Binder；令牌只在
   当前待连接会话有效。普通命令和 ImGui 输入走 Binder，大 JSON 与 RGBA 帧走
   `ParcelFileDescriptor` 管道，不能受 Binder 事务大小限制。
6. Android 组件实例仍由 App UID 宿主持有。脚本 UI、ImGui Surface、输入法和无障碍节点通过
   受限 Provider/Binder 桥调用；动态 Java、FFI 和 native 扩展仍在 Worker 本进程执行。
7. 非 Root 模式不删除 Root 类 API，也不为其统一伪造“权限不足”、重试或备用路线。调用进入与
   Root 模式相同的实现，按既有返回类型交付系统、SO 或底层桥的实际失败结果。

## 后果

- “强停引擎进程”的用户操作实际强停当前一次性 Worker；`:engine` HTTP 控制端与 RootDaemon
  保持可用，后续运行直接创建新 Worker。
- `log.drain` 的日志 ID 和最近日志由控制进程接续保存，Worker 退出后 IDE 仍能读取刚结束脚本的
  输出。
- Root Worker 需要按绝对路径加载安装目录中的 `libengine.so`，因此 APK 安装时保留可提取的
  native 库目录；Root 与非 Root 不分别构建业务 SO。
- 单次脚本仍可能自身 OOM 或崩溃，但其残留不会跨脚本会话累积，也不会污染常驻 RootDaemon。
