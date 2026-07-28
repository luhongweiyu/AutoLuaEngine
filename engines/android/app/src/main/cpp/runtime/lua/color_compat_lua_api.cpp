/**
 * 文件用途：在统一 RGBA 截图缓存上实现取色、比色、计数与多点找色兼容能力。
 */
#include "color_compat_lua_api.h"

#include "../../core/system_c_api.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace {

struct ScreenView {
    int width = 0;
    int height = 0;
    unsigned char* pixels = nullptr;
};

struct Region {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct ColorRule {
    int red = 0;
    int green = 0;
    int blue = 0;
    int deltaRed = 0;
    int deltaGreen = 0;
    int deltaBlue = 0;
    std::string source;
};

struct OffsetRule {
    int x = 0;
    int y = 0;
    std::vector<ColorRule> colors;
};

bool screen(ScreenView* view) {
    return view != nullptr
            && engine_getScreenPixels(&view->width, &view->height, &view->pixels) != 0
            && view->width > 0
            && view->height > 0
            && view->pixels != nullptr;
}

Region regionFromArgs(lua_State* state, int first, const ScreenView& view) {
    int x1 = static_cast<int>(luaL_checkinteger(state, first));
    int y1 = static_cast<int>(luaL_checkinteger(state, first + 1));
    int x2 = static_cast<int>(luaL_checkinteger(state, first + 2));
    int y2 = static_cast<int>(luaL_checkinteger(state, first + 3));
    if (x1 == 0 && y1 == 0 && x2 == 0 && y2 == 0) {
        return Region{0, 0, view.width - 1, view.height - 1};
    }
    Region result{
            std::min(x1, x2),
            std::min(y1, y2),
            std::max(x1, x2),
            std::max(y1, y2)
    };
    result.left = std::max(0, result.left);
    result.top = std::max(0, result.top);
    result.right = std::min(view.width - 1, result.right);
    result.bottom = std::min(view.height - 1, result.bottom);
    return result;
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool parseHexColor(const std::string& text, int* red, int* green, int* blue) {
    std::string value = text;
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) {
        value.erase(0, 2);
    } else if (!value.empty() && value[0] == '#') {
        value.erase(0, 1);
    }
    if (value.size() != 6) {
        return false;
    }
    int digits[6];
    for (int index = 0; index < 6; ++index) {
        digits[index] = hexValue(value[static_cast<size_t>(index)]);
        if (digits[index] < 0) return false;
    }
    *red = digits[0] * 16 + digits[1];
    *green = digits[2] * 16 + digits[3];
    *blue = digits[4] * 16 + digits[5];
    return true;
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> result;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, separator)) {
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

std::vector<std::string> splitColorAlternatives(const std::string& text) {
    std::vector<std::string> result;
    std::string item;
    for (char value : text) {
        if (value == '|' || value == ' ' || value == '\t' || value == '\r' || value == '\n') {
            if (!item.empty()) {
                result.push_back(item);
                item.clear();
            }
        } else {
            item.push_back(value);
        }
    }
    if (!item.empty()) result.push_back(item);
    return result;
}

bool parseInt(const std::string& text, int* output) {
    if (output == nullptr || text.empty()) {
        return false;
    }
    char* end = nullptr;
    long value = std::strtol(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0'
            || value < std::numeric_limits<int>::min()
            || value > std::numeric_limits<int>::max()) {
        return false;
    }
    *output = static_cast<int>(value);
    return true;
}

bool parseColorRule(const std::string& text, ColorRule* rule) {
    if (rule == nullptr) return false;
    size_t dash = text.find('-');
    std::string color = dash == std::string::npos ? text : text.substr(0, dash);
    std::string delta = dash == std::string::npos ? "" : text.substr(dash + 1);
    if (!parseHexColor(color, &rule->red, &rule->green, &rule->blue)) {
        return false;
    }
    rule->source = color;
    if (!delta.empty()
            && !parseHexColor(delta, &rule->deltaRed, &rule->deltaGreen, &rule->deltaBlue)) {
        return false;
    }
    return true;
}

std::vector<ColorRule> parseColors(lua_State* state, const char* text) {
    std::vector<ColorRule> result;
    for (const std::string& item : splitColorAlternatives(text == nullptr ? "" : text)) {
        ColorRule rule;
        if (!parseColorRule(item, &rule)) {
            luaL_error(state, "颜色格式无效：%s", item.c_str());
            return {};
        }
        result.push_back(rule);
    }
    if (result.empty()) {
        luaL_error(state, "颜色参数不能为空");
    }
    return result;
}

std::vector<OffsetRule> parseOffsets(lua_State* state, const char* text) {
    std::vector<OffsetRule> result;
    for (const std::string& item : split(text == nullptr ? "" : text, ',')) {
        std::vector<std::string> fields = split(item, '|');
        if (fields.size() < 3) {
            luaL_error(state, "偏移颜色格式无效：%s", item.c_str());
            return {};
        }
        int x = 0;
        if (!parseInt(fields[0], &x)) {
            luaL_error(state, "偏移 X 坐标无效：%s", fields[0].c_str());
            return {};
        }
        int y = 0;
        if (!parseInt(fields[1], &y)) {
            luaL_error(state, "偏移 Y 坐标无效：%s", fields[1].c_str());
            return {};
        }
        std::string colors = fields[2];
        for (size_t index = 3; index < fields.size(); ++index) {
            colors += "|" + fields[index];
        }
        OffsetRule offset;
        offset.x = x;
        offset.y = y;
        offset.colors = parseColors(state, colors.c_str());
        result.push_back(std::move(offset));
    }
    return result;
}

std::uint32_t pixelAt(const ScreenView& view, int x, int y) {
    const unsigned char* pixel = view.pixels
            + (static_cast<size_t>(y) * static_cast<size_t>(view.width)
                    + static_cast<size_t>(x)) * 4U;
    return (static_cast<std::uint32_t>(pixel[0]) << 16U)
            | (static_cast<std::uint32_t>(pixel[1]) << 8U)
            | static_cast<std::uint32_t>(pixel[2]);
}

bool matches(std::uint32_t pixel, const ColorRule& rule, double similarity) {
    int tolerance = static_cast<int>(
            std::round(std::max(0.0, std::min(1.0, 1.0 - similarity)) * 255.0)
    );
    int red = static_cast<int>((pixel >> 16U) & 0xffU);
    int green = static_cast<int>((pixel >> 8U) & 0xffU);
    int blue = static_cast<int>(pixel & 0xffU);
    return std::abs(red - rule.red) <= std::max(tolerance, rule.deltaRed)
            && std::abs(green - rule.green) <= std::max(tolerance, rule.deltaGreen)
            && std::abs(blue - rule.blue) <= std::max(tolerance, rule.deltaBlue);
}

bool matchesAny(
        const ScreenView& view,
        int x,
        int y,
        const std::vector<ColorRule>& colors,
        double similarity,
        std::string* matchedSource = nullptr
) {
    if (x < 0 || y < 0 || x >= view.width || y >= view.height) return false;
    std::uint32_t pixel = pixelAt(view, x, y);
    for (const ColorRule& color : colors) {
        if (matches(pixel, color, similarity)) {
            if (matchedSource != nullptr) *matchedSource = color.source;
            return true;
        }
    }
    return false;
}

bool matchesMulti(
        const ScreenView& view,
        int x,
        int y,
        const std::vector<ColorRule>& first,
        const std::vector<OffsetRule>& offsets,
        double similarity
) {
    if (!matchesAny(view, x, y, first, similarity)) return false;
    for (const OffsetRule& offset : offsets) {
        if (!matchesAny(view, x + offset.x, y + offset.y, offset.colors, similarity)) {
            return false;
        }
    }
    return true;
}

double similarityFromArg(lua_State* state, int index, double defaultValue = 1.0) {
    double similarity = luaL_optnumber(state, index, defaultValue);
    if (!std::isfinite(similarity) || similarity < 0.0 || similarity > 1.0) {
        luaL_argerror(state, index, "相似度必须在 0 到 1 之间");
    }
    return similarity;
}

int directionFromArg(lua_State* state, int index, int defaultValue = 0) {
    lua_Integer direction = luaL_optinteger(state, index, defaultValue);
    if (direction < 0 || direction > 4) {
        luaL_argerror(state, index, "查找方向必须在 0 到 4 之间");
    }
    return static_cast<int>(direction);
}

template <typename Callback>
bool scanRegion(const Region& region, int direction, Callback callback) {
    if (region.left > region.right || region.top > region.bottom) return false;
    if (direction == 1) {
        int centerX = (region.left + region.right) / 2;
        int centerY = (region.top + region.bottom) / 2;
        int maximum = std::max({
                centerX - region.left,
                region.right - centerX,
                centerY - region.top,
                region.bottom - centerY
        });
        for (int radius = 0; radius <= maximum; ++radius) {
            int left = centerX - radius;
            int right = centerX + radius;
            int top = centerY - radius;
            int bottom = centerY + radius;
            int clippedLeft = std::max(region.left, left);
            int clippedRight = std::min(region.right, right);
            for (int x = clippedLeft; x <= clippedRight; ++x) {
                if (top >= region.top && top <= region.bottom && callback(x, top)) {
                    return true;
                }
                if (bottom != top
                        && bottom >= region.top
                        && bottom <= region.bottom
                        && callback(x, bottom)) {
                    return true;
                }
            }
            int clippedTop = std::max(region.top, top + 1);
            int clippedBottom = std::min(region.bottom, bottom - 1);
            for (int y = clippedTop; y <= clippedBottom; ++y) {
                if (left >= region.left && left <= region.right && callback(left, y)) {
                    return true;
                }
                if (right != left
                        && right >= region.left
                        && right <= region.right
                        && callback(right, y)) {
                    return true;
                }
            }
        }
        return false;
    }

    int xStart = (direction == 2 || direction == 4) ? region.right : region.left;
    int xEnd = (direction == 2 || direction == 4) ? region.left : region.right;
    int xStep = xStart <= xEnd ? 1 : -1;
    int yStart = (direction == 2 || direction == 3) ? region.bottom : region.top;
    int yEnd = (direction == 2 || direction == 3) ? region.top : region.bottom;
    int yStep = yStart <= yEnd ? 1 : -1;
    for (int y = yStart;; y += yStep) {
        for (int x = xStart;; x += xStep) {
            if (callback(x, y)) return true;
            if (x == xEnd) break;
        }
        if (y == yEnd) break;
    }
    return false;
}

int luaGetPixel(lua_State* state) {
    ScreenView view;
    if (!screen(&view)) {
        lua_pushnil(state);
        lua_pushstring(state, engine_screenLastError());
        return 2;
    }
    int x = static_cast<int>(luaL_checkinteger(state, 1));
    int y = static_cast<int>(luaL_checkinteger(state, 2));
    if (x < 0 || y < 0 || x >= view.width || y >= view.height) {
        lua_pushnil(state);
        lua_pushliteral(state, "取色坐标超出屏幕");
        return 2;
    }
    lua_pushinteger(state, static_cast<lua_Integer>(pixelAt(view, x, y)));
    return 1;
}

int luaGetRegion(lua_State* state) {
    ScreenView view;
    if (!screen(&view)) {
        lua_pushinteger(state, -1);
        lua_pushinteger(state, -1);
        lua_pushnil(state);
        return 3;
    }
    Region region = regionFromArgs(state, 1, view);
    if (region.left > region.right || region.top > region.bottom) {
        lua_pushinteger(state, -1);
        lua_pushinteger(state, -1);
        lua_pushnil(state);
        return 3;
    }
    int width = region.right - region.left + 1;
    int height = region.bottom - region.top + 1;
    long long pixelCount = static_cast<long long>(width) * static_cast<long long>(height);
    if (pixelCount > std::numeric_limits<int>::max()) {
        return luaL_error(state, "取色区域像素数量超出 Lua table 上限");
    }
    lua_pushinteger(state, width);
    lua_pushinteger(state, height);
    lua_createtable(state, static_cast<int>(pixelCount), 0);
    lua_Integer outputIndex = 1;
    for (int y = region.top; y <= region.bottom; ++y) {
        for (int x = region.left; x <= region.right; ++x) {
            lua_pushinteger(state, static_cast<lua_Integer>(pixelAt(view, x, y)));
            lua_rawseti(state, -2, outputIndex++);
        }
    }
    return 3;
}

int luaMatch(lua_State* state) {
    ScreenView view;
    if (!screen(&view)) {
        lua_pushboolean(state, 0);
        return 1;
    }
    int x = static_cast<int>(luaL_checkinteger(state, 1));
    int y = static_cast<int>(luaL_checkinteger(state, 2));
    std::vector<ColorRule> colors = parseColors(state, luaL_checkstring(state, 3));
    double similarity = similarityFromArg(state, 4);
    lua_pushboolean(state, matchesAny(view, x, y, colors, similarity));
    return 1;
}

int luaCount(lua_State* state) {
    ScreenView view;
    if (!screen(&view)) {
        lua_pushinteger(state, 0);
        return 1;
    }
    Region region = regionFromArgs(state, 1, view);
    std::vector<ColorRule> colors = parseColors(state, luaL_checkstring(state, 5));
    double similarity = similarityFromArg(state, 6);
    lua_Integer count = 0;
    scanRegion(region, 0, [&](int x, int y) {
        if (matchesAny(view, x, y, colors, similarity)) ++count;
        return false;
    });
    lua_pushinteger(state, count);
    return 1;
}

int luaComparePoints(lua_State* state) {
    ScreenView view;
    if (!screen(&view)) {
        lua_pushboolean(state, 0);
        return 1;
    }
    const char* description = luaL_checkstring(state, 1);
    double similarity = similarityFromArg(state, 2);
    for (const std::string& point : split(description == nullptr ? "" : description, ',')) {
        std::vector<std::string> fields = split(point, '|');
        if (fields.size() < 3) {
            return luaL_error(state, "多点比色格式无效：%s", point.c_str());
        }
        int x = 0;
        int y = 0;
        if (!parseInt(fields[0], &x) || !parseInt(fields[1], &y)) {
            return luaL_error(state, "多点比色坐标无效：%s", point.c_str());
        }
        std::string colors = fields[2];
        for (size_t index = 3; index < fields.size(); ++index) {
            colors += "|" + fields[index];
        }
        if (!matchesAny(view, x, y, parseColors(state, colors.c_str()), similarity)) {
            lua_pushboolean(state, 0);
            return 1;
        }
    }
    lua_pushboolean(state, 1);
    return 1;
}

int luaFindMulti(lua_State* state) {
    ScreenView view;
    if (!screen(&view)) {
        lua_pushnil(state);
        lua_pushstring(state, engine_screenLastError());
        return 2;
    }
    Region region = regionFromArgs(state, 1, view);
    std::vector<ColorRule> first = parseColors(state, luaL_checkstring(state, 5));
    std::vector<OffsetRule> offsets = parseOffsets(state, luaL_optstring(state, 6, ""));
    int direction = directionFromArg(state, 7);
    double similarity = similarityFromArg(state, 8);
    bool all = lua_toboolean(state, 9) != 0;

    if (all) {
        lua_newtable(state);
        lua_Integer resultIndex = 1;
        scanRegion(region, direction, [&](int x, int y) {
            if (matchesMulti(view, x, y, first, offsets, similarity)) {
                lua_createtable(state, 0, 2);
                lua_pushinteger(state, x);
                lua_setfield(state, -2, "x");
                lua_pushinteger(state, y);
                lua_setfield(state, -2, "y");
                lua_rawseti(state, -2, resultIndex++);
            }
            return false;
        });
        return 1;
    }

    int foundX = -1;
    int foundY = -1;
    scanRegion(region, direction, [&](int x, int y) {
        if (!matchesMulti(view, x, y, first, offsets, similarity)) return false;
        foundX = x;
        foundY = y;
        return true;
    });
    if (foundX < 0) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushinteger(state, foundX);
    lua_pushinteger(state, foundY);
    return 2;
}

int luaFindColor(lua_State* state) {
    ScreenView view;
    if (!screen(&view)) {
        lua_pushnil(state);
        lua_pushinteger(state, -1);
        lua_pushinteger(state, -1);
        return 3;
    }
    Region region = regionFromArgs(state, 1, view);
    std::vector<ColorRule> colors = parseColors(state, luaL_checkstring(state, 5));
    int direction = directionFromArg(state, 6);
    double similarity = similarityFromArg(state, 7);
    int foundX = -1;
    int foundY = -1;
    std::string source;
    scanRegion(region, direction, [&](int x, int y) {
        if (!matchesAny(view, x, y, colors, similarity, &source)) return false;
        foundX = x;
        foundY = y;
        return true;
    });
    if (foundX < 0) {
        lua_pushnil(state);
        lua_pushinteger(state, -1);
        lua_pushinteger(state, -1);
        return 3;
    }
    lua_pushlstring(state, source.data(), source.size());
    lua_pushinteger(state, foundX);
    lua_pushinteger(state, foundY);
    return 3;
}

int luaRegionHash(lua_State* state) {
    ScreenView view;
    if (!screen(&view)) {
        lua_pushnil(state);
        return 1;
    }
    Region region = regionFromArgs(state, 1, view);
    std::uint64_t hash = 1469598103934665603ULL;
    for (int y = region.top; y <= region.bottom; ++y) {
        for (int x = region.left; x <= region.right; ++x) {
            std::uint32_t pixel = pixelAt(view, x, y);
            hash ^= pixel;
            hash *= 1099511628211ULL;
        }
    }
    lua_pushinteger(state, static_cast<lua_Integer>(hash));
    return 1;
}

void setFunction(lua_State* state, int tableIndex, const char* name, lua_CFunction function) {
    int absolute = lua_absindex(state, tableIndex);
    lua_pushcfunction(state, function);
    lua_setfield(state, absolute, name);
}

} // namespace

void registerColorCompatLuaApi(lua_State* state, int hostTableIndex) {
    lua_newtable(state);
    int table = lua_gettop(state);
    setFunction(state, table, "getPixel", luaGetPixel);
    setFunction(state, table, "getRegion", luaGetRegion);
    setFunction(state, table, "match", luaMatch);
    setFunction(state, table, "count", luaCount);
    setFunction(state, table, "comparePoints", luaComparePoints);
    setFunction(state, table, "findMulti", luaFindMulti);
    setFunction(state, table, "findColor", luaFindColor);
    setFunction(state, table, "regionHash", luaRegionHash);
    lua_setfield(state, hostTableIndex, "colorCompat");
}
