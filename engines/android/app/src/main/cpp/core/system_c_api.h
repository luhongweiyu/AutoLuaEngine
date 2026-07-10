/**
 * 文件用途：声明稳定 C ABI 入口，方便后续 Lua ffi、JS 插件或外部 so 调用。
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * AutoLuaEngine native 系统能力 C ABI。
 *
 * 该接口面向 Lua FFI、JS native binding、Go 插件或其他 so 复用。
 * C ABI 只暴露稳定、简单的基础类型，不返回 C++ 对象。
 */
const char* engine_version();

/**
 * 返回当前 native 系统能力边界的 JSON 描述。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* engine_capabilities_json();

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
 * 返回最近一次 C ABI 调用失败原因。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* screen_last_error();

#ifdef __cplusplus
}
#endif
