#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * AutoLuaEngine native 系统能力 C ABI。
 *
 * 该接口面向后续 Lua FFI、JS native binding 和插件进程复用。第一版只导出
 * 版本与能力描述，具体系统能力仍先通过 HostApi/JSON-RPC 使用，避免过早承诺
 * 不稳定的二进制参数结构。
 */
const char* ael_system_version();

/**
 * 返回当前 native 系统能力边界的 JSON 描述。
 *
 * 返回指针由 libengine.so 内部持有，调用方只读，不要释放。
 */
const char* ael_system_capabilities_json();

#ifdef __cplusplus
}
#endif
