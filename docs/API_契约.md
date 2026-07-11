# 统一 API 契约

当前契约只记录已经按新边界整理完成的能力。

## 分层规则

固定脚本 API 的真实逻辑先放在 `libengine.so/core/api`，语言绑定层只负责参数和
返回值转换。`system_c_api` 只负责把 `core/api` 包成稳定 C ABI。

动态语言互操作不伪装成固定命令。Android `import` 由 `libengine.so` 注册 Lua
userdata 和元方法，再通过 JNI 调用统一 `JavaInteropBridge`。JS / Go 后续复用
Java 后端，但使用各自对象包装。

C ABI 使用 snake_case，不带项目缩写，不暴露当前底层路线。

当前运行时 C ABI：

```c
engine_print
engine_logPrint
engine_sleep
engine_sleepInterruptible
engine_systemTime
engine_tickCount
engine_runtimeLastError
```

当前截图 C ABI：

```c
engine_capture
engine_keepCapture
engine_releaseCapture
engine_setCaptureCacheMs
engine_clearCaptureCache
engine_captureLastError
```

当前找色 C ABI：

```c
engine_findColors
engine_findColorsLastError
```

当前输入 C ABI：

```c
engine_touchDown
engine_touchMove
engine_touchUp
engine_keyDown
engine_keyUp
engine_keyPress
engine_inputText
engine_getRunEnvType
engine_inputLastError
```

当前插件函数表入口：

```c
engine_getApi
```

Lua 当前通过 HostApi 暴露脚本函数，但 HostApi 只做 Lua 类型转换，并调用同一组
C ABI。JS / Go 后续也按同样方式绑定，不各写一套命令逻辑。

## 动态 Java 互操作契约

```lua
import("java.lang.*")
import("完整.Java.类名")
```

规则：

- 完整类名导入后按简单类名写入 `_G`。
- `package.*` 使用延迟类解析，首次成功解析后缓存到 `_G`。
- Java 对象在 Lua 中是 userdata，不是数字句柄。
- 支持字段、方法、构造函数、公开内部类、重载、数组、集合和接口代理。
- Java `void` 对应 Lua 无返回值；Java `null` 对应一个 `nil`。
- Java 异常转换为 Lua 错误。
- 对象由 JNI GlobalRef 保活，Lua userdata 回收或运行时结束时释放。

该能力没有“每个 Java 方法一条 C ABI”的函数表。实现和线程规则见
`docs/ANDROID_Java互操作.md`。

## 运行时 C ABI

```c
int engine_print(const char* text);
int engine_logPrint(const char* text);
int engine_sleep(int durationMs);
int engine_sleepInterruptible(
        int durationMs,
        runtime_interrupt_callback shouldInterrupt,
        void* userData
);
long long engine_systemTime();
long long engine_tickCount();
const char* engine_runtimeLastError();
```

说明：

- `engine_print`：普通脚本输出。
- `engine_logPrint`：日志模块输出。
- `engine_sleep`：无中断上下文的睡眠。
- `engine_sleepInterruptible`：带脚本停止回调的睡眠，Lua 的 `m.sleep` 当前使用它。
- `engine_systemTime`：系统 Unix 毫秒时间戳。
- `engine_tickCount`：当前脚本运行时间，单位毫秒。

## 截图 C ABI

```c
int engine_capture(int* width, int* height, unsigned char** pixels);
```

参数：

- `width`：输出宽度。
- `height`：输出高度。
- `pixels`：输出点阵地址。

返回：

- `1`：成功。
- `0`：失败，通过 `engine_captureLastError()` 取错误。

点阵：

- 固定 RGBA。
- 紧凑排列。
- 长度为 `width * height * 4`。
- 内存由 `libengine.so` 持有，调用方只读，不释放。

## 截图缓存

```c
void engine_keepCapture();
void engine_releaseCapture();
int engine_setCaptureCacheMs(int durationMs);
void engine_clearCaptureCache();
const char* engine_captureLastError();
```

规则：

- 默认缓存时间为 `20ms`。
- 缓存命中直接返回当前点阵。
- 缓存过期重新截图并覆盖缓存。
- `engine_keepCapture()` 锁帧。
- `engine_releaseCapture()` 取消锁帧。
- `engine_clearCaptureCache()` 清空截图缓存。

## 找色 C ABI

```c
typedef struct EnginePoint {
    int x;
    int y;
} EnginePoint;

int engine_findColors(
        int x1,
        int y1,
        int x2,
        int y2,
        int dir,
        int sim,
        const char* colors,
        EnginePoint* point
);
const char* engine_findColorsLastError();
```

规则：

- `engine_findColors` 直接使用当前截图缓存，不带“是否截屏”参数。
- 截图是否刷新由 `engine_capture` 的缓存时间、`engine_keepCapture` 和 `engine_releaseCapture` 控制。
- `dir` 取值为 `1` 到 `8`，沿用旧找色算法扫描方向。
- `sim` 为默认容差，格式为 `0xRRGGBB`。
- `colors` 格式示例：`0|0|FFFFFF,10|5|FF0000-101010`。
- 找到返回 `1`，`point.x/point.y` 为命中坐标。
- 未找到或失败返回 `0`，`point.x/point.y` 为 `-1/-1`，原因通过 `engine_findColorsLastError()` 获取。

## 输入 C ABI

```c
int engine_touchDown(int id, int x, int y);
int engine_touchMove(int id, int x, int y);
int engine_touchUp(int id);
int engine_keyDown(const char* keyCode);
int engine_keyUp(const char* keyCode);
int engine_keyPress(const char* keyCode);
int engine_inputText(const char* text);
const char* engine_getRunEnvType();
const char* engine_inputLastError();
```

规则：

- 输入注入只走 Root helper 常驻进程，不走无障碍。
- `touchDown` / `touchUp` 在 Lua 层不返回值；C ABI 返回值只用于内部判断和其他语言绑定。
- `touchMove`、`keyDown`、`keyUp`、`keyPress`、`inputText` 返回布尔语义。
- `keyCode` 支持数字字符串和 `Home`、`Back`、`VolUp` 等常用标识符。
- `inputText` 当前通过按键事件输入文本，适合英文、数字和常见符号。

## 插件函数表

```c
const EngineApi* engine_getApi();
```

外部插件 so 可以通过 `engine_getApi()` 取得函数表，复用当前运行时、截图和找色
C ABI。函数表只放稳定 C 类型，不暴露 C++ 对象。

## Lua 映射

```lua
print(...)
sleep(ms)
systemTime()
tickCount()
getRunEnvType()
touchDown(id, x, y)
touchMove(id, x, y)
touchUp(id)
keyDown(keycode)
keyUp(keycode)
keyPress(keycode)
inputText(text)
m.sleep(ms)
m.systemTime()
m.tickCount()
m.log.print(text)
m.capture()
m.keepCapture()
m.releaseCapture()
m.setCaptureCacheMs(ms)
m.findColors(x1, y1, x2, y2, dir, sim, colors)
```

## 暂未定义契约

其他自动化能力暂不保留旧契约，后续按实际实现重新定义。
