/**
 * 文件用途：声明可选 YOLO 运行时的语言无关核心 API，供稳定 C ABI、Lua/JS/Go 绑定复用。
 */
#pragma once

#include <string>

namespace xiaoyv::api {

/** 查询已导入的可选 YOLO 运行时信息；未导入不是错误，JSON 中 available 为 false。 */
bool 获取YOLO运行时信息(std::string* resultJson);

/** 查询 yolo/libxiaoyv_yolo.so 是否已经导入并可尝试加载。 */
bool YOLO运行时可用(bool* available);

/**
 * 加载普通文件路径上的 NCNN YOLOv5 模型。
 *
 * labelsPath、paramPath、binPath 都不能来自 ALPKG；相对路径基于当前脚本工作目录解析。
 * optionsJson 可设置 input、outputs（三个 blob 名称）和 useGpu（当前 CPU 版会明确拒绝 true）。
 */
bool 加载YOLO模型(
        const char* name,
        const char* labelsPath,
        const char* paramPath,
        const char* binPath,
        const char* optionsJson
);

/** 释放指定名称的模型；模型不存在时 released 为 false，但不视为调用失败。 */
bool 释放YOLO模型(const char* name, bool* released);

/** 查询指定名称的模型是否已加载。 */
bool YOLO模型已加载(const char* name, bool* loaded);

/**
 * 使用一次原子拷贝的当前屏幕 RGBA 帧进行检测。
 *
 * 区域采用左闭右开坐标；left/top/right/bottom 全为 0 时检测整张屏幕。结果 JSON 是
 * {"items":[{"x","y","w","h","label","prob"}]}，坐标相对完整屏幕。
 */
bool 检测当前屏幕YOLO(
        const char* name,
        int left,
        int top,
        int right,
        int bottom,
        const char* optionsJson,
        std::string* resultJson
);

/** 解码一张普通图片文件后检测；相对路径基于脚本工作目录，结果坐标相对该图片。 */
bool 检测图片YOLO(
        const char* name,
        const char* imagePath,
        const char* optionsJson,
        std::string* resultJson
);

/** 返回当前线程最近一次 YOLO 核心 API 失败原因。 */
std::string 取YOLO错误();

} // namespace xiaoyv::api
