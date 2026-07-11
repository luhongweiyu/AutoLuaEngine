# 插件 SO API

本文记录外部插件 so 复用引擎能力的当前方式。

## 入口

```c
const EngineApi* engine_getApi();
```

插件通过 `dlsym` 找到 `engine_getApi`，取得 `EngineApi` 函数表。函数表由
`libengine.so` 持有，插件只读，不释放。

## 当前函数表能力

```c
typedef struct EngineApi {
    int abiVersion;
    const char* (*getVersion)();
    const char* (*getCapabilitiesJson)();
    int (*print)(const char* text);
    int (*logPrint)(const char* text);
    int (*sleep)(int durationMs);
    int (*sleepInterruptible)(int durationMs, runtime_interrupt_callback cb, void* userData);
    const char* (*runtimeLastError)();
    int (*capture)(int* width, int* height, unsigned char** pixels);
    void (*keepCapture)();
    void (*releaseCapture)();
    int (*setCaptureCacheMs)(int durationMs);
    void (*clearCaptureCache)();
    const char* (*captureLastError)();
    int (*findColors)(int x1, int y1, int x2, int y2, int dir, int sim, const char* colors, EnginePoint* point);
    const char* (*findColorsLastError)();
} EngineApi;
```

## 规则

- 插件只调用 C ABI，不直接访问 C++ 对象。
- `capture` 返回的点阵由 `libengine.so` 持有，插件只读，不释放。
- `findColors` 直接使用当前截图缓存，不带“是否截屏”参数。
- 新能力先进入 `core/api`，再挂到 `system_c_api` 和 `EngineApi`。
