/**
 * 文件用途：声明 ALPKG ZIP 脚本包读取器，为 Lua 运行时提供加密字节码和包内资源索引。
 */
#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

/**
 * ALPKG 包读取器。
 *
 * 第一版只接受由 tools/pack/xiaoyv_pack.exe 生成的“存储模式” ZIP 项，因此不需要把
 * ZIP 解压库带进 libengine.so。Lua 文件在读取后验证 XChaCha20-Poly1305 标签并直接
 * 交给 Lua VM；资源文件既可由 App 主进程的 ScriptPackageResources 提供给 WebView，
 * 也可由 core/api 通过稳定 C ABI 返回给 Lua、JS、Go 等语言绑定。
 */
class AlpkgPackage {
public:
    static std::shared_ptr<AlpkgPackage> open(const std::string& packagePath, std::string* error);

    const std::string& packagePath() const;
    const std::string& entryPath() const;

    /** 判断相对路径是否是包内 Lua 文件。 */
    bool hasLuaFile(const std::string& relativePath) const;

    /**
     * 读取、认证并解密一个包内 Lua 文件。
     *
     * 成功时 bytecode 只在调用方内存中存在，调用方负责在 lua_load 后调用 alpkg_wipe。
     */
    bool loadLuaFile(
            const std::string& relativePath,
            std::vector<unsigned char>* bytecode,
            std::string* error
    ) const;

    /**
     * 读取 manifest 中声明为 resource 的包内原始文件。
     *
     * 该接口只接受项目相对路径，不接受 Lua 条目、绝对路径或路径穿越。读取结果只存在
     * 调用方提供的内存中，不会解压到缓存或共享存储。调用方可把字节交给任意语言运行时，
     * 因而不把 WebView 的资源读取机制当作通用文件访问入口。
     */
    bool readResourceFile(
            const std::string& relativePath,
            std::vector<unsigned char>* data,
            std::string* error
    ) const;

private:
    struct ZipEntry {
        uint32_t localHeaderOffset = 0;
        uint32_t compressedSize = 0;
        uint32_t uncompressedSize = 0;
        uint16_t compressionMethod = 0;
        uint16_t flags = 0;
    };

    struct LuaEntry {
        std::string archivePath;
        std::array<unsigned char, 24> nonce{};
        std::array<unsigned char, 16> tag{};
    };

    /**
     * 资源条目保持项目相对路径与 ZIP 条目路径一致，避免通过伪造 manifest 把加密 Lua
     * 字节码或其他内部条目伪装成普通资源暴露给语言绑定。
     */
    struct ResourceEntry {
        std::string archivePath;
    };

    bool load(const std::string& packagePath, std::string* error);
    bool readZipEntry(const std::string& archivePath, std::vector<unsigned char>* data, std::string* error) const;
    bool parseManifest(const std::vector<unsigned char>& manifestBytes, std::string* error);

    std::string packagePath_;
    std::string entryPath_;
    std::map<std::string, ZipEntry> zipEntries_;
    std::map<std::string, LuaEntry> luaEntries_;
    std::map<std::string, ResourceEntry> resourceEntries_;
};
