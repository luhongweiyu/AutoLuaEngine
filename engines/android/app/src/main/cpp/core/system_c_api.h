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
 * 插件可以通过 engine_get_api() 取得这张表，然后调用同一套引擎能力，不需要
 * 自己重复声明每个 dlsym 符号。
 */
typedef struct EngineApi {
    int abiVersion;
    const char* (*engine_version)();
    const char* (*engine_capabilities_json)();
    int (*runtime_print)(const char* text);
    int (*runtime_log_print)(const char* text);
    int (*runtime_sleep)(int durationMs);
    int (*runtime_sleep_interruptible)(
            int durationMs,
            runtime_interrupt_callback shouldInterrupt,
            void* userData
    );
    const char* (*runtime_last_error)();
    int (*screen_capture)(int* width, int* height, unsigned char** pixels);
    void (*screen_keep_capture)();
    void (*screen_release_capture)();
    int (*screen_set_capture_cache_ms)(int durationMs);
    void (*screen_clear_capture_cache)();
    const char* (*screen_last_error)();
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
} EngineApi;

/**
 * AutoLuaEngine native 稳定 C ABI。
 *
 * 真实逻辑在 core/api；本文件只暴露跨语言稳定入口。语言绑定层只负责参数转换
 * 和返回值封装，不在 Lua/JS/Go 各自重复实现命令逻辑。
 */
const char* engine_version();

/**
 * 返回当前 native 能力边界的 JSON 描述。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* engine_capabilities_json();

/**
 * 返回给外部插件 so 使用的引擎函数表。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const EngineApi* engine_get_api();

/**
 * 输出普通脚本日志。
 *
 * 返回 1 表示成功，返回 0 表示失败。
 */
int runtime_print(const char* text);

/**
 * 输出日志模块文本。
 *
 * 当前和 runtime_print 同级别输出；保留独立入口方便后续区分 print 与 log。
 */
int runtime_log_print(const char* text);

/**
 * 不带中断检查的睡眠。
 *
 * 参数单位为毫秒。返回 1 表示完成，返回 0 表示失败。
 */
int runtime_sleep(int durationMs);

/**
 * 可中断睡眠。
 *
 * 参数单位为毫秒。shouldInterrupt 可为空；不为空时，睡眠过程中会定期调用它。
 */
int runtime_sleep_interruptible(
        int durationMs,
        runtime_interrupt_callback shouldInterrupt,
        void* userData
);

/**
 * 返回最近一次运行时 C ABI 失败原因。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* runtime_last_error();

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
 * - 0：失败，可通过 screen_last_error() 读取错误文本。
 *
 * 注意：
 * pixels 指向 libengine.so 内部缓存，调用方只读，不要释放。下一次缓存过期后
 * 重新截图、screen_clear_capture_cache 或脚本结束清理时，该地址可能失效。
 */
int screen_capture(int* width, int* height, unsigned char** pixels);

/**
 * 锁定当前截图帧。
 *
 * 开启后 screen_capture 会一直返回当前缓存帧；如果当前还没有缓存帧，
 * 下一次 screen_capture 会先截图并锁住这一帧。
 */
void screen_keep_capture();

/**
 * 取消锁帧，恢复按缓存时间判断是否重新截图。
 */
void screen_release_capture();

/**
 * 设置截图缓存时间，单位毫秒。
 *
 * 返回 1 表示设置成功，返回 0 表示参数非法。
 */
int screen_set_capture_cache_ms(int durationMs);

/**
 * 清空截图缓存，并恢复默认缓存时间和非锁帧状态。
 */
void screen_clear_capture_cache();

/**
 * 返回最近一次截图 C ABI 调用失败原因。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* screen_last_error();

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

#ifdef __cplusplus
}
#endif
