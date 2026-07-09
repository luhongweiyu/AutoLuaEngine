/**
 * 文件用途：声明轻量 JSON 值类型和工具函数，避免引入额外第三方库。
 */
#pragma once

#include <map>
#include <string>
#include <vector>

/**
 * 引擎内部使用的轻量 JSON 值。
 *
 * 这里不引入第三方库，原因是 Android native 首版只需要解析 IDE/HTTP
 * 传入的对象参数，并生成稳定 JSON 响应。这个类型只放在 native 命令层使用，
 * 不作为脚本公开 ABI。
 */
class JsonValue {
public:
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    JsonValue();

    static JsonValue makeNull();
    static JsonValue makeBool(bool value);
    static JsonValue makeNumber(double value);
    static JsonValue makeString(std::string value);
    static JsonValue makeArray(std::vector<JsonValue> value);
    static JsonValue makeObject(std::map<std::string, JsonValue> value);

    Type type() const;
    bool isNull() const;
    bool isBool() const;
    bool isNumber() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;

    bool boolValue(bool defaultValue = false) const;
    int intValue(int defaultValue = 0) const;
    long long longValue(long long defaultValue = 0) const;
    double numberValue(double defaultValue = 0.0) const;
    const std::string& stringValue() const;
    const std::vector<JsonValue>& arrayValue() const;
    const std::map<std::string, JsonValue>& objectValue() const;

    bool has(const std::string& key) const;
    const JsonValue* get(const std::string& key) const;
    std::string stringOr(const std::string& key, const std::string& defaultValue = "") const;
    int intOr(const std::string& key, int defaultValue = 0) const;
    bool boolOr(const std::string& key, bool defaultValue = false) const;

private:
    Type type_;
    bool boolValue_;
    double numberValue_;
    std::string stringValue_;
    std::vector<JsonValue> arrayValue_;
    std::map<std::string, JsonValue> objectValue_;
};

/**
 * 解析 JSON 文本。
 *
 * 解析失败返回 false，并把错误原因写入 error。第一版命令入口只要求 params
 * 是 JSON 对象，但解析器本身支持对象、数组、字符串、数字、布尔和 null。
 */
bool parseJsonText(const std::string& text, JsonValue* value, std::string* error);

/**
 * JSON 输出工具。
 *
 * 命令分发层大量手写响应对象，统一走这里转义字符串，避免脚本代码、中文日志
 * 或 root 输出里的引号/换行破坏 JSON 结构。
 */
std::string escapeJsonString(const std::string& value);
std::string quoteJsonString(const std::string& value);
std::string jsonValueToString(const JsonValue& value);
