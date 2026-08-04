# 0017：Android Root Worker 进程内直接截图

- 状态：已接受
- 日期：2026-08-04
- 更新：[0013：Android 一次性脚本 Worker 与 Root 权限边界](0013-Android一次性脚本Worker与Root权限边界.md)中的截图归属

## 背景

0013 将截图与输入都留在常驻 RootDaemon，避免普通 App UID 调用系统隐藏截图接口。一次性
Root Worker 落地后本身已经是 uid=0，继续让每帧截图经过 RootDaemon 会额外执行状态探测、
认证 socket 会话和整帧 RGBA 传输；720×1280 屏幕每帧需要传输 3,686,400 字节，并在 daemon
保留一份中间点阵。

## 决定

1. Root 物理截图改由 uid=0 的 `EngineWorkerMain` 在本进程直接调用 SurfaceControl 隐藏接口，
   不再发送 RootDaemon `capture` 命令。
2. `libengine.so` 首帧通过既有 JNI 结果建立固定截图缓冲；后续帧把该地址包装为
   `DirectByteBuffer`，由 `Bitmap.copyPixelsToBuffer` 原地覆盖。
3. RootDaemon 删除截图命令和整帧 socket 缓冲，继续负责触控、按键、输入法系统切换、系统
   命令、物理音量键以及 Root Worker 的启动和监督。
4. 直接截图入口必须校验当前进程 uid=0。非 Root 高频截图另行设计，不在此处增加权限回退、
   MediaProjection 或其他备用路线。

## 后果

- 强制刷新不再承担每帧 RootDaemon 状态探测及 3.52 MiB 点阵 socket 搬运。
- 截图状态随一次性 Worker 退出而回收，不会把语言运行时、模型或用户 SO 带入 RootDaemon。
- C ABI、Lua API、RGBA 格式、截图缓存和错误不回退语义保持不变。
