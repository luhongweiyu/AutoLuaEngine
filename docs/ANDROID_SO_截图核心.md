# Android SO 截图核心

本文记录当前 `libengine.so` 已重写完成的截图核心。旧的图片句柄池、`id` 元信息、`getPixel/getPixels` 暂时已从 native 对外路径移除。

## C ABI

头文件：

```text
engines/android/app/src/main/cpp/core/system_c_api.h
```

Root 截图命令：

```c
int ael_root_capture(int* width, int* height, unsigned char** pixels);
```

调用方式：

```c
int w = 0;
int h = 0;
unsigned char* pixels = 0;
int ok = ael_root_capture(&w, &h, &pixels);
```

返回值：

- `ok == 1`：成功，`w`、`h`、`pixels` 有效。
- `ok == 0`：失败，通过 `ael_last_error()` 获取错误。
- `pixels` 固定为紧凑 RGBA，长度为 `w * h * 4`。
- `pixels` 由 `libengine.so` 内部缓存持有，调用方只读，不释放。

## 缓存控制

```c
void ael_keep_capture();
void ael_release_capture();
int ael_set_capture_cache_ms(int durationMs);
void ael_clear_capture_cache();
const char* ael_last_error();
```

规则：

- 默认缓存时间：`20ms`。
- 缓存有效时，`ael_root_capture` 直接返回当前点阵。
- 缓存过期时，`ael_root_capture` 重新 root 截图并覆盖内部点阵缓存。
- `ael_keep_capture()` 后一直复用当前帧。
- `ael_release_capture()` 后恢复按时间缓存。
- 脚本开始和结束会调用 `ael_clear_capture_cache()`。

## Lua 临时绑定

当前 Lua 绑定只保留截图核心：

```lua
local w, h, pixels = m.capture()
```

失败时：

```lua
local w, err = m.capture()
```

可用函数：

```lua
m.capture()
m.rootCapture()
m.keepCapture()
m.releaseCapture()
m.setCaptureCacheMs(ms)
```

触控、滑动、按键、输入、找色等 API 已先从 Lua/协议暴露层清空，等待按新的 C ABI 边界重新实现。
