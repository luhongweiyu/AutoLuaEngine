/*
 * 文件用途：定义 ALPKG Lua 字节码的 XChaCha20-Poly1305 加密格式与公共常量。
 *
 * 此头文件同时被 Windows 打包器和 Android libengine.so 使用，确保 nonce、认证标签、
 * 关联数据和派生密钥规则完全一致。它不包含任何平台 API。
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALPKG_KEY_BYTES 32
#define ALPKG_NONCE_BYTES 24
#define ALPKG_TAG_BYTES 16

/*
 * 基于固定产品密钥和包内文件相对路径派生单文件密钥。
 *
 * 第一版是离线通用包：该机制用于避免包内所有 Lua 共用同一密钥，并防止随意互换密文。
 * 它不是服务器授权替代品；联网授权阶段将保持函数接口不变，仅改为传入服务器下发密钥。
 */
void alpkg_derive_file_key(
        uint8_t output_key[ALPKG_KEY_BYTES],
        const char* relative_path,
        size_t relative_path_size
);

/* 使用相对路径作为认证关联数据加密 Lua 字节码。返回 0 表示成功。 */
int alpkg_encrypt_lua(
        uint8_t* cipher_text,
        uint8_t tag[ALPKG_TAG_BYTES],
        const uint8_t nonce[ALPKG_NONCE_BYTES],
        const char* relative_path,
        size_t relative_path_size,
        const uint8_t* plain_text,
        size_t plain_text_size
);

/* 验证认证标签并解密 Lua 字节码。返回 0 表示成功，-1 表示密文已损坏或密钥不匹配。 */
int alpkg_decrypt_lua(
        uint8_t* plain_text,
        const uint8_t tag[ALPKG_TAG_BYTES],
        const uint8_t nonce[ALPKG_NONCE_BYTES],
        const char* relative_path,
        size_t relative_path_size,
        const uint8_t* cipher_text,
        size_t cipher_text_size
);

/* 清零密钥和已使用的明文内存。 */
void alpkg_wipe(void* memory, size_t size);

#ifdef __cplusplus
}
#endif
