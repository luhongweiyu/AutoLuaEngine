/**
 * 文件用途：声明 RapidOCR 模型管理与图片识别核心 API，供 C ABI 和各脚本语言绑定复用。
 */
#pragma once

#include <string>

namespace xiaoyv::api {

/**
 * 加载 APK 内置的中文 PP-OCRv4 mobile 模型。
 *
 * name 是脚本侧模型名称；模型资源由 Android 平台首次加载时准备到应用私有目录。
 */
bool 加载内置OCR模型(const char* name, int threads);

/**
 * 加载一组 RapidOCR PP-OCR ONNX 模型。
 *
 * name 是脚本侧模型名称；det/rec/keys 必填，cls 可为空。相同名称且同配置重复加载会直接
 * 复用，名称已绑定不同配置时必须先 release，避免运行中隐式替换模型。
 */
bool 加载OCR模型(
        const char* name,
        const char* detPath,
        const char* recPath,
        const char* clsPath,
        const char* keysPath,
        int threads
);

/** 释放指定模型名称持有的引用；没有其他名称引用时关闭 OrtSession。 */
bool 释放OCR模型(const char* name, bool* released);

/** 查询指定模型名称是否已加载。 */
bool OCR模型已加载(const char* name, bool* loaded);

/**
 * 识别图片文件，成功时 resultJson 返回 {"items":[...]}。
 *
 * optionsJson 是 JSON 对象，可传 threshold、boxThreshold、maxSideLen、padding、useAngle、
 * minScore 等 RapidOCR 参数；为空时使用默认值。
 */
bool 识别图片文字(
        const char* name,
        const char* imagePath,
        const char* optionsJson,
        std::string* resultJson
);

/** 在图片识别结果中查找指定文字，成功时 resultJson 返回 found/x/y/w/h/text/score。 */
bool 在图片中找文字(
        const char* name,
        const char* imagePath,
        const char* text,
        const char* optionsJson,
        std::string* resultJson
);

/** 返回当前线程最近一次 OCR API 失败原因。 */
std::string 取OCR错误();

} // namespace xiaoyv::api
