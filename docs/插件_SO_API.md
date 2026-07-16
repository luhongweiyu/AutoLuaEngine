# 插件 SO API

本文记录外部插件 so 复用引擎能力的当前方式。

## 入口

```c
const EngineApi* engine_getApi();
```

插件通过 `dlsym` 找到 `engine_getApi`，取得 `EngineApi` 函数表。函数表由
`libengine.so` 持有，插件只读，不释放。

当前 `EngineApi::abiVersion` 为 `14`。版本 14 将获取点阵字段明确命名为 `getScreenPixels`，
将保存图片字段命名为 `capture`，支持可选 `EngineRect` 区域，并在函数表尾部追加模板缓存
上限设置。新插件必须使用当前头文件编译，并在使用这些字段前检查版本不低于 14。

## 当前函数表能力

```c
typedef struct EngineRect {
    int left;
    int top;
    int right;
    int bottom;
} EngineRect;

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
    int (*getScreenPixels)(int* width, int* height, unsigned char** pixels);
    void (*keepCapture)();
    void (*releaseCapture)();
    int (*setCaptureCacheMs)(int durationMs);
    const char* (*screenLastError)();
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
    const EngineDeviceApi* (*getDeviceApi)();
    int (*capture)(const char* path, const EngineRect* region);
    int (*findPic)(
        int x1, int y1, int x2, int y2,
        const char* picName, const char* deltaColor,
        int dir, double sim, EnginePoint* point
    );
    void (*clearImageCache)(const char* picName);
    const char* (*imageLastError)();
    int (*ocrLoadModel)(
        const char* name, const char* detPath, const char* recPath,
        const char* clsPath, const char* keysPath, int threads
    );
    int (*ocrReleaseModel)(const char* name);
    int (*ocrIsModelLoaded)(const char* name);
    const char* (*ocrRead)(const char* name, const char* imagePath, const char* optionsJson);
    const char* (*ocrFindText)(
        const char* name, const char* imagePath,
        const char* text, const char* optionsJson
    );
    const char* (*ocrLastError)();
    int (*fontSetDict)(int index, const char* dictionary);
    int (*fontAddDict)(int index, const char* dictionary);
    int (*fontUseDict)(int index);
    const char* (*fontGetPixel)(int x1, int y1, int x2, int y2, const char* color);
    const char* (*fontOcr)(int x1, int y1, int x2, int y2, const char* color, double sim);
    int (*fontFindStr)(
        int x1, int y1, int x2, int y2,
        const char* text, const char* color, double sim,
        EnginePoint* point
    );
    const char* (*fontFindStrEx)(
        int x1, int y1, int x2, int y2,
        const char* text, const char* color, double sim
    );
    const char* (*fontLastError)();
    int (*setImageCacheMaxBytes)(size_t maxBytes);
} EngineApi;
```

设备子表通过 `api->getDeviceApi()` 获取。插件必须先确认 `api->abiVersion >= 12`，再读取该
字段；取得子表后再检查 `device->abiVersion` 是否满足自身所需版本。它包含应用状态、应用启动/停止、安装 APK、
Root `exec`、硬件信息、屏幕信息、传感器、网络/电话信息和系统控制函数。完整声明不在本文
重复维护，以 [system_c_api.h](../engines/android/app/src/main/cpp/core/system_c_api.h) 和
[脚本文档 · 设备](脚本文档.md) 与 [Android 设备 API](ANDROID_设备_API.md) 为准：

```c
const EngineDeviceApi* device = api->getDeviceApi();
int foreground = device->appIsFront("com.example.app");
const char* output = device->exec("id", 1);
const char* displayJson = device->getDisplayInfoJson();
```

## 规则

- 插件只调用 C ABI，不直接访问 C++ 对象。
- `getScreenPixels` 返回的点阵由 `libengine.so` 持有，插件只读，不释放。
- `findColors` 直接使用当前截图缓存，不带“是否截屏”参数。
- `capture` 的 `region` 为空时保存全屏，非空时保存左闭右开的指定区域。Lua 的 `snapShot`
  只是 `capture` 的直接别名，不占用额外函数表字段。`findPic` 同样直接复用截图缓存，
  模板默认按 `5 MiB` 字节上限执行 LRU，脚本结束后全部释放；`setImageCacheMaxBytes(0)` 可
  关闭当前脚本的模板缓存。
- `ocrLoadModel` / `ocrReleaseModel` 由插件显式管理 RapidOCR ONNX 模型；重复加载同名同配置
  会复用，`ocrRead` 和 `ocrFindText` 返回当前调用线程持有的 JSON 文本。
- `fontSetDict` 支持 `文字$宽$高$十六进制点阵` 手机可变尺寸字库，也兼容简化 11 行格式和
  大漠/懒人带末尾字高元数据的旧字库；`fontOcr` / `fontFindStr` 直接读取当前截图缓存。
- `systemTime` 返回 Unix 毫秒时间戳；`tickCount` 返回当前顶层脚本运行耗时，Lua 主任务和子线程共享起点。
- `touch/key/inputText` 只走 Root helper 常驻进程，不走无障碍。
- `imeLock/imeUnlock` 通过 Root helper 保存、切换和恢复系统输入法；`imeSetText` 通过
  已锁定的 小鱼精灵 输入法提交 Unicode 文本，不执行额外 Root 命令。
- `uiOpen` 的 `surface` 当前为 `dialog`、`hud`、`web`；配置和消息参数均为 JSON 文本。
- `uiWaitEvent` 返回 `{"type":...,"data":...}` JSON，`uiWaitEventInterruptible` 适用于
  需要响应脚本停止请求的语言运行时。
- `readAlpkgFile` 仅在当前调用线程正在运行 `.alpkg` 时可用，只读取 manifest 的
  `resource` 条目；返回字节由 SO 当前线程持有，插件只读、不释放，并应在下次调用前复制。
- `getDeviceApi` 返回的字符串、JSON 和 `lastError` 指针由调用线程持有，下一次设备 API
  调用可能覆盖内容；结构化信息统一为 JSON，不暴露 Lua table 或 Java 对象。
- 新能力先进入 `core/api`，再挂到 `system_c_api` 和 `EngineApi`。
