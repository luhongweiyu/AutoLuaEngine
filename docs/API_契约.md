# 统一 API 契约

当前契约只记录已经按新边界整理完成的能力，避免旧半成品 API 继续误导实现。

## 命名规则

C ABI 使用 snake_case，不带项目缩写，不暴露当前底层路线：

```c
screen_capture
screen_keep_capture
screen_release_capture
screen_set_capture_cache_ms
screen_clear_capture_cache
screen_last_error
```

Lua / JS / Go 后续都绑定同一组 C ABI，不各写一套系统调用。

## 截图 C ABI

```c
int screen_capture(int* width, int* height, unsigned char** pixels);
```

参数：

- `width`：输出宽度。
- `height`：输出高度。
- `pixels`：输出点阵地址。

返回：

- `1`：成功。
- `0`：失败，通过 `screen_last_error()` 取错误。

点阵：

- 固定 RGBA。
- 紧凑排列。
- 长度为 `width * height * 4`。
- 内存由 `libengine.so` 持有，调用方只读，不释放。

## 截图缓存

```c
void screen_keep_capture();
void screen_release_capture();
int screen_set_capture_cache_ms(int durationMs);
void screen_clear_capture_cache();
const char* screen_last_error();
```

规则：

- 默认缓存时间为 `20ms`。
- 缓存命中直接返回当前点阵。
- 缓存过期重新截图并覆盖缓存。
- `screen_keep_capture()` 锁帧。
- `screen_release_capture()` 取消锁帧。
- `screen_clear_capture_cache()` 清空截图缓存。

## Lua 映射

```lua
m.capture()
m.keepCapture()
m.releaseCapture()
m.setCaptureCacheMs(ms)
```

## 暂未定义契约

其他自动化能力暂不保留旧契约，后续按实际实现重新定义。
