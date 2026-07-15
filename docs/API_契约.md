# 统一 API 契约

当前契约只记录已经按新边界整理完成的能力。

## 分层规则

固定脚本 API 的真实逻辑先放在 `libengine.so/core/api`，语言绑定层只负责参数和
返回值转换。`system_c_api` 只负责把 `core/api` 包成稳定 C ABI。

动态语言互操作不伪装成固定命令。Android `import` 由 `libengine.so` 注册 Lua
userdata 和元方法，再通过 JNI 调用统一 `JavaInteropBridge`。JS / Go 后续复用
Java 后端，但使用各自对象包装。

语言自身的并发语义同样不伪装成通用 C ABI。Lua 的 `beginThread`、
`Thread.newThread` 和 VM Gate 位于 `libengine.so/runtime/lua`；JS 和 Go 后续分别使用
自己的事件循环或 goroutine。

C ABI 统一使用 `engine_` 前缀，不带项目缩写，不暴露当前底层路线。后缀沿用已确定
脚本 API 的命名，例如 `engine_inputText`、`engine_imeSetText`，避免跨层出现不同名称。

Android 的 Root 执行边界不属于 C ABI：固定 API 仍是 `libengine.so -> system_c_api -> AndroidBridge`，
AndroidBridge 再通过 `:engine` 的认证 socket 请求主进程预先启动的 RootDaemon。脚本、JS、Go 和
插件不会各自启动 `su`，也不需要感知 RootDaemon 的端口或令牌。

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

当前 ALPKG 资源 C ABI：

```c
int engine_readAlpkgFile(
        const char* relativePath,
        const unsigned char** data,
        size_t* size
);
```

`engine_readAlpkgFile` 只允许当前 ALPKG 脚本任务及其已绑定上下文的 native 子线程读取 manifest 中 `resource` 类型的
项目相对路径。成功返回 `1`，数据由 SO 当前线程持有且只读；失败返回 `0`，原因通过
`engine_runtimeLastError()` 获取。该数据地址在同线程下一次读取前有效，语言绑定必须复制
需要长期保存的数据。Lua、未来 JS/Go 和插件都通过这个 ABI 复用同一条读取路径。

当前截图 C ABI：

```c
engine_capture
engine_keepCapture
engine_releaseCapture
engine_setCaptureCacheMs
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

当前输入法 C ABI：

```c
engine_imeLock
engine_imeSetText
engine_imeUnlock
engine_imeLastError
```

当前设备 C ABI：

```c
engine_getDeviceApi
engine_appIsFront
engine_appIsRunning
engine_frontAppName
engine_getDisplayInfoJson
engine_getInstalledAppsJson
engine_exec
engine_exitScript
engine_deviceLastError
```

完整接口及 Lua 返回结构见 [脚本文档 · 设备](脚本文档.md)；实现与 C ABI 见 [Android 设备 API](ANDROID_设备_API.md)。

当前脚本 UI C ABI：

```c
engine_uiOpen
engine_uiUpdate
engine_uiPostMessage
engine_uiClose
engine_uiWaitEvent
engine_uiWaitEventInterruptible
engine_uiCloseAll
engine_uiLastError
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
const char* engine_captureLastError();
```

规则：

- 默认缓存时间为 `20ms`。
- 缓存命中直接返回当前点阵。
- 缓存过期重新截图并覆盖缓存。
- `engine_keepCapture()` 锁帧。
- `engine_releaseCapture()` 取消锁帧。
- 脚本运行中不会主动清空截图缓存；脚本结束时由引擎内部统一释放。

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

- 输入注入只走 RootDaemon 常驻特权进程，不走无障碍。
- `touchDown` / `touchUp` 在 Lua 层不返回值；C ABI 返回值只用于内部判断和其他语言绑定。
- `touchMove`、`keyDown`、`keyUp`、`keyPress`、`inputText` 返回布尔语义。
- `keyCode` 支持数字字符串和 `Home`、`Back`、`VolUp` 等常用标识符。
- `inputText` 当前通过按键事件输入文本，适合英文、数字和常见符号。

## 输入法 C ABI

```c
int engine_imeLock();
int engine_imeSetText(const char* text);
int engine_imeUnlock();
const char* engine_imeLastError();
```

规则：

- `engine_imeLock` 保存当前默认输入法后，启用并切换到 小鱼精灵 输入法。
- `engine_imeSetText` 只通过已经活动的 小鱼精灵 输入法提交 Unicode 文本；不会重复
  执行 Root 命令，也不回退到按键注入或无障碍。
- `engine_imeUnlock` 恢复 lock 前保存的原默认输入法，并禁用 小鱼精灵 输入法。
- `engine_imeLock` / `engine_imeUnlock` 只走 RootDaemon；调用失败通过
  `engine_imeLastError` 获取原因。

## 设备 C ABI

设备函数表由 `engine_getDeviceApi()` 返回。应用状态、硬件信息、安装应用列表、系统控制和
Root shell 都进入同一个 `EngineDeviceApi`，不由 Lua、JS、Go 或插件各自直连 Android。

```c
const EngineDeviceApi* engine_getDeviceApi();
int engine_appIsFront(const char* packageName);
int engine_appIsRunning(const char* packageName);
const char* engine_frontAppName();
const char* engine_getDisplayInfoJson();
const char* engine_getInstalledAppsJson();
const char* engine_exec(const char* command, int isRet);
int engine_exitScript();
const char* engine_deviceLastError();
```

规则：

- `EngineApi` 和 `EngineDeviceApi` 都有自己的 `abiVersion`。函数表只能尾部追加字段；
  插件先检查所需版本再访问新增字段，旧插件继续使用已有字段时保持可用。
- 结构化结果一律以 JSON 文本从 C ABI 返回；Lua HostApi 才转换为 table。
- 设备字符串、JSON 和错误文本由调用线程持有，下一次设备调用可能覆盖内容。
- `engine_exec` 只返回 shell 合并输出，不根据命令退出码改变成功状态；调用方自行判断。
- Root 控制命令只请求常驻 RootDaemon，不重复申请 `su`，也没有无障碍回退。

## 脚本 UI C ABI

```c
long long engine_uiOpen(const char* surface, const char* specJson);
int engine_uiUpdate(long long sessionId, const char* specJson);
int engine_uiPostMessage(long long sessionId, const char* messageJson);
int engine_uiClose(long long sessionId);
const char* engine_uiWaitEvent(long long sessionId, int timeoutMs);
const char* engine_uiWaitEventInterruptible(
        long long sessionId,
        int timeoutMs,
        runtime_interrupt_callback shouldInterrupt,
        void* userData
);
void engine_uiCloseAll();
const char* engine_uiLastError();
```

规则：

- `surface` 当前支持 `dialog`、`hud`、`web`，配置和消息均使用完整 JSON 文本。
- 成功创建时 `engine_uiOpen` 返回大于 `0` 的会话 ID；失败返回 `0`，原因通过
  `engine_uiLastError()` 获取。
- `engine_uiWaitEvent*` 成功时返回 `{"type":...,"data":...}` JSON；超时也是正常事件
  `{"type":"timeout","data":null}`。失败返回空字符串。
- Lua 等脚本语言必须使用 `engine_uiWaitEventInterruptible`，这样停止脚本可以中断等待。
- Android UI 线程只把事件投递进 native 会话队列，不能直接执行语言运行时。
- `engine_uiCloseAll` 在脚本结束、停止和引擎销毁时调用，确保 App 主进程没有遗留界面。

## 插件函数表

```c
const EngineApi* engine_getApi();
```

外部插件 so 可以通过 `engine_getApi()` 取得函数表，再使用 `getDeviceApi()` 访问设备
能力；运行时、截图、找色、输入、输入法和脚本 UI 仍位于顶层 `EngineApi`。函数表只放
稳定 C 类型，不暴露 C++ 对象。

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
imeLib.lock()
imeLib.setText(text)
imeLib.unlock()
m.sleep(ms)
m.systemTime()
m.tickCount()
m.log.print(text)
m.capture()
m.keepCapture()
m.releaseCapture()
m.setCaptureCacheMs(ms)
m.findColors(x1, y1, x2, y2, dir, sim, colors)
m.ime.lock()
m.ime.setText(text)
m.ime.unlock()
m.dialog.alert(...)
m.dialog.confirm(...)
m.dialog.input(...)
m.dialog.select(...)
m.ui.form(spec)
m.hud.show(id, spec)
m.hud.update(id, patch)
m.hud.hide(id)
m.hud.waitEvent(id, timeoutMs)
m.web.open(spec)
m.web.waitEvent(handle, timeoutMs)
m.web.postMessage(handle, data)
m.web.close(handle)
```

## 暂未定义契约

其他自动化能力暂不保留旧契约，后续按实际实现重新定义。
