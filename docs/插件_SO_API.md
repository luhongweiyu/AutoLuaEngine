# 插件 SO API

本文记录外部插件 so 复用引擎能力的当前方式。

## 入口

```c
const EngineApi* engine_getApi();
```

插件通过 `dlsym` 找到 `engine_getApi`，取得 `EngineApi` 函数表。函数表由
`libengine.so` 持有，插件只读，不释放。

当前 `abiVersion` 为 `11`。版本 11 移除了未使用的 `clearCaptureCache` 字段，因此
旧插件必须使用当前头文件重编译；后续新增字段只追加到结构体末尾。

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
    long long (*systemTime)();
    long long (*tickCount)();
    const char* (*runtimeLastError)();
    int (*capture)(int* width, int* height, unsigned char** pixels);
    void (*keepCapture)();
    void (*releaseCapture)();
    int (*setCaptureCacheMs)(int durationMs);
    const char* (*captureLastError)();
    int (*findColors)(int x1, int y1, int x2, int y2, int dir, int sim, const char* colors, EnginePoint* point);
    const char* (*findColorsLastError)();
    int (*touchDown)(int id, int x, int y);
    int (*touchMove)(int id, int x, int y);
    int (*touchUp)(int id);
    int (*keyDown)(const char* keyCode);
    int (*keyUp)(const char* keyCode);
    int (*keyPress)(const char* keyCode);
    int (*inputText)(const char* text);
    const char* (*getRunEnvType)();
    const char* (*inputLastError)();
    int (*imeLock)();
    int (*imeSetText)(const char* text);
    int (*imeUnlock)();
    const char* (*imeLastError)();
    long long (*uiOpen)(const char* surface, const char* specJson);
    int (*uiUpdate)(long long sessionId, const char* specJson);
    int (*uiPostMessage)(long long sessionId, const char* messageJson);
    int (*uiClose)(long long sessionId);
    const char* (*uiWaitEvent)(long long sessionId, int timeoutMs);
    const char* (*uiWaitEventInterruptible)(
        long long sessionId,
        int timeoutMs,
        runtime_interrupt_callback cb,
        void* userData
    );
    void (*uiCloseAll)();
    const char* (*uiLastError)();
    int (*readAlpkgFile)(
        const char* relativePath,
        const unsigned char** data,
        size_t* size
    );
} EngineApi;
```

## 规则

- 插件只调用 C ABI，不直接访问 C++ 对象。
- `capture` 返回的点阵由 `libengine.so` 持有，插件只读，不释放。
- `findColors` 直接使用当前截图缓存，不带“是否截屏”参数。
- `systemTime` 返回 Unix 毫秒时间戳；`tickCount` 返回当前顶层脚本运行耗时，Lua 主任务和子线程共享起点。
- `touch/key/inputText` 只走 Root helper 常驻进程，不走无障碍。
- `imeLock/imeUnlock` 通过 Root helper 保存、切换和恢复系统输入法；`imeSetText` 通过
  已锁定的 小鱼精灵 输入法提交 Unicode 文本，不执行额外 Root 命令。
- `uiOpen` 的 `surface` 当前为 `dialog`、`hud`、`web`；配置和消息参数均为 JSON 文本。
- `uiWaitEvent` 返回 `{"type":...,"data":...}` JSON，`uiWaitEventInterruptible` 适用于
  需要响应脚本停止请求的语言运行时。
- `readAlpkgFile` 仅在当前调用线程正在运行 `.alpkg` 时可用，只读取 manifest 的
  `resource` 条目；返回字节由 SO 当前线程持有，插件只读、不释放，并应在下次调用前复制。
- 新能力先进入 `core/api`，再挂到 `system_c_api` 和 `EngineApi`。
