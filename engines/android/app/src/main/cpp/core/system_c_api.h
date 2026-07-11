/**
 * 文件用途：声明稳定 C ABI 门面，供 Lua HostApi、JS/Go 绑定和外部 so 复用。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 脚本中断检查回调。
 *
 * 返回非 0 表示当前脚本应停止；返回 0 表示继续等待。
 */
typedef int (*runtime_interrupt_callback)(void* userData);

/**
 * C ABI 通用坐标。
 *
 * 找到目标时 x/y 为命中坐标；未找到或失败时调用方会收到 -1/-1。
 */
typedef struct EnginePoint {
    int x;
    int y;
} EnginePoint;

/**
 * 给外部插件 so 使用的函数表。
 *
 * 插件可以通过 engine_getApi() 取得这张表，然后调用同一套引擎能力，不需要
 * 自己重复声明每个 dlsym 符号。
 */
typedef struct EngineApi {
    int abiVersion;
    const char* (*getVersion)();
    const char* (*getCapabilitiesJson)();
    int (*print)(const char* text);
    int (*logPrint)(const char* text);
    int (*sleep)(int durationMs);
    int (*sleepInterruptible)(
            int durationMs,
            runtime_interrupt_callback shouldInterrupt,
            void* userData
    );
    long long (*systemTime)();
    long long (*tickCount)();
    const char* (*runtimeLastError)();
    int (*capture)(int* width, int* height, unsigned char** pixels);
    void (*keepCapture)();
    void (*releaseCapture)();
    int (*setCaptureCacheMs)(int durationMs);
    void (*clearCaptureCache)();
    const char* (*captureLastError)();
    int (*findColors)(
            int x1,
            int y1,
            int x2,
            int y2,
            int dir,
            int sim,
            const char* colors,
            EnginePoint* point
    );
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
} EngineApi;

/**
 * AutoLuaEngine native 稳定 C ABI。
 *
 * 真实逻辑在 core/api；本文件只暴露跨语言稳定入口。语言绑定层只负责参数转换
 * 和返回值封装，不在 Lua/JS/Go 各自重复实现命令逻辑。
 */
const char* engine_getVersion();

/**
 * 返回当前 native 能力边界的 JSON 描述。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* engine_getCapabilitiesJson();

/**
 * 返回给外部插件 so 使用的引擎函数表。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const EngineApi* engine_getApi();

/**
 * 输出普通脚本日志。
 *
 * 返回 1 表示成功，返回 0 表示失败。
 */
int engine_print(const char* text);

/**
 * 输出日志模块文本。
 *
 * 当前和 engine_print 同级别输出；保留独立入口方便后续区分 print 与 log。
 */
int engine_logPrint(const char* text);

/**
 * 不带中断检查的睡眠。
 *
 * 参数单位为毫秒。返回 1 表示完成，返回 0 表示失败。
 */
int engine_sleep(int durationMs);

/**
 * 可中断睡眠。
 *
 * 参数单位为毫秒。shouldInterrupt 可为空；不为空时，睡眠过程中会定期调用它。
 */
int engine_sleepInterruptible(
        int durationMs,
        runtime_interrupt_callback shouldInterrupt,
        void* userData
);

/**
 * 返回最近一次运行时 C ABI 失败原因。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* engine_runtimeLastError();

/**
 * 返回系统时间戳，单位毫秒。
 *
 * 该值是 Unix epoch 毫秒时间戳，适合记录真实世界时间。
 */
long long engine_systemTime();

/**
 * 返回当前脚本运行时间，单位毫秒。
 *
 * 语言运行时会在脚本开始执行前记录起点；该函数返回当前线程距离这个起点的耗时。
 */
long long engine_tickCount();

/**
 * 屏幕截图。
 *
 * 参数：
 * - width：输出屏幕宽度。
 * - height：输出屏幕高度。
 * - pixels：输出点阵首地址，格式固定为紧凑 RGBA，长度为 width * height * 4。
 *
 * 返回：
 * - 1：成功，width/height/pixels 都已写入。
 * - 0：失败，可通过 engine_captureLastError() 读取错误文本。
 *
 * 注意：
 * pixels 指向 libengine.so 内部缓存，调用方只读，不要释放。下一次缓存过期后
 * 重新截图、engine_clearCaptureCache 或脚本结束清理时，该地址可能失效。
 */
int engine_capture(int* width, int* height, unsigned char** pixels);

/**
 * 锁定当前截图帧。
 *
 * 开启后 engine_capture 会一直返回当前缓存帧；如果当前还没有缓存帧，
 * 下一次 engine_capture 会先截图并锁住这一帧。
 */
void engine_keepCapture();

/**
 * 取消锁帧，恢复按缓存时间判断是否重新截图。
 */
void engine_releaseCapture();

/**
 * 设置截图缓存时间，单位毫秒。
 *
 * 返回 1 表示设置成功，返回 0 表示参数非法。
 */
int engine_setCaptureCacheMs(int durationMs);

/**
 * 清空截图缓存，并恢复默认缓存时间和非锁帧状态。
 */
void engine_clearCaptureCache();

/**
 * 返回最近一次截图 C ABI 调用失败原因。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* engine_captureLastError();

/**
 * 在当前屏幕截图缓存上执行多点找色。
 *
 * 参数：
 * - x1/y1/x2/y2：查找范围。
 * - dir：扫描方向，取值 1 到 8，语义沿用旧找色算法。
 * - sim：默认颜色容差，格式为 0xRRGGBB。
 * - colors：颜色描述，例如 "0|0|FFFFFF,10|5|FF0000-101010"。
 * - point：输出命中坐标。
 *
 * 返回：
 * - 1：找到颜色，point 写入命中坐标。
 * - 0：未找到或失败，point 写入 -1/-1，可通过 engine_findColorsLastError() 读取原因。
 */
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

/**
 * 返回最近一次找色 C ABI 调用失败原因。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* engine_findColorsLastError();

/**
 * 按住不放。
 *
 * id 为模拟手指索引，范围 0 到 4。该函数只走 Root helper 输入注入，不走无障碍。
 * 返回 1 表示 root helper 注入成功，返回 0 表示失败。
 */
int engine_touchDown(int id, int x, int y);

/**
 * 移动已按下的模拟手指。
 */
int engine_touchMove(int id, int x, int y);

/**
 * 弹起模拟手指。
 */
int engine_touchUp(int id);

/**
 * 按下一个按键不弹起。
 *
 * keyCode 支持数字字符串和常用按键标识符，例如 Home、Back、VolUp。
 */
int engine_keyDown(const char* keyCode);

/**
 * 弹起一个按键。
 */
int engine_keyUp(const char* keyCode);

/**
 * 按一下按键并弹起。
 */
int engine_keyPress(const char* keyCode);

/**
 * 模拟输入文字。
 *
 * 当前通过 Root 注入 KeyEvent 实现，适合英文、数字和常见符号。
 */
int engine_inputText(const char* text);

/**
 * 返回当前运行环境类型。
 *
 * 当前输入注入只认 root；没有可用 Root helper 时返回 none。
 */
const char* engine_getRunEnvType();

/**
 * 返回最近一次输入 C ABI 失败原因。
 */
const char* engine_inputLastError();

#ifdef __cplusplus
}
#endif
