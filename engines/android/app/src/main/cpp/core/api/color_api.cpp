/**
 * 文件用途：实现高速多点找色核心 API，算法整理自旧项目找色实现。
 */
#include "color_api.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "screen_api.h"

namespace xiaoyv::api {
namespace {

// 保持旧项目限制：单次多点找色最多解析 500 个颜色点，避免热路径动态分配。
constexpr int 最大颜色点数 = 500;

/**
 * 预处理后的颜色比较项。
 *
 * 偏移在解析阶段按方向提前算好，扫描阶段只做一次锚点索引加法。
 */
struct 颜色比较项 {
    int 偏移 = 0;
    int16_t r下界 = 0;
    int16_t g下界 = 0;
    int16_t b下界 = 0;
    uint16_t r跨度 = 0;
    uint16_t g跨度 = 0;
    uint16_t b跨度 = 0;
};

/**
 * 解析 colors 后得到的全部热路径数据。
 *
 * 这里使用固定数组，避免每次找色产生堆分配和容量判断。
 */
struct 已解析颜色 {
    颜色比较项 颜色项[最大颜色点数];
    int 颜色长度 = 0;
    int 最小相对x = 0;
    int 最大相对x = 0;
    int 最小相对y = 0;
    int 最大相对y = 0;
};

// 转置点阵缓存直接用裸指针和 realloc，和旧项目保持同类实现方式。
uint32_t* g转置点阵 = nullptr;
size_t g转置容量 = 0;
int g转置宽度 = 0;
int g转置高度 = 0;
long long g转置帧编号 = -1;

// 错误字符串不是热路径；保留 std::string 只用于失败时返回错误。
std::string g最近错误;

int 十六进制字符转数值(char 字符) {
    if (字符 >= '0' && 字符 <= '9') {
        return 字符 - '0';
    }
    if (字符 >= 'a' && 字符 <= 'f') {
        return 字符 - 'a' + 10;
    }
    if (字符 >= 'A' && 字符 <= 'F') {
        return 字符 - 'A' + 10;
    }
    return -1;
}

bool 解析有符号整数(const char*& 游标, int& 结果) {
    const char* 起始位置 = 游标;
    bool 是否负数 = false;

    if (*游标 == '-' || *游标 == '+') {
        是否负数 = (*游标 == '-');
        ++游标;
    }

    if (*游标 < '0' || *游标 > '9') {
        游标 = 起始位置;
        return false;
    }

    int 累计值 = 0;
    while (*游标 >= '0' && *游标 <= '9') {
        累计值 = 累计值 * 10 + (*游标 - '0');
        ++游标;
    }

    结果 = 是否负数 ? -累计值 : 累计值;
    return true;
}

bool 解析6位十六进制(const char*& 游标, int& 结果) {
    const char* 起始位置 = 游标;
    if (游标[0] == '0' && (游标[1] == 'x' || 游标[1] == 'X')) {
        游标 += 2;
    }

    int 累计值 = 0;
    for (int 位序 = 0; 位序 < 6; ++位序) {
        int 半字节值 = 十六进制字符转数值(*游标);
        if (半字节值 < 0) {
            游标 = 起始位置;
            return false;
        }
        累计值 = (累计值 << 4) | 半字节值;
        ++游标;
    }

    结果 = 累计值;
    return true;
}

bool 解析颜色片段(
        const char*& 游标,
        int& 坐标x,
        int& 坐标y,
        int& 颜色值,
        bool& 有独立容差,
        int& 容差值
) {
    const char* 起始位置 = 游标;

    if (!解析有符号整数(游标, 坐标x) || *游标 != '|') {
        游标 = 起始位置;
        return false;
    }
    ++游标;

    if (!解析有符号整数(游标, 坐标y) || *游标 != '|') {
        游标 = 起始位置;
        return false;
    }
    ++游标;

    if (!解析6位十六进制(游标, 颜色值)) {
        游标 = 起始位置;
        return false;
    }

    有独立容差 = false;
    if (*游标 == '-' || *游标 == '|') {
        ++游标;
        if (!解析6位十六进制(游标, 容差值)) {
            游标 = 起始位置;
            return false;
        }
        有独立容差 = true;
    }

    return true;
}

bool 写入错误(const char* 错误) {
    g最近错误 = 错误 == nullptr ? "" : 错误;
    return false;
}

void 写入未找到坐标(找色坐标* 坐标) {
    if (坐标 != nullptr) {
        坐标->x = -1;
        坐标->y = -1;
    }
}

bool 是否转置方向(int 方向) {
    return 方向 == 1 || 方向 == 4 || 方向 == 5 || 方向 == 8;
}

bool 解析颜色字符串(
        const char* colors,
        int 宽度,
        int 高度,
        bool 使用转置点阵,
        int 默认容差,
        已解析颜色* 解析结果
) {
    if (colors == nullptr || 解析结果 == nullptr) {
        return 写入错误("找色参数为空");
    }

    int 锚点x = 0;
    int 锚点y = 0;
    bool 已有锚点 = false;
    const char* 扫描指针 = colors;

    while (*扫描指针 != '\0') {
        int 点x = 0;
        int 点y = 0;
        int 颜色值 = 0;
        int 容差值 = 默认容差;
        bool 有独立容差 = false;
        const char* 片段起始 = 扫描指针;

        if (!解析颜色片段(扫描指针, 点x, 点y, 颜色值, 有独立容差, 容差值)) {
            扫描指针 = 片段起始 + 1;
            continue;
        }

        if (!有独立容差) {
            容差值 = 默认容差;
        }

        if (!已有锚点) {
            锚点x = 点x;
            锚点y = 点y;
            已有锚点 = true;
        }

        if (解析结果->颜色长度 >= 最大颜色点数) {
            return 写入错误("找色颜色点数量超过 500");
        }

        const int 相对x = 点x - 锚点x;
        const int 相对y = 点y - 锚点y;
        const int 目标r = (颜色值 >> 16) & 0xff;
        const int 目标g = (颜色值 >> 8) & 0xff;
        const int 目标b = 颜色值 & 0xff;
        const int 容差r = (容差值 >> 16) & 0xff;
        const int 容差g = (容差值 >> 8) & 0xff;
        const int 容差b = 容差值 & 0xff;

        颜色比较项 比较项;
        比较项.偏移 = 使用转置点阵
                ? 相对x * 高度 + 相对y
                : 相对y * 宽度 + 相对x;
        比较项.r下界 = static_cast<int16_t>(目标r - 容差r);
        比较项.g下界 = static_cast<int16_t>(目标g - 容差g);
        比较项.b下界 = static_cast<int16_t>(目标b - 容差b);
        比较项.r跨度 = static_cast<uint16_t>(容差r * 2);
        比较项.g跨度 = static_cast<uint16_t>(容差g * 2);
        比较项.b跨度 = static_cast<uint16_t>(容差b * 2);

        if (解析结果->颜色长度 == 0) {
            解析结果->最小相对x = 相对x;
            解析结果->最大相对x = 相对x;
            解析结果->最小相对y = 相对y;
            解析结果->最大相对y = 相对y;
        } else {
            if (相对x < 解析结果->最小相对x) {
                解析结果->最小相对x = 相对x;
            }
            if (相对x > 解析结果->最大相对x) {
                解析结果->最大相对x = 相对x;
            }
            if (相对y < 解析结果->最小相对y) {
                解析结果->最小相对y = 相对y;
            }
            if (相对y > 解析结果->最大相对y) {
                解析结果->最大相对y = 相对y;
            }
        }

        解析结果->颜色项[解析结果->颜色长度] = 比较项;
        ++解析结果->颜色长度;
    }

    if (!已有锚点 || 解析结果->颜色长度 <= 0) {
        return 写入错误("找色颜色字符串没有有效颜色点");
    }

    if (解析结果->颜色长度 > 1) {
        std::sort(
                解析结果->颜色项,
                解析结果->颜色项 + 解析结果->颜色长度,
                [](const 颜色比较项& 左项, const 颜色比较项& 右项) {
                    const unsigned int 左项评分 = static_cast<unsigned int>(左项.r跨度)
                            + static_cast<unsigned int>(左项.g跨度)
                            + static_cast<unsigned int>(左项.b跨度);
                    const unsigned int 右项评分 = static_cast<unsigned int>(右项.r跨度)
                            + static_cast<unsigned int>(右项.g跨度)
                            + static_cast<unsigned int>(右项.b跨度);
                    return 左项评分 < 右项评分;
                }
        );
    }

    return true;
}

bool 修正查找范围(
        int 宽度,
        int 高度,
        const 已解析颜色& 解析结果,
        int& x1,
        int& y1,
        int& x2,
        int& y2
) {
    if ((0 - 解析结果.最小相对x) > x1) {
        x1 = 0 - 解析结果.最小相对x;
    }
    if ((0 - 解析结果.最小相对y) > y1) {
        y1 = 0 - 解析结果.最小相对y;
    }
    if ((x2 + 解析结果.最大相对x) > (宽度 - 1)) {
        x2 = 宽度 - 1 - 解析结果.最大相对x;
    }
    if ((y2 + 解析结果.最大相对y) > (高度 - 1)) {
        y2 = 高度 - 1 - 解析结果.最大相对y;
    }

    if (x1 > x2 || y1 > y2) {
        return 写入错误("找色范围为空");
    }
    return true;
}

bool 比较颜色项(
        const uint32_t* 点阵数据,
        const 颜色比较项* 颜色项,
        int 颜色长度,
        int 锚点索引
) {
    for (int 索引 = 0; 索引 < 颜色长度; ++索引) {
        const 颜色比较项& 比较项 = 颜色项[索引];
        const uint8_t* 像素通道 = reinterpret_cast<const uint8_t*>(
                &点阵数据[锚点索引 + 比较项.偏移]
        );

        if ((unsigned) ((int) 像素通道[0] - (int) 比较项.r下界) > (unsigned) 比较项.r跨度) {
            return false;
        }
        if ((unsigned) ((int) 像素通道[1] - (int) 比较项.g下界) > (unsigned) 比较项.g跨度) {
            return false;
        }
        if ((unsigned) ((int) 像素通道[2] - (int) 比较项.b下界) > (unsigned) 比较项.b跨度) {
            return false;
        }
    }
    return true;
}

bool 重建转置点阵(
        const uint32_t* 源点阵,
        int 宽度,
        int 高度,
        long long 帧编号
) {
    if (g转置帧编号 == 帧编号
            && g转置宽度 == 宽度
            && g转置高度 == 高度
            && g转置点阵 != nullptr) {
        return true;
    }

    const size_t 像素总数 = static_cast<size_t>(宽度) * static_cast<size_t>(高度);
    if (像素总数 > g转置容量) {
        void* 新缓冲 = std::realloc(g转置点阵, 像素总数 * sizeof(uint32_t));
        if (新缓冲 == nullptr) {
            return 写入错误("转置点阵内存不足");
        }
        g转置点阵 = static_cast<uint32_t*>(新缓冲);
        g转置容量 = 像素总数;
    }

    for (int y = 0; y < 高度; ++y) {
        const int 行起始 = y * 宽度;
        for (int x = 0; x < 宽度; ++x) {
            g转置点阵[x * 高度 + y] = 源点阵[行起始 + x];
        }
    }

    g转置宽度 = 宽度;
    g转置高度 = 高度;
    g转置帧编号 = 帧编号;
    return true;
}

bool 扫描颜色(
        const uint32_t* 点阵数据,
        int 宽度,
        int 高度,
        int x1,
        int y1,
        int x2,
        int y2,
        int 方向,
        const 已解析颜色& 解析结果,
        找色坐标* 坐标
) {
    const 颜色比较项* 颜色项 = 解析结果.颜色项;
    const int 颜色长度 = 解析结果.颜色长度;

    switch (方向) {
        case 1:
            for (int x = x1; x <= x2; ++x) {
                int 锚点索引 = x * 高度 + y1;
                for (int y = y1; y <= y2; ++y) {
                    if (比较颜色项(点阵数据, 颜色项, 颜色长度, 锚点索引)) {
                        坐标->x = x;
                        坐标->y = y;
                        return true;
                    }
                    ++锚点索引;
                }
            }
            break;
        case 2:
            for (int y = y1; y <= y2; ++y) {
                int 锚点索引 = y * 宽度 + x1;
                for (int x = x1; x <= x2; ++x) {
                    if (比较颜色项(点阵数据, 颜色项, 颜色长度, 锚点索引)) {
                        坐标->x = x;
                        坐标->y = y;
                        return true;
                    }
                    ++锚点索引;
                }
            }
            break;
        case 3:
            for (int y = y1; y <= y2; ++y) {
                int 锚点索引 = y * 宽度 + x2;
                for (int x = x2; x >= x1; --x) {
                    if (比较颜色项(点阵数据, 颜色项, 颜色长度, 锚点索引)) {
                        坐标->x = x;
                        坐标->y = y;
                        return true;
                    }
                    --锚点索引;
                }
            }
            break;
        case 4:
            for (int x = x2; x >= x1; --x) {
                int 锚点索引 = x * 高度 + y1;
                for (int y = y1; y <= y2; ++y) {
                    if (比较颜色项(点阵数据, 颜色项, 颜色长度, 锚点索引)) {
                        坐标->x = x;
                        坐标->y = y;
                        return true;
                    }
                    ++锚点索引;
                }
            }
            break;
        case 5:
            for (int x = x2; x >= x1; --x) {
                int 锚点索引 = x * 高度 + y2;
                for (int y = y2; y >= y1; --y) {
                    if (比较颜色项(点阵数据, 颜色项, 颜色长度, 锚点索引)) {
                        坐标->x = x;
                        坐标->y = y;
                        return true;
                    }
                    --锚点索引;
                }
            }
            break;
        case 6:
            for (int y = y2; y >= y1; --y) {
                int 锚点索引 = y * 宽度 + x2;
                for (int x = x2; x >= x1; --x) {
                    if (比较颜色项(点阵数据, 颜色项, 颜色长度, 锚点索引)) {
                        坐标->x = x;
                        坐标->y = y;
                        return true;
                    }
                    --锚点索引;
                }
            }
            break;
        case 7:
            for (int y = y2; y >= y1; --y) {
                int 锚点索引 = y * 宽度 + x1;
                for (int x = x1; x <= x2; ++x) {
                    if (比较颜色项(点阵数据, 颜色项, 颜色长度, 锚点索引)) {
                        坐标->x = x;
                        坐标->y = y;
                        return true;
                    }
                    ++锚点索引;
                }
            }
            break;
        case 8:
            for (int x = x1; x <= x2; ++x) {
                int 锚点索引 = x * 高度 + y2;
                for (int y = y2; y >= y1; --y) {
                    if (比较颜色项(点阵数据, 颜色项, 颜色长度, 锚点索引)) {
                        坐标->x = x;
                        坐标->y = y;
                        return true;
                    }
                    --锚点索引;
                }
            }
            break;
        default:
            return 写入错误("找色方向必须是 1 到 8");
    }

    return 写入错误("未找到匹配颜色");
}

} // namespace

bool 在点阵中多点找色(
        const unsigned char* pixels,
        int width,
        int height,
        long long frameId,
        int x1,
        int y1,
        int x2,
        int y2,
        int dir,
        int sim,
        const char* colors,
        找色坐标* point
) {
    写入未找到坐标(point);

    if (point == nullptr) {
        return 写入错误("找色输出坐标为空");
    }
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return 写入错误("找色点阵为空");
    }

    bool 使用转置点阵 = 是否转置方向(dir);
    已解析颜色 解析结果;
    if (!解析颜色字符串(colors, width, height, 使用转置点阵, sim, &解析结果)) {
        return false;
    }
    if (!修正查找范围(width, height, 解析结果, x1, y1, x2, y2)) {
        return false;
    }

    const uint32_t* 当前点阵 = reinterpret_cast<const uint32_t*>(pixels);
    if (使用转置点阵) {
        if (!重建转置点阵(当前点阵, width, height, frameId)) {
            return false;
        }
        当前点阵 = g转置点阵;
    }

    bool 找到 = 扫描颜色(
            当前点阵,
            width,
            height,
            x1,
            y1,
            x2,
            y2,
            dir,
            解析结果,
            point
    );
    if (找到) {
        g最近错误.clear();
    }
    return 找到;
}

bool 在屏幕中多点找色(
        int x1,
        int y1,
        int x2,
        int y2,
        int dir,
        int sim,
        const char* colors,
        找色坐标* point
) {
    写入未找到坐标(point);

    ScreenFrame frame;
    if (!captureScreen(&frame)) {
        g最近错误 = screenLastError();
        return false;
    }

    return 在点阵中多点找色(
            frame.pixels,
            frame.width,
            frame.height,
            frame.frameId,
            x1,
            y1,
            x2,
            y2,
            dir,
            sim,
            colors,
            point
    );
}

void 清空找色缓存() {
    std::free(g转置点阵);
    g转置点阵 = nullptr;
    g转置容量 = 0;
    g转置宽度 = 0;
    g转置高度 = 0;
    g转置帧编号 = -1;
    g最近错误.clear();
}

std::string 取找色错误() {
    return g最近错误;
}

} // namespace xiaoyv::api
