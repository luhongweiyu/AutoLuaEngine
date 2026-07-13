/*
 * 文件用途：实现 ALPKG Lua 字节码加密、认证和单文件密钥派生。
 */
#include "alpkg_crypto.h"

#include "../third_party/monocypher/monocypher.h"

#include <string.h>

/*
 * 第一版离线包的产品根密钥。它会编译进打包器和 libengine.so，因此只能提高静态提取成本。
 * 后续授权模式改由服务器提供运行期根密钥，而无需修改包的加密布局。
 */
static const uint8_t kAlpkgOfflineRootKey[ALPKG_KEY_BYTES] = {
        0xA7, 0x31, 0xDE, 0x49, 0x08, 0xC2, 0x75, 0xF6,
        0x5B, 0x10, 0x9D, 0xE4, 0x3C, 0x88, 0x27, 0xB1,
        0x6E, 0xFA, 0x42, 0x0D, 0x95, 0x63, 0xBC, 0x17,
        0xD8, 0x54, 0x2A, 0xF0, 0x71, 0xCE, 0x36, 0x9B
};

void alpkg_derive_file_key(
        uint8_t output_key[ALPKG_KEY_BYTES],
        const char* relative_path,
        size_t relative_path_size
) {
    crypto_blake2b_keyed(
            output_key,
            ALPKG_KEY_BYTES,
            kAlpkgOfflineRootKey,
            ALPKG_KEY_BYTES,
            (const uint8_t*) relative_path,
            relative_path_size
    );
}

int alpkg_encrypt_lua(
        uint8_t* cipher_text,
        uint8_t tag[ALPKG_TAG_BYTES],
        const uint8_t nonce[ALPKG_NONCE_BYTES],
        const char* relative_path,
        size_t relative_path_size,
        const uint8_t* plain_text,
        size_t plain_text_size
) {
    uint8_t key[ALPKG_KEY_BYTES];
    alpkg_derive_file_key(key, relative_path, relative_path_size);
    crypto_aead_lock(
            cipher_text,
            tag,
            key,
            nonce,
            (const uint8_t*) relative_path,
            relative_path_size,
            plain_text,
            plain_text_size
    );
    crypto_wipe(key, sizeof(key));
    return 0;
}

int alpkg_decrypt_lua(
        uint8_t* plain_text,
        const uint8_t tag[ALPKG_TAG_BYTES],
        const uint8_t nonce[ALPKG_NONCE_BYTES],
        const char* relative_path,
        size_t relative_path_size,
        const uint8_t* cipher_text,
        size_t cipher_text_size
) {
    uint8_t key[ALPKG_KEY_BYTES];
    alpkg_derive_file_key(key, relative_path, relative_path_size);
    int result = crypto_aead_unlock(
            plain_text,
            tag,
            key,
            nonce,
            (const uint8_t*) relative_path,
            relative_path_size,
            cipher_text,
            cipher_text_size
    );
    crypto_wipe(key, sizeof(key));
    return result;
}

void alpkg_wipe(void* memory, size_t size) {
    if (memory != NULL && size > 0) {
        crypto_wipe(memory, size);
    }
}
