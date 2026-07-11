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
int runtime_print(const char* text);
int runtime_log_print(const char* text);
int runtime_sleep(int durationMs);
int runtime_sleep_interruptible(...);
int screen_capture(int* width, int* height, unsigned char** pixels);
void screen_keep_capture();
void screen_release_capture();
int screen_set_capture_cache_ms(int durationMs);
void screen_clear_capture_cache();
const char* screen_last_error();
int color_find(...);
const char* color_last_error();
const EngineApi* engine_get_api();
```

## 当前已清空

旧的系统自动化入口不要从旧代码恢复，需要时重新设计和实现。
