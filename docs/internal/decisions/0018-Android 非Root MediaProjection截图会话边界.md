# 0018：Android 非 Root MediaProjection 截图会话边界

- 状态：已接受
- 日期：2026-08-04
- 补充：[0017：Android Root Worker 进程内直接截图](<0017-Android Root Worker进程内直接截图.md>)的非 Root 路线

## 背景

uid=0 的一次性 Root Worker 可以直接调用 SurfaceControl 隐藏接口，App UID 的非 Root Worker
不能使用同一路线。MediaProjection 必须由可显示系统确认界面的 App 请求用户授权，并通过前台
服务持有；如果把授权会话放进一次性 Worker，每次脚本结束都会丢失授权宿主。

Android 14 还限制一个授权令牌只能创建一次 VirtualDisplay。后续 Worker 若用同一令牌释放并
重建 VirtualDisplay 会失败，因此 Worker 生命周期、ImageReader Surface 生命周期和投屏授权
会话必须分开。

## 决定

1. 打开 App 或切到非 Root 模式都不请求屏幕录制授权。用户从主界面、悬浮控制或非 Root
   音量键启动脚本且当前未授权时，由 `MainActivity` 请求；授权成功后再恢复本次脚本运行，
   授权结果交给 App 主进程的前台 `MediaProjectionCaptureService`。Worker 和 HTTP 调用只能
   报告未授权，不能自行弹确认框或把远程请求改成界面跳转。
2. 每个有效授权会话只创建一个 MediaProjection 和一个 VirtualDisplay。主进程通过
   `IScreenCaptureHost` 接收当前 Worker 的 Surface；Worker 更换或显示尺寸变化时使用
   `VirtualDisplay.setSurface` 和 `resize`，不使用同一令牌创建第二个 VirtualDisplay。
3. App UID Worker 持有 `ImageReader(RGBA_8888)`、帧通知线程和 Surface。它消费最新 Image，按
   crop、rowStride 和 pixelStride 整理为紧凑 RGBA，并优先直接写入 `libengine.so` 的固定
   native 缓冲。首次尚无 native 缓冲时允许通过紧凑 `byte[]` 建立该缓冲。
4. 首帧允许等待系统投递；已有完整帧但暂时没有新 Image 时继续返回 native 缓冲中的上一帧。
   这是 MediaProjection 帧源语义，核心层原有的 `20ms` 缓存、锁帧和图片屏幕语义保持不变。
5. Worker 正常结束时分离 Surface、关闭 ImageReader 并解绑服务，但不结束主进程授权会话。
   MediaProjection 被系统收回、前台服务结束或切回 Root 模式时，才释放 VirtualDisplay 和授权
   会话；下次从本机入口运行非 Root 脚本时重新请求用户授权。
6. `AndroidHostBridge` 按当前 Worker UID 固定分发：uid=0 使用 SurfaceControl，App UID 使用
   MediaProjection/ImageReader。任一路线失败都不跨权限路线回退；C ABI、语言绑定、紧凑 RGBA
   格式及固定缓冲所有权不分叉。

## 后果

- 非 Root 截图不依赖 RootDaemon，也不要求 Worker 取得 uid=0，但必须遵守 Android 的屏幕录制
  用户授权和前台通知要求。
- 连续脚本会话可以复用仍有效的授权和同一个 VirtualDisplay，只替换一次性 Worker 的 Surface，
  避免重复授权以及 Android 14 的单次令牌限制。
- App 主进程只持有投屏会话和显示输出，不读取 RGBA；点阵整理、native 固定缓存和全部图像算法
  仍留在本次 Worker 与 `libengine.so`。
