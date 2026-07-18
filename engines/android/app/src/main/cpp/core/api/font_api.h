/**
 * 文件用途：声明点阵字库核心 API，统一提供完整识字、完整找字和目标字形快速找字。
 */
#pragma once

#include <string>

namespace xiaoyv::api {

/** 找字命中坐标。未找到或失败时 x/y 为 -1。 */
struct 字库坐标 {
    int x = -1;
    int y = -1;
};

/** 替换指定索引的字库。dictionary 可为文本、普通文件路径或当前 ALPKG 包内资源路径。 */
bool 设置字库(int index, const char* dictionary);

/** 向指定索引追加字库内容。 */
bool 追加字库(int index, const char* dictionary);

/** 选择当前线程要使用的字库索引。 */
bool 使用字库(int index);

/**
 * 清空当前脚本任务加载的全部点阵字库，并把当前调用线程恢复到默认索引 0。
 *
 * 这是 Engine 统一任务收尾使用的内部入口，不通过 C ABI 或语言绑定公开。
 */
void 清空全部字库();

/**
 * 从当前截图生成一个可写入新字库的点阵描述，返回 "宽$高$十六进制点阵"。
 *
 * 输出会自动裁去纯背景边缘。11 位仅是内部候选索引宽度，字形仍保存完整宽高点阵。
 */
bool 获取字形点阵(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* color,
        std::string* fontPixel
);

/** 在当前截图指定区域内按当前字库识字，成功返回结构化 JSON。 */
bool 点阵识字(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* color,
        double similarity,
        std::string* resultJson
);

/** 在当前截图指定区域内查找一段文字，返回第一个命中坐标。 */
bool 点阵找字(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double similarity,
        字库坐标* point
);

/** 在当前截图指定区域内查找所有文字命中，成功返回结构化 JSON 数组。 */
bool 点阵找字全部(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double similarity,
        std::string* resultJson
);

/** 只识别目标文字涉及的字形并返回第一个命中；适合大字库中的固定文字查找。 */
bool 点阵快速找字(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double similarity,
        字库坐标* point
);

/** 只识别目标文字涉及的字形并返回全部命中坐标 JSON 数组。 */
bool 点阵快速找字全部(
        int x1,
        int y1,
        int x2,
        int y2,
        const char* text,
        const char* color,
        double similarity,
        std::string* resultJson
);

/** 返回当前线程最近一次字库 API 失败原因。 */
std::string 取字库错误();

} // namespace xiaoyv::api
