# AI 执行指南

## 当前原则

- 不保留半成品 API。
- 新增脚本 API 先做 `libengine.so/core/api` 核心实现。
- `system_c_api` 只做 C ABI 门面，不写最终业务逻辑。
- Lua / JS / Go 绑定层只做参数转换和返回值封装。
- 语言自身的运行时语义放在对应 `runtime/<语言>`；Lua 多线程不伪装成通用 C ABI。
- 文档只记录当前真实可用能力。
- 修改后按用户要求提交；不自动推送远程仓库。

## 当前已实现

```c
int engine_print(const char* text);
int engine_logPrint(const char* text);
int engine_sleep(int durationMs);
int engine_sleepInterruptible(...);
int engine_getScreenPixels(int* width, int* height, unsigned char** pixels);
int engine_setScreenPixels(const char* imagePath);
int engine_restoreScreenPixels();
void engine_keepCapture();
void engine_releaseCapture();
int engine_setCaptureCacheMs(int durationMs);
const char* engine_screenLastError();
int engine_capture(const char* path, const EngineRect* region);
int engine_findColors(...);
const char* engine_findColorsLastError();
long long engine_uiOpen(const char* surface, const char* specJson);
int engine_uiUpdate(long long sessionId, const char* specJson);
int engine_uiPostMessage(long long sessionId, const char* messageJson);
int engine_uiClose(long long sessionId);
const char* engine_uiWaitEvent(...);
void engine_uiCloseAll();
const char* engine_uiLastError();
const EngineApi* engine_getApi();
```

## 当前已清空

旧的系统自动化入口不要从旧代码恢复，需要时重新设计和实现。
