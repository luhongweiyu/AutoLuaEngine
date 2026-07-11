# 统一 API 契约

当前契约只记录已经按新边界整理完成的能力。

## 分层规则

所有脚本 API 的真实逻辑先放在 `libengine.so/core/api`，语言绑定层只负责参数和
返回值转换。`system_c_api` 只负责把 `core/api` 包成稳定 C ABI。

C ABI 使用 snake_case，不带项目缩写，不暴露当前底层路线。

当前运行时 C ABI：

```c
runtime_print
runtime_log_print
runtime_sleep
runtime_sleep_interruptible
runtime_last_error
```

当前截图 C ABI：

```c
screen_capture
screen_keep_capture
screen_release_capture
screen_set_capture_cache_ms
screen_clear_capture_cache
screen_last_error
```

当前找色 C ABI：

```c
color_find
color_last_error
```

当前插件函数表入口：

```c
engine_get_api
```

Lua 当前通过 HostApi 暴露脚本函数，但 HostApi 只做 Lua 类型转换，并调用同一组
C ABI。JS / Go 后续也按同样方式绑定，不各写一套命令逻辑。

## 运行时 C ABI

```c
int runtime_print(const char* text);
int runtime_log_print(const char* text);
int runtime_sleep(int durationMs);
int runtime_sleep_interruptible(
        int durationMs,
        runtime_interrupt_callback shouldInterrupt,
        void* userData
);
const char* runtime_last_error();
```

说明：

- `runtime_print`：普通脚本输出。
- `runtime_log_print`：日志模块输出。
- `runtime_sleep`：无中断上下文的睡眠。
- `runtime_sleep_interruptible`：带脚本停止回调的睡眠，Lua 的 `m.sleep` 当前使用它。

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

## 找色 C ABI

```c
typedef struct EnginePoint {
    int x;
    int y;
} EnginePoint;

int color_find(
        int x1,
        int y1,
        int x2,
        int y2,
        int dir,
        int sim,
        const char* colors,
        EnginePoint* point
);
const char* color_last_error();
```

规则：

- `color_find` 直接使用当前截图缓存，不带“是否截屏”参数。
- 截图是否刷新由 `screen_capture` 的缓存时间、`screen_keep_capture` 和 `screen_release_capture` 控制。
- `dir` 取值为 `1` 到 `8`，沿用旧找色算法扫描方向。
- `sim` 为默认容差，格式为 `0xRRGGBB`。
- `colors` 格式示例：`0|0|FFFFFF,10|5|FF0000-101010`。
- 找到返回 `1`，`point.x/point.y` 为命中坐标。
- 未找到或失败返回 `0`，`point.x/point.y` 为 `-1/-1`，原因通过 `color_last_error()` 获取。

## 插件函数表

```c
const EngineApi* engine_get_api();
```

外部插件 so 可以通过 `engine_get_api()` 取得函数表，复用当前运行时、截图和找色
C ABI。函数表只放稳定 C 类型，不暴露 C++ 对象。

## Lua 映射

```lua
print(...)
m.sleep(ms)
m.log.print(text)
m.capture()
m.keepCapture()
m.releaseCapture()
m.setCaptureCacheMs(ms)
m.findColor(x1, y1, x2, y2, dir, sim, colors)
```

## 暂未定义契约

其他自动化能力暂不保留旧契约，后续按实际实现重新定义。
