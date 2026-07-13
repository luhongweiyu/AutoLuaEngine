/*
 * 文件用途：实现 ALPKG Windows 命令行打包器，递归读取项目、编译 Lua 字节码并输出 ZIP 脚本包。
 */
#include "alpkg_crypto.h"
#include "json_value.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kPackageExtension = ".alpkg";
constexpr const char* kPackageConfigFileName = "alpkg.json";
constexpr const char* kManifestPath = "manifest.json";
constexpr const char* kLuaArchiveDirectory = "code/";
constexpr uint32_t kZipLocalHeaderSignature = 0x04034B50;
constexpr uint32_t kZipCentralHeaderSignature = 0x02014B50;
constexpr uint32_t kZipEndSignature = 0x06054B50;
constexpr uint16_t kZipUtf8Flag = 0x0800;

// 新项目首次打包时自动写入的固定配置。入口保持最常见的 main.lua，排除规则避免
// 将 IDE 元数据、Git 目录和上一次打包输出递归写进新的 .alpkg。
constexpr const char* kDefaultPackageConfigText =
        "{\n"
        "  \"entry\": \"main.lua\",\n"
        "  \"exclude\": [\".git/\", \".vscode/\", \"dist/\", \"*.bak\"]\n"
        "}\n";

struct PackageConfig {
    std::string entry;
    std::vector<std::string> excludeRules;
};

struct ArchiveEntry {
    std::string path;
    std::vector<uint8_t> data;
};

struct LuaFileMetadata {
    std::string archivePath;
    std::array<uint8_t, ALPKG_NONCE_BYTES> nonce{};
    std::array<uint8_t, ALPKG_TAG_BYTES> tag{};
};

struct CentralDirectoryEntry {
    std::string path;
    uint32_t crc32 = 0;
    uint32_t size = 0;
    uint32_t offset = 0;
};

std::string normalizePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.rfind("./", 0) == 0) {
        path.erase(0, 2);
    }
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }
    return path;
}

/**
 * Windows 的 std::filesystem::u8string 在部分 MinGW 代码页环境会抛异常。
 * ZIP 和 manifest 需要稳定 UTF-8，因此在这里直接把宽路径转换为 UTF-8。
 */
std::string pathToUtf8(const fs::path& path) {
#ifdef _WIN32
    std::wstring value = path.native();
    if (value.empty()) {
        return "";
    }
    int size = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            nullptr, 0, nullptr, nullptr
    );
    if (size <= 0) {
        throw std::runtime_error("无法转换 Windows 路径为 UTF-8");
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), size, nullptr, nullptr
    );
    return result;
#else
    return path.string();
#endif
}

/** 把配置文件中的 UTF-8 相对路径转为 Windows 原生宽路径。 */
fs::path utf8ToPath(const std::string& value) {
#ifdef _WIN32
    if (value.empty()) {
        return fs::path();
    }
    int size = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            nullptr, 0
    );
    if (size <= 0) {
        throw std::runtime_error("配置路径不是有效 UTF-8");
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
            result.data(), size
    );
    return fs::path(result);
#else
    return fs::path(value);
#endif
}

std::string fileNameFromRelativePath(const std::string& path) {
    size_t separator = path.find_last_of('/');
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

bool hasSuffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size()
            && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool wildcardMatch(const std::string& pattern, const std::string& value) {
    size_t patternIndex = 0;
    size_t valueIndex = 0;
    size_t starIndex = std::string::npos;
    size_t resumeIndex = 0;

    while (valueIndex < value.size()) {
        if (patternIndex < pattern.size()
                && (pattern[patternIndex] == '?' || pattern[patternIndex] == value[valueIndex])) {
            ++patternIndex;
            ++valueIndex;
        } else if (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
            starIndex = patternIndex++;
            resumeIndex = valueIndex;
        } else if (starIndex != std::string::npos) {
            patternIndex = starIndex + 1;
            valueIndex = ++resumeIndex;
        } else {
            return false;
        }
    }

    while (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
        ++patternIndex;
    }
    return patternIndex == pattern.size();
}

bool isExcluded(const std::string& relativePath, const std::vector<std::string>& rules) {
    std::string fileName = fileNameFromRelativePath(relativePath);
    for (std::string rule : rules) {
        rule = normalizePath(rule);
        if (rule.empty()) {
            continue;
        }

        if (hasSuffix(rule, "/**")) {
            std::string directory = rule.substr(0, rule.size() - 2);
            if (startsWith(relativePath, directory)) {
                return true;
            }
            continue;
        }

        if (rule.back() == '/') {
            if (startsWith(relativePath, rule)) {
                return true;
            }
            continue;
        }

        if (wildcardMatch(rule, relativePath) || wildcardMatch(rule, fileName)) {
            return true;
        }
    }
    return false;
}

std::vector<uint8_t> readBinaryFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("无法读取文件：" + pathToUtf8(path));
    }

    input.seekg(0, std::ios::end);
    std::streamoff size = input.tellg();
    if (size < 0) {
        throw std::runtime_error("无法读取文件长度：" + pathToUtf8(path));
    }
    input.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), size);
        if (!input) {
            throw std::runtime_error("读取文件失败：" + pathToUtf8(path));
        }
    }
    return data;
}

std::string readTextFile(const fs::path& path) {
    std::vector<uint8_t> data = readBinaryFile(path);
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

void writeUint16(std::ofstream& output, uint16_t value) {
    const uint8_t bytes[] = {
            static_cast<uint8_t>(value & 0xFF),
            static_cast<uint8_t>((value >> 8) & 0xFF)
    };
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void writeUint32(std::ofstream& output, uint32_t value) {
    const uint8_t bytes[] = {
            static_cast<uint8_t>(value & 0xFF),
            static_cast<uint8_t>((value >> 8) & 0xFF),
            static_cast<uint8_t>((value >> 16) & 0xFF),
            static_cast<uint8_t>((value >> 24) & 0xFF)
    };
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

uint32_t crc32(const uint8_t* data, size_t size) {
    static uint32_t table[256]{};
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t index = 0; index < 256; ++index) {
            uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1) != 0 ? (value >> 1) ^ 0xEDB88320U : value >> 1;
            }
            table[index] = value;
        }
        initialized = true;
    }

    uint32_t value = 0xFFFFFFFFU;
    for (size_t index = 0; index < size; ++index) {
        value = table[(value ^ data[index]) & 0xFFU] ^ (value >> 8);
    }
    return value ^ 0xFFFFFFFFU;
}

std::string base64Encode(const uint8_t* data, size_t size) {
    static constexpr char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((size + 2) / 3) * 4);

    for (size_t index = 0; index < size; index += 3) {
        uint32_t value = static_cast<uint32_t>(data[index]) << 16;
        bool hasSecond = index + 1 < size;
        bool hasThird = index + 2 < size;
        if (hasSecond) {
            value |= static_cast<uint32_t>(data[index + 1]) << 8;
        }
        if (hasThird) {
            value |= static_cast<uint32_t>(data[index + 2]);
        }

        output.push_back(alphabet[(value >> 18) & 0x3F]);
        output.push_back(alphabet[(value >> 12) & 0x3F]);
        output.push_back(hasSecond ? alphabet[(value >> 6) & 0x3F] : '=');
        output.push_back(hasThird ? alphabet[value & 0x3F] : '=');
    }
    return output;
}

bool randomBytes(uint8_t* target, size_t size) {
    if (target == nullptr || size > static_cast<size_t>(ULONG_MAX)) {
        return false;
    }
    return BCryptGenRandom(
            nullptr,
            target,
            static_cast<ULONG>(size),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG
    ) == 0;
}

int luaVectorWriter(lua_State* state, const void* data, size_t size, void* context) {
    (void) state;
    auto* output = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const uint8_t*>(data);
    output->insert(output->end(), bytes, bytes + size);
    return 0;
}

std::vector<uint8_t> compileLuaBytecode(
        const std::vector<uint8_t>& source,
        const std::string& relativePath
) {
    lua_State* state = luaL_newstate();
    if (state == nullptr) {
        throw std::runtime_error("Lua 编译器初始化失败");
    }

    std::string chunkName = "@" + relativePath;
    int loadStatus = luaL_loadbufferx(
            state,
            reinterpret_cast<const char*>(source.data()),
            source.size(),
            chunkName.c_str(),
            "t"
    );
    if (loadStatus != LUA_OK) {
        const char* error = lua_tostring(state, -1);
        std::string message = error == nullptr ? "Lua 源码无效" : error;
        lua_close(state);
        throw std::runtime_error("Lua 编译失败（" + relativePath + "）：" + message);
    }

    std::vector<uint8_t> bytecode;
    int dumpStatus = lua_dump(state, luaVectorWriter, &bytecode, 1);
    lua_close(state);
    if (dumpStatus != 0 || bytecode.empty()) {
        throw std::runtime_error("Lua 字节码生成失败：" + relativePath);
    }
    return bytecode;
}

void createDefaultPackageConfig(const fs::path& configPath) {
    std::ofstream output(configPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("无法创建默认配置：" + pathToUtf8(configPath));
    }

    output.write(kDefaultPackageConfigText, static_cast<std::streamsize>(std::strlen(kDefaultPackageConfigText)));
    output.close();
    if (!output) {
        throw std::runtime_error("写入默认配置失败：" + pathToUtf8(configPath));
    }

    std::cout << "已创建默认配置：" << pathToUtf8(configPath) << "\n";
}

PackageConfig readPackageConfig(const fs::path& projectDirectory) {
    fs::path configPath = projectDirectory / kPackageConfigFileName;
    if (!fs::exists(configPath)) {
        createDefaultPackageConfig(configPath);
    }
    if (!fs::is_regular_file(configPath)) {
        throw std::runtime_error("项目根目录的 alpkg.json 不是普通文件");
    }

    JsonValue config;
    std::string parseError;
    if (!parseJsonText(readTextFile(configPath), &config, &parseError) || !config.isObject()) {
        throw std::runtime_error("alpkg.json 格式无效：" + parseError);
    }

    const JsonValue* entryValue = config.get("entry");
    if (entryValue == nullptr || !entryValue->isString() || entryValue->stringValue().empty()) {
        throw std::runtime_error("alpkg.json 必须设置 entry");
    }

    PackageConfig result;
    result.entry = normalizePath(entryValue->stringValue());
    if (result.entry.empty() || result.entry.find("..") != std::string::npos
            || !hasSuffix(result.entry, ".lua")) {
        throw std::runtime_error("entry 必须是项目内的 .lua 相对路径");
    }

    const JsonValue* excludeValue = config.get("exclude");
    if (excludeValue == nullptr) {
        return result;
    }
    if (!excludeValue->isArray()) {
        throw std::runtime_error("exclude 必须是字符串数组");
    }
    for (const JsonValue& item : excludeValue->arrayValue()) {
        if (!item.isString()) {
            throw std::runtime_error("exclude 只支持字符串规则");
        }
        result.excludeRules.push_back(normalizePath(item.stringValue()));
    }
    return result;
}

fs::path resolveOutputPath(
        const fs::path& projectDirectory,
        const fs::path* requestedOutputPath
) {
    if (requestedOutputPath != nullptr && !requestedOutputPath->empty()) {
        return requestedOutputPath->is_absolute()
                ? *requestedOutputPath
                : projectDirectory / *requestedOutputPath;
    }

    std::string projectName = pathToUtf8(projectDirectory.filename());
    if (projectName.empty()) {
        projectName = "脚本包";
    }
    return projectDirectory / "dist" / (projectName + kPackageExtension);
}

std::string relativeProjectPath(const fs::path& projectDirectory, const fs::path& file) {
    std::wstring root = projectDirectory.native();
    std::wstring fullPath = file.native();
    if (fullPath.size() < root.size()
            || fullPath.compare(0, root.size(), root) != 0) {
        throw std::runtime_error("文件不在项目目录内");
    }
    size_t offset = root.size();
    while (offset < fullPath.size() && (fullPath[offset] == L'\\' || fullPath[offset] == L'/')) {
        ++offset;
    }
    return normalizePath(pathToUtf8(fs::path(fullPath.substr(offset))));
}

std::string luaArchivePath(const std::string& relativeLuaPath) {
    std::string path = relativeLuaPath;
    path.erase(path.size() - 4);
    return std::string(kLuaArchiveDirectory) + path + ".luac.enc";
}

std::string byteOrderName() {
    const uint16_t value = 1;
    return *reinterpret_cast<const uint8_t*>(&value) == 1 ? "little" : "big";
}

JsonValue makeLuaManifestItem(const LuaFileMetadata& metadata) {
    std::map<std::string, JsonValue> item;
    item["kind"] = JsonValue::makeString("lua");
    item["path"] = JsonValue::makeString(metadata.archivePath);
    item["nonce"] = JsonValue::makeString(base64Encode(metadata.nonce.data(), metadata.nonce.size()));
    item["tag"] = JsonValue::makeString(base64Encode(metadata.tag.data(), metadata.tag.size()));
    return JsonValue::makeObject(std::move(item));
}

JsonValue makeResourceManifestItem(const std::string& archivePath) {
    std::map<std::string, JsonValue> item;
    item["kind"] = JsonValue::makeString("resource");
    item["path"] = JsonValue::makeString(archivePath);
    return JsonValue::makeObject(std::move(item));
}

std::string makeManifest(
        const PackageConfig& config,
        const std::map<std::string, LuaFileMetadata>& luaFiles,
        const std::vector<std::string>& resourceFiles
) {
    std::map<std::string, JsonValue> files;
    for (const auto& item : luaFiles) {
        files[item.first] = makeLuaManifestItem(item.second);
    }
    for (const std::string& path : resourceFiles) {
        files[path] = makeResourceManifestItem(path);
    }

    std::map<std::string, JsonValue> lua;
    lua["version"] = JsonValue::makeString("5.4.8");
    lua["integerBytes"] = JsonValue::makeNumber(sizeof(lua_Integer));
    lua["numberBytes"] = JsonValue::makeNumber(sizeof(lua_Number));
    lua["byteOrder"] = JsonValue::makeString(byteOrderName());

    std::map<std::string, JsonValue> manifest;
    manifest["format"] = JsonValue::makeString("alpkg");
    manifest["formatVersion"] = JsonValue::makeNumber(1);
    manifest["lua"] = JsonValue::makeObject(std::move(lua));
    manifest["entry"] = JsonValue::makeString(config.entry);
    manifest["files"] = JsonValue::makeObject(std::move(files));
    return jsonValueToString(JsonValue::makeObject(std::move(manifest)));
}

void writeZip(const fs::path& outputPath, const std::vector<ArchiveEntry>& entries) {
    if (entries.size() > 0xFFFF) {
        throw std::runtime_error("文件数量超过 ZIP 第一版限制");
    }

    fs::create_directories(outputPath.parent_path());
    fs::path temporaryPath = outputPath;
    temporaryPath += ".tmp";

    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("无法创建输出文件：" + pathToUtf8(temporaryPath));
    }

    std::vector<CentralDirectoryEntry> centralEntries;
    centralEntries.reserve(entries.size());
    for (const ArchiveEntry& entry : entries) {
        if (entry.path.empty() || entry.path.size() > 0xFFFF || entry.data.size() > 0xFFFFFFFFULL) {
            throw std::runtime_error("ZIP 文件项过大或路径无效：" + entry.path);
        }

        std::streamoff currentOffset = output.tellp();
        if (currentOffset < 0 || static_cast<uint64_t>(currentOffset) > 0xFFFFFFFFULL) {
            throw std::runtime_error("ZIP 文件过大，超出第一版限制");
        }

        uint32_t entryCrc = crc32(entry.data.data(), entry.data.size());
        uint32_t entrySize = static_cast<uint32_t>(entry.data.size());
        writeUint32(output, kZipLocalHeaderSignature);
        writeUint16(output, 20);
        writeUint16(output, kZipUtf8Flag);
        writeUint16(output, 0);
        writeUint16(output, 0);
        writeUint16(output, 0);
        writeUint32(output, entryCrc);
        writeUint32(output, entrySize);
        writeUint32(output, entrySize);
        writeUint16(output, static_cast<uint16_t>(entry.path.size()));
        writeUint16(output, 0);
        output.write(entry.path.data(), static_cast<std::streamsize>(entry.path.size()));
        if (!entry.data.empty()) {
            output.write(reinterpret_cast<const char*>(entry.data.data()),
                    static_cast<std::streamsize>(entry.data.size()));
        }

        centralEntries.push_back({entry.path, entryCrc, entrySize, static_cast<uint32_t>(currentOffset)});
    }

    std::streamoff centralOffset = output.tellp();
    if (centralOffset < 0 || static_cast<uint64_t>(centralOffset) > 0xFFFFFFFFULL) {
        throw std::runtime_error("ZIP 中央目录过大");
    }

    for (const CentralDirectoryEntry& entry : centralEntries) {
        writeUint32(output, kZipCentralHeaderSignature);
        writeUint16(output, 20);
        writeUint16(output, 20);
        writeUint16(output, kZipUtf8Flag);
        writeUint16(output, 0);
        writeUint16(output, 0);
        writeUint16(output, 0);
        writeUint32(output, entry.crc32);
        writeUint32(output, entry.size);
        writeUint32(output, entry.size);
        writeUint16(output, static_cast<uint16_t>(entry.path.size()));
        writeUint16(output, 0);
        writeUint16(output, 0);
        writeUint16(output, 0);
        writeUint16(output, 0);
        writeUint32(output, 0);
        writeUint32(output, entry.offset);
        output.write(entry.path.data(), static_cast<std::streamsize>(entry.path.size()));
    }

    std::streamoff centralEnd = output.tellp();
    if (centralEnd < centralOffset || static_cast<uint64_t>(centralEnd - centralOffset) > 0xFFFFFFFFULL) {
        throw std::runtime_error("ZIP 中央目录长度无效");
    }

    writeUint32(output, kZipEndSignature);
    writeUint16(output, 0);
    writeUint16(output, 0);
    writeUint16(output, static_cast<uint16_t>(centralEntries.size()));
    writeUint16(output, static_cast<uint16_t>(centralEntries.size()));
    writeUint32(output, static_cast<uint32_t>(centralEnd - centralOffset));
    writeUint32(output, static_cast<uint32_t>(centralOffset));
    writeUint16(output, 0);
    output.close();

    if (!output) {
        fs::remove(temporaryPath);
        throw std::runtime_error("写入 ZIP 文件失败：" + pathToUtf8(outputPath));
    }

    std::error_code ignored;
    fs::remove(outputPath, ignored);
    fs::rename(temporaryPath, outputPath);
}

void packProject(
        const fs::path& rawProjectDirectory,
        const fs::path* requestedOutputPath
) {
    fs::path projectDirectory = fs::absolute(rawProjectDirectory).lexically_normal();
    if (!fs::is_directory(projectDirectory)) {
        throw std::runtime_error("项目目录不存在：" + pathToUtf8(projectDirectory));
    }

    PackageConfig config = readPackageConfig(projectDirectory);
    fs::path entryPath = projectDirectory / utf8ToPath(config.entry);
    if (!fs::is_regular_file(entryPath)) {
        throw std::runtime_error("entry 指向的 Lua 文件不存在：" + config.entry);
    }
    if (isExcluded(config.entry, config.excludeRules)) {
        throw std::runtime_error("entry 不能被 exclude 排除：" + config.entry);
    }

    fs::path outputPath = fs::absolute(resolveOutputPath(projectDirectory, requestedOutputPath));
    outputPath = outputPath.lexically_normal();

    std::vector<std::pair<std::string, fs::path>> projectFiles;
    for (const fs::directory_entry& item : fs::recursive_directory_iterator(projectDirectory)) {
        if (!item.is_regular_file()) {
            continue;
        }

        std::string relativePath = relativeProjectPath(projectDirectory, item.path());
        if (relativePath == kPackageConfigFileName || isExcluded(relativePath, config.excludeRules)) {
            continue;
        }

        std::error_code equivalentError;
        if (fs::equivalent(item.path(), outputPath, equivalentError) && !equivalentError) {
            continue;
        }
        if (hasSuffix(relativePath, kPackageExtension)) {
            continue;
        }
        projectFiles.emplace_back(relativePath, item.path());
    }

    std::sort(projectFiles.begin(), projectFiles.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });

    std::map<std::string, LuaFileMetadata> luaFiles;
    std::vector<std::string> resourceFiles;
    std::vector<ArchiveEntry> archiveEntries;

    for (const auto& item : projectFiles) {
        const std::string& relativePath = item.first;
        std::vector<uint8_t> source = readBinaryFile(item.second);
        if (hasSuffix(relativePath, ".lua")) {
            std::vector<uint8_t> bytecode = compileLuaBytecode(source, relativePath);
            LuaFileMetadata metadata;
            metadata.archivePath = luaArchivePath(relativePath);
            if (!randomBytes(metadata.nonce.data(), metadata.nonce.size())) {
                throw std::runtime_error("无法生成 Lua 加密随机 nonce");
            }

            std::vector<uint8_t> cipherText(bytecode.size());
            if (alpkg_encrypt_lua(
                    cipherText.data(),
                    metadata.tag.data(),
                    metadata.nonce.data(),
                    relativePath.data(),
                    relativePath.size(),
                    bytecode.data(),
                    bytecode.size()) != 0) {
                alpkg_wipe(bytecode.data(), bytecode.size());
                throw std::runtime_error("Lua 字节码加密失败：" + relativePath);
            }
            alpkg_wipe(bytecode.data(), bytecode.size());
            luaFiles.emplace(relativePath, metadata);
            archiveEntries.push_back({metadata.archivePath, std::move(cipherText)});
        } else {
            resourceFiles.push_back(relativePath);
            archiveEntries.push_back({relativePath, std::move(source)});
        }
    }

    if (luaFiles.find(config.entry) == luaFiles.end()) {
        throw std::runtime_error("entry 未被打包：" + config.entry);
    }

    std::string manifest = makeManifest(config, luaFiles, resourceFiles);
    ArchiveEntry manifestEntry;
    manifestEntry.path = kManifestPath;
    manifestEntry.data.assign(manifest.begin(), manifest.end());
    archiveEntries.insert(archiveEntries.begin(), std::move(manifestEntry));
    writeZip(outputPath, archiveEntries);

    std::cout << "打包完成：" << pathToUtf8(outputPath) << "\n";
    std::cout << "Lua 文件：" << luaFiles.size() << "，资源文件：" << resourceFiles.size() << "\n";
}

void printUsage() {
    std::cout << "用法：autolua_pack.exe <项目文件夹> [输出文件]\n";
    std::cout << "项目根目录使用 alpkg.json；缺失时自动创建默认配置，入口为 main.lua。\n";
}

} // namespace

int main(int argumentCount, char* arguments[]) {
    try {
#ifdef _WIN32
        (void) argumentCount;
        (void) arguments;
        int wideArgumentCount = 0;
        wchar_t** wideArguments = CommandLineToArgvW(GetCommandLineW(), &wideArgumentCount);
        if (wideArguments == nullptr || wideArgumentCount < 2 || wideArgumentCount > 3) {
            if (wideArguments != nullptr) {
                LocalFree(wideArguments);
            }
            printUsage();
            return 2;
        }

        fs::path projectPath(wideArguments[1]);
        fs::path outputPath;
        const fs::path* requestedOutputPath = nullptr;
        if (wideArgumentCount == 3) {
            outputPath = fs::path(wideArguments[2]);
            requestedOutputPath = &outputPath;
        }
        packProject(projectPath, requestedOutputPath);
        LocalFree(wideArguments);
#else
        if (argumentCount < 2 || argumentCount > 3) {
            printUsage();
            return 2;
        }
        fs::path outputPath;
        const fs::path* requestedOutputPath = nullptr;
        if (argumentCount == 3) {
            outputPath = fs::path(arguments[2]);
            requestedOutputPath = &outputPath;
        }
        packProject(fs::path(arguments[1]), requestedOutputPath);
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "打包失败：" << error.what() << "\n";
        return 1;
    }
}
