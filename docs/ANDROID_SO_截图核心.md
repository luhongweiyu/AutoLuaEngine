# Android SO 截图核心

本文记录当前 `libengine.so` 已重写完成的截图核心。当前接口直接返回宽、高和点阵地址，不再返回中间资源句柄。

## 核心实现和 C ABI

真实截图缓存和 Root 截图分发位于：

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
- `pixels` 由 `libengine.so` 内部缓存持有，调用方只读，不释放；脚本结束后由引擎统一释放。

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
- 脚本运行中不会主动清空截图缓存；脚本结束时由引擎内部统一释放。

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
m.capture(path[, left, top, right, bottom])
m.snapShot(path[, left, top, right, bottom])
m.keepCapture()
m.releaseCapture()
m.setCaptureCacheMs(ms)
```

本文只描述截图核心。输入、输入法和其他已实现的脚本 API 仍遵循
`core/api -> C ABI -> Lua/JS/Go 绑定` 边界，完整列表见 [统一 API 契约](API_契约.md)。
