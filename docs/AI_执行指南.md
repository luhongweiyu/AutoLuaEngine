# AI 执行指南

## 当前原则

- 不保留半成品 API。
- 新增脚本 API 先做 `libengine.so/core/api` 核心实现。
- `system_c_api` 只做 C ABI 门面，不写最终业务逻辑。
- Lua / JS / Go 绑定层只做参数转换和返回值封装。
- 文档只记录当前真实可用能力。
- 修改后需要提交并推送。

## 当前已实现

```c
int engine_print(const char* text);
int engine_logPrint(const char* text);
int engine_sleep(int durationMs);
int engine_sleepInterruptible(...);
int engine_capture(int* width, int* height, unsigned char** pixels);
void engine_keepCapture();
void engine_releaseCapture();
int engine_setCaptureCacheMs(int durationMs);
void engine_clearCaptureCache();
const char* engine_captureLastError();
int engine_findColors(...);
const char* engine_findColorsLastError();
const EngineApi* engine_getApi();
```

## 当前已清空

旧的系统自动化入口不要从旧代码恢复，需要时重新设计和实现。
