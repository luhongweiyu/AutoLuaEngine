# Android Root 模式

当前 Root 模式为已经完成的截图、Root 输入注入和输入法切换服务。

## 当前行为

- App 启动或切换到 Root 模式时准备 root 运行层。
- `engine_capture` 缓存未命中时，通过当前 Android root 截图路线获取 RGBA 点阵。
- `touchDown`、`touchMove`、`touchUp`、`keyDown`、`keyUp`、`keyPress`、`inputText`
  通过常驻 Root helper 注入，不为每条命令拉起 `input` 外部进程。
- `engine_imeLock` / `engine_imeUnlock` 通过 Root helper 执行系统输入法切换；
  `engine_imeSetText` 直接调用已活动的应用输入法，不执行 Root 命令。
- 失败时直接返回错误，不做路线兜底。

## 当前范围

已完成的 Root 能力统一按 `core/api -> system_c_api -> 语言绑定` 分层；尚未实现的
自动化能力继续按这一边界新增，不在 Java 或 Lua 层单独实现业务逻辑。

## 截图接口

```c
int engine_capture(int* width, int* height, unsigned char** pixels);
void engine_keepCapture();
void engine_releaseCapture();
int engine_setCaptureCacheMs(int durationMs);
void engine_clearCaptureCache();
const char* engine_captureLastError();
```

Lua 暂时映射为：

```lua
local w, h, pixels = m.capture()
```

## 输入与输入法接口

```c
int engine_touchDown(int id, int x, int y);
int engine_touchMove(int id, int x, int y);
int engine_touchUp(int id);
int engine_keyDown(const char* keyCode);
int engine_keyUp(const char* keyCode);
int engine_keyPress(const char* keyCode);
int engine_inputText(const char* text);
int engine_imeLock();
int engine_imeSetText(const char* text);
int engine_imeUnlock();
```

`engine_inputText` 适合英文、数字和常见符号。需要完整 Unicode 文本时，Lua 使用
`imeLib.lock()`、`imeLib.setText(text)`、`imeLib.unlock()`。
