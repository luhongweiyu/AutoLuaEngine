# 插件 SO API

本文记录外部插件 so 复用引擎能力的当前方式。

## 入口

```c
const EngineApi* engine_get_api();
```

插件通过 `dlsym` 找到 `engine_get_api`，取得 `EngineApi` 函数表。函数表由
`libengine.so` 持有，插件只读，不释放。

## 当前函数表能力

```c
typedef struct EngineApi {
    int abiVersion;
    const char* (*engine_version)();
    const char* (*engine_capabilities_json)();
    int (*runtime_print)(const char* text);
    int (*runtime_log_print)(const char* text);
    int (*runtime_sleep)(int durationMs);
    int (*runtime_sleep_interruptible)(int durationMs, runtime_interrupt_callback cb, void* userData);
    const char* (*runtime_last_error)();
    int (*screen_capture)(int* width, int* height, unsigned char** pixels);
    void (*screen_keep_capture)();
    void (*screen_release_capture)();
    int (*screen_set_capture_cache_ms)(int durationMs);
    void (*screen_clear_capture_cache)();
    const char* (*screen_last_error)();
    int (*color_find)(int x1, int y1, int x2, int y2, int dir, int sim, const char* colors, EnginePoint* point);
    const char* (*color_last_error)();
} EngineApi;
```

## 规则

- 插件只调用 C ABI，不直接访问 C++ 对象。
- `screen_capture` 返回的点阵由 `libengine.so` 持有，插件只读，不释放。
- `color_find` 直接使用当前截图缓存，不带“是否截屏”参数。
- 新能力先进入 `core/api`，再挂到 `system_c_api` 和 `EngineApi`。
