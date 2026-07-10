# Android Root 模式

当前 Root 模式只为已经重写完成的截图核心服务。

## 当前行为

- App 启动或切换到 Root 模式时准备 root 运行层。
- `screen_capture` 缓存未命中时，通过当前 Android root 截图路线获取 RGBA 点阵。
- 失败时直接返回错误，不做路线兜底。

## 当前范围

除截图核心外，其他自动化能力后续需要时重新按 `libengine.so` C ABI 设计和实现。

## 截图接口

```c
int screen_capture(int* width, int* height, unsigned char** pixels);
void screen_keep_capture();
void screen_release_capture();
int screen_set_capture_cache_ms(int durationMs);
void screen_clear_capture_cache();
const char* screen_last_error();
```

Lua 暂时映射为：

```lua
local w, h, pixels = m.capture()
```
