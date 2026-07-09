#include "json_value.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {

const std::string kEmptyString;
const std::vector<JsonValue> kEmptyArray;
const std::map<std::string, JsonValue> kEmptyObject;

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text), offset_(0) {
    }

    bool parse(JsonValue* value, std::string* error) {
        skipWhitespace();
        if (!parseValue(value, error)) {
            return false;
        }

        skipWhitespace();
        if (offset_ != text_.size()) {
            setError(error, "json has trailing characters");
            return false;
        }
        return true;
    }

private:
    const std::string& text_;
    size_t offset_;

    void skipWhitespace() {
        while (offset_ < text_.size()) {
            char ch = text_[offset_];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                ++offset_;
                continue;
            }
            break;
        }
    }

    bool parseValue(JsonValue* value, std::string* error) {
        if (offset_ >= text_.size()) {
            setError(error, "json value is empty");
            return false;
        }

        char ch = text_[offset_];
        if (ch == '{') {
            return parseObject(value, error);
        }
        if (ch == '[') {
            return parseArray(value, error);
        }
        if (ch == '"') {
            std::string result;
            if (!parseString(&result, error)) {
                return false;
            }
            *value = JsonValue::makeString(std::move(result));
            return true;
        }
        if (ch == 't') {
            return parseLiteral("true", JsonValue::makeBool(true), value, error);
        }
        if (ch == 'f') {
            return parseLiteral("false", JsonValue::makeBool(false), value, error);
        }
        if (ch == 'n') {
            return parseLiteral("null", JsonValue::makeNull(), value, error);
        }
        if (ch == '-' || (ch >= '0' && ch <= '9')) {
            return parseNumber(value, error);
        }

        setError(error, "json value type is invalid");
        return false;
    }

    bool parseObject(JsonValue* value, std::string* error) {
        if (!consume('{')) {
            setError(error, "json object must start with {");
            return false;
        }

        std::map<std::string, JsonValue> object;
        skipWhitespace();
        if (consume('}')) {
            *value = JsonValue::makeObject(std::move(object));
            return true;
        }

        while (offset_ < text_.size()) {
            skipWhitespace();
            std::string key;
            if (!parseString(&key, error)) {
                return false;
            }

            skipWhitespace();
            if (!consume(':')) {
                setError(error, "json object key must be followed by :");
                return false;
            }

            JsonValue item;
            skipWhitespace();
            if (!parseValue(&item, error)) {
                return false;
            }
            object[key] = std::move(item);

            skipWhitespace();
            if (consume('}')) {
                *value = JsonValue::makeObject(std::move(object));
                return true;
            }
            if (!consume(',')) {
                setError(error, "json object item must be followed by , or }");
                return false;
            }
        }

        setError(error, "json object is incomplete");
        return false;
    }

    bool parseArray(JsonValue* value, std::string* error) {
        if (!consume('[')) {
            setError(error, "json array must start with [");
            return false;
        }

        std::vector<JsonValue> array;
        skipWhitespace();
        if (consume(']')) {
            *value = JsonValue::makeArray(std::move(array));
            return true;
        }

        while (offset_ < text_.size()) {
            JsonValue item;
            skipWhitespace();
            if (!parseValue(&item, error)) {
                return false;
            }
            array.push_back(std::move(item));

            skipWhitespace();
            if (consume(']')) {
                *value = JsonValue::makeArray(std::move(array));
                return true;
            }
            if (!consume(',')) {
                setError(error, "json array item must be followed by , or ]");
                return false;
            }
        }

        setError(error, "json array is incomplete");
        return false;
    }

    bool parseString(std::string* value, std::string* error) {
        if (!consume('"')) {
            setError(error, "json string must start with quote");
            return false;
        }

        std::string result;
        while (offset_ < text_.size()) {
            unsigned char ch = static_cast<unsigned char>(text_[offset_++]);
            if (ch == '"') {
                *value = std::move(result);
                return true;
            }
            if (ch != '\\') {
                result.push_back(static_cast<char>(ch));
                continue;
            }

            if (offset_ >= text_.size()) {
                setError(error, "json string escape is incomplete");
                return false;
            }
            char escape = text_[offset_++];
            switch (escape) {
                case '"':
                case '\\':
                case '/':
                    result.push_back(escape);
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case 'u':
                    if (!appendUnicodeEscape(&result, error)) {
                        return false;
                    }
                    break;
                default:
                    setError(error, "json string escape is invalid");
                    return false;
            }
        }

        setError(error, "json string is incomplete");
        return false;
    }

    bool appendUnicodeEscape(std::string* output, std::string* error) {
        int codePoint = 0;
        if (!readHexCodeUnit(&codePoint)) {
            setError(error, "json unicode escape is invalid");
            return false;
        }

        // 支持代理对，保证 IDE 传入的 emoji 或扩展字符不会被拆坏。
        if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
            if (offset_ + 2 > text_.size() || text_[offset_] != '\\' || text_[offset_ + 1] != 'u') {
                setError(error, "json unicode surrogate pair is incomplete");
                return false;
            }
            offset_ += 2;
            int low = 0;
            if (!readHexCodeUnit(&low) || low < 0xDC00 || low > 0xDFFF) {
                setError(error, "json unicode surrogate pair is invalid");
                return false;
            }
            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
        }

        appendUtf8(output, codePoint);
        return true;
    }

    bool readHexCodeUnit(int* value) {
        if (offset_ + 4 > text_.size()) {
            return false;
        }

        int result = 0;
        for (int i = 0; i < 4; ++i) {
            char ch = text_[offset_++];
            int part = 0;
            if (ch >= '0' && ch <= '9') {
                part = ch - '0';
            } else if (ch >= 'a' && ch <= 'f') {
                part = ch - 'a' + 10;
            } else if (ch >= 'A' && ch <= 'F') {
                part = ch - 'A' + 10;
            } else {
                return false;
            }
            result = (result << 4) | part;
        }

        *value = result;
        return true;
    }

    static void appendUtf8(std::string* output, int codePoint) {
        if (codePoint <= 0x7F) {
            output->push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7FF) {
            output->push_back(static_cast<char>(0xC0 | ((codePoint >> 6) & 0x1F)));
            output->push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else if (codePoint <= 0xFFFF) {
            output->push_back(static_cast<char>(0xE0 | ((codePoint >> 12) & 0x0F)));
            output->push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            output->push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
            output->push_back(static_cast<char>(0xF0 | ((codePoint >> 18) & 0x07)));
            output->push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            output->push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            output->push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    bool parseNumber(JsonValue* value, std::string* error) {
        size_t start = offset_;
        if (peek() == '-') {
            ++offset_;
        }
        if (peek() == '0') {
            ++offset_;
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') {
                ++offset_;
            }
        } else {
            setError(error, "json number integer part is invalid");
            return false;
        }

        if (peek() == '.') {
            ++offset_;
            if (!(peek() >= '0' && peek() <= '9')) {
                setError(error, "json number decimal part is invalid");
                return false;
            }
            while (peek() >= '0' && peek() <= '9') {
                ++offset_;
            }
        }

        if (peek() == 'e' || peek() == 'E') {
            ++offset_;
            if (peek() == '+' || peek() == '-') {
                ++offset_;
            }
            if (!(peek() >= '0' && peek() <= '9')) {
                setError(error, "json number exponent is invalid");
                return false;
            }
            while (peek() >= '0' && peek() <= '9') {
                ++offset_;
            }
        }

        std::string numberText = text_.substr(start, offset_ - start);
        char* end = nullptr;
        double number = std::strtod(numberText.c_str(), &end);
        if (end == numberText.c_str() || *end != '\0') {
            setError(error, "json number parse failed");
            return false;
        }
        *value = JsonValue::makeNumber(number);
        return true;
    }

    bool parseLiteral(const char* literal,
                      JsonValue literalValue,
                      JsonValue* value,
                      std::string* error) {
        std::string expected(literal);
        if (text_.compare(offset_, expected.size(), expected) != 0) {
            setError(error, "json literal is invalid");
            return false;
        }
        offset_ += expected.size();
        *value = std::move(literalValue);
        return true;
    }

    bool consume(char expected) {
        if (offset_ < text_.size() && text_[offset_] == expected) {
            ++offset_;
            return true;
        }
        return false;
    }

    char peek() const {
        if (offset_ >= text_.size()) {
            return '\0';
        }
        return text_[offset_];
    }

    static void setError(std::string* error, const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
    }
};

} // namespace

JsonValue::JsonValue()
        : type_(Type::Null),
          boolValue_(false),
          numberValue_(0.0) {
}

JsonValue JsonValue::makeNull() {
    return JsonValue();
}

JsonValue JsonValue::makeBool(bool value) {
    JsonValue result;
    result.type_ = Type::Bool;
    result.boolValue_ = value;
    return result;
}

JsonValue JsonValue::makeNumber(double value) {
    JsonValue result;
    result.type_ = Type::Number;
    result.numberValue_ = value;
    return result;
}

JsonValue JsonValue::makeString(std::string value) {
    JsonValue result;
    result.type_ = Type::String;
    result.stringValue_ = std::move(value);
    return result;
}

JsonValue JsonValue::makeArray(std::vector<JsonValue> value) {
    JsonValue result;
    result.type_ = Type::Array;
    result.arrayValue_ = std::move(value);
    return result;
}

JsonValue JsonValue::makeObject(std::map<std::string, JsonValue> value) {
    JsonValue result;
    result.type_ = Type::Object;
    result.objectValue_ = std::move(value);
    return result;
}

JsonValue::Type JsonValue::type() const {
    return type_;
}

bool JsonValue::isNull() const {
    return type_ == Type::Null;
}

bool JsonValue::isBool() const {
    return type_ == Type::Bool;
}

bool JsonValue::isNumber() const {
    return type_ == Type::Number;
}

bool JsonValue::isString() const {
    return type_ == Type::String;
}

bool JsonValue::isArray() const {
    return type_ == Type::Array;
}

bool JsonValue::isObject() const {
    return type_ == Type::Object;
}

bool JsonValue::boolValue(bool defaultValue) const {
    if (type_ == Type::Bool) {
        return boolValue_;
    }
    return defaultValue;
}

int JsonValue::intValue(int defaultValue) const {
    if (type_ == Type::Number) {
        return static_cast<int>(numberValue_);
    }
    return defaultValue;
}

long long JsonValue::longValue(long long defaultValue) const {
    if (type_ == Type::Number) {
        return static_cast<long long>(numberValue_);
    }
    return defaultValue;
}

double JsonValue::numberValue(double defaultValue) const {
    if (type_ == Type::Number) {
        return numberValue_;
    }
    return defaultValue;
}

const std::string& JsonValue::stringValue() const {
    if (type_ == Type::String) {
        return stringValue_;
    }
    return kEmptyString;
}

const std::vector<JsonValue>& JsonValue::arrayValue() const {
    if (type_ == Type::Array) {
        return arrayValue_;
    }
    return kEmptyArray;
}

const std::map<std::string, JsonValue>& JsonValue::objectValue() const {
    if (type_ == Type::Object) {
        return objectValue_;
    }
    return kEmptyObject;
}

bool JsonValue::has(const std::string& key) const {
    return type_ == Type::Object && objectValue_.find(key) != objectValue_.end();
}

const JsonValue* JsonValue::get(const std::string& key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }

    auto iterator = objectValue_.find(key);
    if (iterator == objectValue_.end()) {
        return nullptr;
    }
    return &iterator->second;
}

std::string JsonValue::stringOr(const std::string& key, const std::string& defaultValue) const {
    const JsonValue* value = get(key);
    if (value == nullptr || !value->isString()) {
        return defaultValue;
    }
    return value->stringValue();
}

int JsonValue::intOr(const std::string& key, int defaultValue) const {
    const JsonValue* value = get(key);
    if (value == nullptr || !value->isNumber()) {
        return defaultValue;
    }
    return value->intValue(defaultValue);
}

bool JsonValue::boolOr(const std::string& key, bool defaultValue) const {
    const JsonValue* value = get(key);
    if (value == nullptr || !value->isBool()) {
        return defaultValue;
    }
    return value->boolValue(defaultValue);
}

bool parseJsonText(const std::string& text, JsonValue* value, std::string* error) {
    JsonParser parser(text);
    return parser.parse(value, error);
}

std::string escapeJsonString(const std::string& value) {
    std::ostringstream output;
    for (unsigned char ch : value) {
        switch (ch) {
            case '\\':
                output << "\\\\";
                break;
            case '"':
                output << "\\\"";
                break;
            case '\b':
                output << "\\b";
                break;
            case '\f':
                output << "\\f";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                if (ch < 0x20) {
                    output << "\\u"
                           << std::hex
                           << std::setw(4)
                           << std::setfill('0')
                           << static_cast<int>(ch)
                           << std::dec;
                } else {
                    output << static_cast<char>(ch);
                }
                break;
        }
    }
    return output.str();
}

std::string quoteJsonString(const std::string& value) {
    return "\"" + escapeJsonString(value) + "\"";
}

std::string jsonValueToString(const JsonValue& value) {
    std::ostringstream output;
    switch (value.type()) {
        case JsonValue::Type::Null:
            return "null";
        case JsonValue::Type::Bool:
            return value.boolValue() ? "true" : "false";
        case JsonValue::Type::Number:
            if (std::isfinite(value.numberValue())) {
                output << std::setprecision(15) << value.numberValue();
                return output.str();
            }
            return "0";
        case JsonValue::Type::String:
            return quoteJsonString(value.stringValue());
        case JsonValue::Type::Array: {
            output << "[";
            const auto& array = value.arrayValue();
            for (size_t i = 0; i < array.size(); ++i) {
                if (i > 0) {
                    output << ",";
                }
                output << jsonValueToString(array[i]);
            }
            output << "]";
            return output.str();
        }
        case JsonValue::Type::Object: {
            output << "{";
            bool first = true;
            for (const auto& item : value.objectValue()) {
                if (!first) {
                    output << ",";
                }
                first = false;
                output << quoteJsonString(item.first) << ":" << jsonValueToString(item.second);
            }
            output << "}";
            return output.str();
        }
    }
    return "null";
}
