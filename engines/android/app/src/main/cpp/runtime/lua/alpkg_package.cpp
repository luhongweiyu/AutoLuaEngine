/**
 * 文件用途：实现 ALPKG ZIP 索引、manifest 校验和 Lua 字节码认证解密。
 */
#include "alpkg_package.h"

#include "../../engine/json_value.h"
#include "package/alpkg_crypto.h"

extern "C" {
#include "lua.h"
}

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

namespace {

constexpr uint32_t kZipLocalHeaderSignature = 0x04034B50;
constexpr uint32_t kZipCentralHeaderSignature = 0x02014B50;
constexpr uint32_t kZipEndSignature = 0x06054B50;
constexpr size_t kZipLocalHeaderSize = 30;
constexpr size_t kZipCentralHeaderSize = 46;
constexpr size_t kZipEndSize = 22;
constexpr size_t kZipMaxCommentSize = 0xFFFF;
constexpr size_t kMaxPackageFileBytes = 64U * 1024U * 1024U;

uint16_t readLe16(const unsigned char* bytes) {
    return static_cast<uint16_t>(bytes[0])
            | static_cast<uint16_t>(bytes[1]) << 8;
}

uint32_t readLe32(const unsigned char* bytes) {
    return static_cast<uint32_t>(bytes[0])
            | static_cast<uint32_t>(bytes[1]) << 8
            | static_cast<uint32_t>(bytes[2]) << 16
            | static_cast<uint32_t>(bytes[3]) << 24;
}

bool isSafeRelativePath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos
            || path.find('\0') != std::string::npos) {
        return false;
    }
    // Android 实际使用 POSIX 路径，但 manifest 可能来自 Windows 打包环境。显式拒绝
    // C:/... 这类盘符形式，避免它在“项目相对路径”语义中被误接受。
    if (path.size() >= 2 && path[1] == ':'
            && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))) {
        return false;
    }

    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        std::string segment = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return true;
}

bool readFileRange(
        const std::string& path,
        uint64_t offset,
        size_t size,
        std::vector<unsigned char>* output,
        std::string* error
) {
    if (output == nullptr || size > kMaxPackageFileBytes) {
        if (error != nullptr) {
            *error = "脚本包文件项过大";
        }
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error != nullptr) {
            *error = "无法读取脚本包";
        }
        return false;
    }
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        if (error != nullptr) {
            *error = "脚本包偏移无效";
        }
        return false;
    }

    output->assign(size, 0);
    if (size > 0) {
        input.read(reinterpret_cast<char*>(output->data()), static_cast<std::streamsize>(size));
        if (!input) {
            output->clear();
            if (error != nullptr) {
                *error = "脚本包数据不完整";
            }
            return false;
        }
    }
    return true;
}

bool base64Decode(const std::string& text, std::vector<unsigned char>* output) {
    if (output == nullptr || text.empty() || text.size() % 4 != 0) {
        return false;
    }

    auto valueOf = [](unsigned char character) -> int {
        if (character >= 'A' && character <= 'Z') {
            return character - 'A';
        }
        if (character >= 'a' && character <= 'z') {
            return character - 'a' + 26;
        }
        if (character >= '0' && character <= '9') {
            return character - '0' + 52;
        }
        if (character == '+') {
            return 62;
        }
        if (character == '/') {
            return 63;
        }
        return -1;
    };

    output->clear();
    output->reserve((text.size() / 4) * 3);
    for (size_t index = 0; index < text.size(); index += 4) {
        int first = valueOf(static_cast<unsigned char>(text[index]));
        int second = valueOf(static_cast<unsigned char>(text[index + 1]));
        bool thirdPadding = text[index + 2] == '=';
        bool fourthPadding = text[index + 3] == '=';
        int third = thirdPadding ? 0 : valueOf(static_cast<unsigned char>(text[index + 2]));
        int fourth = fourthPadding ? 0 : valueOf(static_cast<unsigned char>(text[index + 3]));
        bool isLastGroup = index + 4 == text.size();

        if (first < 0 || second < 0 || third < 0 || fourth < 0
                || (thirdPadding && !fourthPadding)
                || ((thirdPadding || fourthPadding) && !isLastGroup)) {
            output->clear();
            return false;
        }

        uint32_t value = static_cast<uint32_t>(first) << 18
                | static_cast<uint32_t>(second) << 12
                | static_cast<uint32_t>(third) << 6
                | static_cast<uint32_t>(fourth);
        output->push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
        if (!thirdPadding) {
            output->push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
        }
        if (!fourthPadding) {
            output->push_back(static_cast<unsigned char>(value & 0xFF));
        }
    }
    return true;
}

std::string nativeByteOrder() {
    const uint16_t value = 1;
    return *reinterpret_cast<const unsigned char*>(&value) == 1 ? "little" : "big";
}

bool checkManifestLuaConfig(const JsonValue& manifest, std::string* error) {
    const JsonValue* lua = manifest.get("lua");
    if (lua == nullptr || !lua->isObject()) {
        if (error != nullptr) {
            *error = "脚本包缺少 Lua 配置";
        }
        return false;
    }

    if (lua->stringOr("version") != "5.4.8"
            || lua->intOr("integerBytes", 0) != static_cast<int>(sizeof(lua_Integer))
            || lua->intOr("numberBytes", 0) != static_cast<int>(sizeof(lua_Number))
            || lua->stringOr("byteOrder") != nativeByteOrder()) {
        if (error != nullptr) {
            *error = "脚本包 Lua 字节码与当前引擎不兼容";
        }
        return false;
    }
    return true;
}

} // namespace

std::shared_ptr<AlpkgPackage> AlpkgPackage::open(const std::string& packagePath, std::string* error) {
    std::shared_ptr<AlpkgPackage> package(new AlpkgPackage());
    if (!package->load(packagePath, error)) {
        return nullptr;
    }
    return package;
}

const std::string& AlpkgPackage::packagePath() const {
    return packagePath_;
}

const std::string& AlpkgPackage::entryPath() const {
    return entryPath_;
}

bool AlpkgPackage::hasLuaFile(const std::string& relativePath) const {
    return luaEntries_.find(relativePath) != luaEntries_.end();
}

bool AlpkgPackage::load(const std::string& packagePath, std::string* error) {
    std::ifstream input(packagePath, std::ios::binary | std::ios::ate);
    if (!input) {
        if (error != nullptr) {
            *error = "无法打开脚本包";
        }
        return false;
    }

    std::streamoff size = input.tellg();
    if (size < static_cast<std::streamoff>(kZipEndSize)
            || static_cast<uint64_t>(size) > std::numeric_limits<uint32_t>::max()) {
        if (error != nullptr) {
            *error = "脚本包不是受支持的 ZIP 文件";
        }
        return false;
    }
    input.close();

    size_t tailSize = static_cast<size_t>(std::min<std::streamoff>(
            size,
            static_cast<std::streamoff>(kZipEndSize + kZipMaxCommentSize)
    ));
    std::vector<unsigned char> tail;
    if (!readFileRange(packagePath, static_cast<uint64_t>(size) - tailSize, tailSize, &tail, error)) {
        return false;
    }

    size_t endOffset = std::string::npos;
    for (size_t index = tail.size() - kZipEndSize + 1; index-- > 0;) {
        if (readLe32(tail.data() + index) == kZipEndSignature) {
            endOffset = index;
            break;
        }
    }
    if (endOffset == std::string::npos || endOffset + kZipEndSize > tail.size()) {
        if (error != nullptr) {
            *error = "脚本包缺少 ZIP 目录";
        }
        return false;
    }

    const unsigned char* end = tail.data() + endOffset;
    uint16_t entryCount = readLe16(end + 10);
    uint32_t centralSize = readLe32(end + 12);
    uint32_t centralOffset = readLe32(end + 16);
    uint16_t commentSize = readLe16(end + 20);
    if (endOffset + kZipEndSize + commentSize != tail.size()
            || static_cast<uint64_t>(centralOffset) + centralSize > static_cast<uint64_t>(size)) {
        if (error != nullptr) {
            *error = "脚本包 ZIP 目录损坏";
        }
        return false;
    }

    std::vector<unsigned char> centralDirectory;
    if (!readFileRange(packagePath, centralOffset, centralSize, &centralDirectory, error)) {
        return false;
    }

    zipEntries_.clear();
    size_t offset = 0;
    for (uint16_t count = 0; count < entryCount; ++count) {
        if (offset + kZipCentralHeaderSize > centralDirectory.size()
                || readLe32(centralDirectory.data() + offset) != kZipCentralHeaderSignature) {
            if (error != nullptr) {
                *error = "脚本包 ZIP 文件项无效";
            }
            return false;
        }

        const unsigned char* header = centralDirectory.data() + offset;
        uint16_t flags = readLe16(header + 8);
        uint16_t compressionMethod = readLe16(header + 10);
        uint32_t compressedSize = readLe32(header + 20);
        uint32_t uncompressedSize = readLe32(header + 24);
        uint16_t nameSize = readLe16(header + 28);
        uint16_t extraSize = readLe16(header + 30);
        uint16_t entryCommentSize = readLe16(header + 32);
        uint32_t localHeaderOffset = readLe32(header + 42);
        size_t nextOffset = offset + kZipCentralHeaderSize + nameSize + extraSize + entryCommentSize;
        if (nextOffset > centralDirectory.size() || compressionMethod != 0 || compressedSize != uncompressedSize
                || (flags & 0x0001) != 0 || (flags & 0x0008) != 0) {
            if (error != nullptr) {
                *error = "脚本包必须使用未压缩且未加密的 ZIP 文件项";
            }
            return false;
        }

        std::string name(reinterpret_cast<const char*>(header + kZipCentralHeaderSize), nameSize);
        if (!isSafeRelativePath(name) || zipEntries_.find(name) != zipEntries_.end()) {
            if (error != nullptr) {
                *error = "脚本包包含无效或重复路径";
            }
            return false;
        }

        zipEntries_.emplace(name, ZipEntry{
                localHeaderOffset,
                compressedSize,
                uncompressedSize,
                compressionMethod,
                flags
        });
        offset = nextOffset;
    }
    if (offset != centralDirectory.size()) {
        if (error != nullptr) {
            *error = "脚本包 ZIP 目录长度无效";
        }
        return false;
    }

    std::vector<unsigned char> manifest;
    packagePath_ = packagePath;
    if (!readZipEntry("manifest.json", &manifest, error) || !parseManifest(manifest, error)) {
        packagePath_.clear();
        return false;
    }
    return true;
}

bool AlpkgPackage::readZipEntry(
        const std::string& archivePath,
        std::vector<unsigned char>* data,
        std::string* error
) const {
    auto iterator = zipEntries_.find(archivePath);
    if (iterator == zipEntries_.end()) {
        if (error != nullptr) {
            *error = "脚本包中找不到文件：" + archivePath;
        }
        return false;
    }
    const ZipEntry& entry = iterator->second;
    std::vector<unsigned char> localHeader;
    if (!readFileRange(packagePath_, entry.localHeaderOffset, kZipLocalHeaderSize, &localHeader, error)) {
        return false;
    }
    if (readLe32(localHeader.data()) != kZipLocalHeaderSignature
            || readLe16(localHeader.data() + 8) != entry.compressionMethod
            || readLe16(localHeader.data() + 6) != entry.flags) {
        if (error != nullptr) {
            *error = "脚本包本地 ZIP 头无效";
        }
        return false;
    }

    uint16_t nameSize = readLe16(localHeader.data() + 26);
    uint16_t extraSize = readLe16(localHeader.data() + 28);
    uint64_t dataOffset = static_cast<uint64_t>(entry.localHeaderOffset)
            + kZipLocalHeaderSize + nameSize + extraSize;
    return readFileRange(packagePath_, dataOffset, entry.uncompressedSize, data, error);
}

bool AlpkgPackage::parseManifest(const std::vector<unsigned char>& manifestBytes, std::string* error) {
    JsonValue manifest;
    std::string parseError;
    std::string manifestText(reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size());
    if (!parseJsonText(manifestText, &manifest, &parseError) || !manifest.isObject()) {
        if (error != nullptr) {
            *error = "脚本包 manifest 无效：" + parseError;
        }
        return false;
    }
    if (manifest.stringOr("format") != "alpkg" || manifest.intOr("formatVersion", 0) != 1) {
        if (error != nullptr) {
            *error = "脚本包格式不受支持";
        }
        return false;
    }
    if (!checkManifestLuaConfig(manifest, error)) {
        return false;
    }

    std::string entry = manifest.stringOr("entry");
    const JsonValue* files = manifest.get("files");
    if (!isSafeRelativePath(entry) || files == nullptr || !files->isObject()) {
        if (error != nullptr) {
            *error = "脚本包入口或文件索引无效";
        }
        return false;
    }

    luaEntries_.clear();
    resourceEntries_.clear();
    for (const auto& file : files->objectValue()) {
        const std::string& relativePath = file.first;
        const JsonValue& metadata = file.second;
        if (!isSafeRelativePath(relativePath) || !metadata.isObject()) {
            if (error != nullptr) {
                *error = "脚本包文件索引无效";
            }
            return false;
        }
        const std::string kind = metadata.stringOr("kind");
        if (kind == "lua") {
            LuaEntry luaEntry;
            luaEntry.archivePath = metadata.stringOr("path");
            std::vector<unsigned char> nonce;
            std::vector<unsigned char> tag;
            if (!isSafeRelativePath(luaEntry.archivePath)
                    || zipEntries_.find(luaEntry.archivePath) == zipEntries_.end()
                    || !base64Decode(metadata.stringOr("nonce"), &nonce)
                    || !base64Decode(metadata.stringOr("tag"), &tag)
                    || nonce.size() != luaEntry.nonce.size()
                    || tag.size() != luaEntry.tag.size()) {
                if (error != nullptr) {
                    *error = "脚本包 Lua 加密信息无效：" + relativePath;
                }
                return false;
            }
            std::copy(nonce.begin(), nonce.end(), luaEntry.nonce.begin());
            std::copy(tag.begin(), tag.end(), luaEntry.tag.begin());
            luaEntries_.emplace(relativePath, std::move(luaEntry));
            continue;
        }

        if (kind == "resource") {
            ResourceEntry resourceEntry;
            resourceEntry.archivePath = metadata.stringOr("path");
            // 打包器会把资源保留在原相对路径。强制两者相等，避免 manifest 把内部
            // code/*.luac.enc 条目标记为 resource 后被脚本直接读取。
            if (resourceEntry.archivePath != relativePath
                    || !isSafeRelativePath(resourceEntry.archivePath)
                    || zipEntries_.find(resourceEntry.archivePath) == zipEntries_.end()) {
                if (error != nullptr) {
                    *error = "脚本包资源索引无效：" + relativePath;
                }
                return false;
            }
            resourceEntries_.emplace(relativePath, std::move(resourceEntry));
            continue;
        }

        if (error != nullptr) {
            *error = "脚本包包含不支持的文件类型：" + relativePath;
        }
        return false;
    }

    // 资源索引的路径不得指向任何 Lua 密文。该检查必须在所有条目收集后执行，因为
    // manifest 的字典顺序可能让 resource 先于 Lua 条目被解析。
    for (const auto& resource : resourceEntries_) {
        for (const auto& lua : luaEntries_) {
            if (resource.second.archivePath == lua.second.archivePath) {
                if (error != nullptr) {
                    *error = "脚本包资源不能引用 Lua 加密字节码：" + resource.first;
                }
                return false;
            }
        }
    }

    if (luaEntries_.find(entry) == luaEntries_.end()) {
        if (error != nullptr) {
            *error = "脚本包入口不是 Lua 文件";
        }
        return false;
    }
    entryPath_ = entry;
    return true;
}

bool AlpkgPackage::loadLuaFile(
        const std::string& relativePath,
        std::vector<unsigned char>* bytecode,
        std::string* error
) const {
    if (bytecode == nullptr) {
        if (error != nullptr) {
            *error = "Lua 字节码输出为空";
        }
        return false;
    }
    auto iterator = luaEntries_.find(relativePath);
    if (iterator == luaEntries_.end()) {
        if (error != nullptr) {
            *error = "脚本包中找不到 Lua 文件：" + relativePath;
        }
        return false;
    }

    std::vector<unsigned char> cipherText;
    if (!readZipEntry(iterator->second.archivePath, &cipherText, error)) {
        return false;
    }
    bytecode->assign(cipherText.size(), 0);
    int decryptResult = alpkg_decrypt_lua(
            bytecode->data(),
            iterator->second.tag.data(),
            iterator->second.nonce.data(),
            relativePath.data(),
            relativePath.size(),
            cipherText.data(),
            cipherText.size()
    );
    alpkg_wipe(cipherText.data(), cipherText.size());
    if (decryptResult != 0) {
        alpkg_wipe(bytecode->data(), bytecode->size());
        bytecode->clear();
        if (error != nullptr) {
            *error = "Lua 字节码认证失败：" + relativePath;
        }
        return false;
    }
    return true;
}

bool AlpkgPackage::readResourceFile(
        const std::string& relativePath,
        std::vector<unsigned char>* data,
        std::string* error
) const {
    if (data == nullptr) {
        if (error != nullptr) {
            *error = "脚本包资源输出缓冲为空";
        }
        return false;
    }
    data->clear();

    if (!isSafeRelativePath(relativePath)) {
        if (error != nullptr) {
            *error = "脚本包资源路径无效：" + relativePath;
        }
        return false;
    }

    auto iterator = resourceEntries_.find(relativePath);
    if (iterator == resourceEntries_.end()) {
        if (error != nullptr) {
            *error = "脚本包中找不到资源文件：" + relativePath;
        }
        return false;
    }

    return readZipEntry(iterator->second.archivePath, data, error);
}
