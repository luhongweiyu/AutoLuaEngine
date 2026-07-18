# Android SO 屏幕点阵核心

本文记录 `libengine.so` 的物理截图缓存和图片屏幕。接口直接返回宽、高和点阵地址，不返回
中间资源句柄。

## 核心实现和 C ABI

真实点阵来源切换、截图缓存和 Root 截图分发位于：

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
- 激活后直接返回固定图片，不检查 `20ms` 缓存时间，也不触发 Root 截图。
- 找色、找图、点阵识字和保存截图都读取同一个当前来源。
- `engine_restoreScreenPixels()` 使当前物理帧失效；下一次读取会把实时 Root 截图写入同一地址。
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
- 缓存过期时，`engine_getScreenPixels` 重新截图并覆盖内部点阵缓存。
- `engine_keepCapture()` 后一直复用当前帧。
- `engine_releaseCapture()` 后恢复按时间缓存。
- 图片屏幕激活时缓存时间和锁帧状态不参与读帧，还原后继续生效；还原后的第一次读取
  必定获取实时物理帧。
- 脚本结束时释放固定缓冲区、清除帧状态并恢复默认缓存设置。

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
`core/api -> C ABI -> Lua/JS/Go 绑定` 边界，完整列表见 [统一 API 契约](API_契约.md)。
