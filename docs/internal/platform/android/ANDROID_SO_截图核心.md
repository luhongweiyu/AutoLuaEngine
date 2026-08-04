# Android SO 屏幕点阵核心

本文记录 `libengine.so` 的物理截图缓存和图片屏幕。接口直接返回宽、高和点阵地址，不返回
中间资源句柄。

## 核心实现和 C ABI

真实点阵来源切换、截图缓存和 Android 物理截图分发位于：

```text
engines/android/app/src/main/cpp/core/api/screen_api.cpp
```

C ABI 门面位于：

头文件：

```text
engines/android/app/src/main/cpp/core/system_c_api.h
```

截图命令：

```c
int engine_getScreenPixels(int* width, int* height, unsigned char** pixels);
int engine_setScreenPixels(const char* imagePath);
int engine_restoreScreenPixels();
```

调用方式：

```c
int w = 0;
int h = 0;
unsigned char* pixels = 0;
int ok = engine_getScreenPixels(&w, &h, &pixels);
```

返回值：

- `ok == 1`：成功，`w`、`h`、`pixels` 有效。
- `ok == 0`：失败，通过 `engine_screenLastError()` 获取错误。
- `pixels` 固定为紧凑 RGBA，长度为 `w * h * 4`。
- `pixels` 由 `libengine.so` 当前脚本任务的固定缓冲区持有，调用方只读、不释放。物理帧
  刷新和图片屏幕替换/还原会覆盖内容但不更换地址；脚本任务结束后地址失效。

## 图片屏幕

- `engine_setScreenPixels(path)` 解码普通文件、脚本相对文件或当前 ALPKG 资源。
- 图片宽高不能超过物理屏幕，不缩放、不裁剪；图片点阵会覆盖固定屏幕缓冲区。
- 激活后直接返回固定图片，不检查 `20ms` 缓存时间，也不触发 Android 物理截图。
- 找色、找图、点阵识字和保存截图都读取同一个当前来源。
- `engine_restoreScreenPixels()` 使当前物理帧失效；下一次读取会通过当前 Worker 对应的物理截图
  路线刷新同一地址。
- 脚本结束、停止或报错时会统一释放固定缓冲区并清除图片屏幕状态。

## 缓存控制

```c
void engine_keepCapture();
void engine_releaseCapture();
int engine_setCaptureCacheMs(int durationMs);
const char* engine_screenLastError();
```

规则：

- 默认缓存时间：`20ms`。
- 缓存有效时，`engine_getScreenPixels` 直接返回当前点阵。
- 缓存过期时，`engine_getScreenPixels` 请求当前 Worker 对应的物理截图路线刷新内部点阵缓存。
- `engine_keepCapture()` 后一直复用当前帧。
- `engine_releaseCapture()` 后恢复按时间缓存。
- 图片屏幕激活时缓存时间和锁帧状态不参与读帧，还原后继续生效；还原后的第一次读取
  必定进入当前物理截图路线。非 Root 路线尚无新投影帧时可以继续使用最近一帧完整点阵。
- 脚本结束时释放固定缓冲区、清除帧状态并恢复默认缓存设置。

## Android 物理帧分发

`AndroidHostBridge.captureScreen` 只按当前 Worker UID 选择既定路线，不在截图失败时互相回退：

- uid=0 的 Root Worker 使用 SurfaceControl 隐藏接口。
- App UID 的非 Root Worker 使用主进程已有授权的 MediaProjection 会话和本进程 ImageReader。

两条路线最终都向 `libengine.so` 交付紧凑 RGBA。首次读取尚无 native 缓冲时，Java 返回紧凑
`byte[]`，JNI 据此分配当前脚本任务的固定缓冲；屏幕尺寸不变时，后续读取把该地址包装成
`DirectByteBuffer`，由 Java 路线直接覆盖。分辨率变化后只要原容量足够仍使用同一地址；所需
容量增长时明确失败并要求重启 Worker，不在已经向调用方返回裸地址后扩容或更换地址。

## Android Root 物理帧路径

```text
screen_api
  -> AndroidBridge JNI
  -> RootScreenCaptureBridge（校验当前 Worker uid=0）
  -> SurfaceScreenCaptureBridge / SurfaceControl
  -> 软件 ARGB_8888 Bitmap（隐藏接口返回硬件 Bitmap 时先完成 GPU 读回）
  -> Bitmap.copyPixelsToBuffer
  -> libengine.so 固定截图缓冲
```

Android 的 `ARGB_8888` 是 Bitmap 的逻辑颜色命名；当前平台内存中的四字节顺序与核心约定的
RGBA 一致，不在 Java 与 native 之间再做通道重排。后续帧由 Bitmap 直接覆盖固定 native 缓冲，
不再经过 RootDaemon socket 或 Java 整帧中间数组。非 Root Worker 不能调用这条隐藏 API 路线；
失败时保持原错误语义，不自动切换其他截图实现。

## Android 非 Root 物理帧路径

```text
screen_api
  -> AndroidBridge JNI
  -> MediaProjectionScreenCaptureBridge（App UID Worker）
  -> IScreenCaptureHost Binder
  -> MediaProjectionCaptureService（App 主进程，持有授权和唯一 VirtualDisplay）
  -> Worker ImageReader(RGBA_8888) Surface
  -> acquireLatestImage / Plane
  -> libengine.so 固定截图缓冲
```

屏幕录制授权和前台服务位于 App 主进程，不随一次性 Worker 退出。一个授权会话只创建一个
`VirtualDisplay`；Worker 启停或屏幕尺寸变化时，主进程只分离、替换 Surface 并按需调整尺寸，
不拿同一授权令牌重建 VirtualDisplay。Worker 正常清理时关闭自己的 ImageReader、解绑服务并
分离 Surface；意外退出留下的旧 Surface 会在下一 Worker 附着时被替换。后续 Worker 可以在
授权仍有效时重新附着，无需再次授权。

ImageReader 使用 `RGBA_8888` 并消费最新可用 Image。Plane 为紧凑四字节像素时按行写入固定
native 缓冲；存在行填充或更大像素步长时按 `rowStride`、`pixelStride` 和裁剪区域整理为紧凑
RGBA。首帧最多等待当前实现规定的超时时间；已有完整帧但系统尚未投递更新帧时，直接保留
native 缓冲中的上一帧，但仅限核心确认该缓冲仍保存上一张物理帧时。图片屏幕覆盖同一缓冲后，
还原时必须等待新的投影帧重新写入，不能把图片内容当成投影缓存。这一复用发生在平台帧源层，
不改变上面的 `20ms` 核心缓存设置。

未授权、授权被系统收回、前台服务结束或宿主 Binder 断开时，截图返回可读错误；Worker 不会
自行弹系统授权框，也不会改走 Root SurfaceControl。本机主界面、悬浮控制或非 Root 音量键在
启动脚本前发现未授权时，会把用户带到 `MainActivity` 的系统确认页，授权成功后恢复本次运行。
HTTP/IDE 远程调用不主动拉起界面，只能得到未授权错误。打开 App 和切换到非 Root 本身不申请。

## Lua 绑定

当前 Lua 绑定通过 HostApi 调用同一组 C ABI：

```lua
local w, h, pixels = m.getScreenPixels()
```

失败时：

```lua
local w, err = m.getScreenPixels()
```

可用函数：

```lua
m.getScreenPixels()
m.setScreenPixels(imagePath)
m.restoreScreenPixels()
m.capture(path[, left, top, right, bottom])
m.snapShot(path[, left, top, right, bottom])
m.keepCapture()
m.releaseCapture()
m.setCaptureCacheMs(ms)
```

本文只描述截图核心。输入、输入法和其他已实现的脚本 API 仍遵循
`core/api -> C ABI -> Lua/JS/Go 绑定` 边界，完整列表见
[统一 API 契约](../../contracts/API_契约.md)。
