# AI 执行指南

## 当前原则

- 不保留半成品 API。
- 新增能力先设计 `libengine.so` C ABI。
- Lua / JS / Go 都绑定同一层 C ABI。
- 文档只记录当前真实可用能力。
- 修改后需要提交并推送。

## 当前已实现

```c
int screen_capture(int* width, int* height, unsigned char** pixels);
void screen_keep_capture();
void screen_release_capture();
int screen_set_capture_cache_ms(int durationMs);
void screen_clear_capture_cache();
const char* screen_last_error();
```

## 当前已清空

旧的系统自动化入口不要从旧代码恢复，需要时重新设计和实现。
