# 0010：Android 可选 YOLO 运行时与多语言 C ABI

- 状态：部分被 0011 取代
- 日期：2026-07-30
- 更新：[0016：Android YOLO Lua 公开接口](<0016-Android YOLO Lua公开接口.md>)已确定 Lua 公开层；
  JS、Go 与旧 `YoloV5` 兼容入口仍不属于本记录。

## 背景

旧 Android 项目使用 `YoloV5.init(result, param, bin)` 和 `YoloV5.detect(bitmap, useGpu)`，其
native 后端是 NCNN YOLOv5，模型文件由外部路径传入。小鱼精灵同时面向 Lua、JS、Go 与 native
插件，不能把旧 Java/Lua 兼容形状直接当成所有语言的底层能力；同时 YOLO 对不使用它的用户不应
增大基础 APK。

## 取代说明

[0011：Android 本地扩展文件导入与按需加载](0011-Android本地扩展文件导入与按需加载.md)
已取代本记录中“通过 Gradle 选择性把 YOLO SO 打入 APK、禁止从本地导入 native SO”的分发方式。
本记录关于独立 `libxiaoyv_yolo.so`、NCNN CPU 后端和语言中立 `EngineYoloApi` 的边界仍然有效。

## 决定

1. `libengine.so` 始终提供版本化的 `EngineYoloApi` 和直接 `engine_yolo*` C ABI。该层是多语言
   绑定及 native 插件的共同入口，不等同于最终 Lua `m` 的函数形状。
2. 推理后端单独构建为 `libxiaoyv_yolo.so`：Java 的 `YoloPlatformBridge` 按需加载它，SO 内静态
   链接 CPU 版 NCNN。基础 APK 不带该库时，查询返回 `available:false`；加载或检测返回明确错误，
   不影响其他引擎能力。
3. `libxiaoyv_yolo.so` 只能由 `tools/build_android_yolo.ps1` 单独生成并复用。Gradle 的
   `-PxiaoyvEnableYolo=true` 只验证并打包已经存在的 ABI SO，绝不在 APK 构建时下载或重新编译
   NCNN / YOLO。
4. `labels`、`param`、`bin` 和待检测图片均使用普通文件系统路径，延续 0009 的 ALPKG 边界。
   App 日后可以下载、校验和管理这些模型文件；它们不进入 ALPKG。
5. 第一阶段仅启用 CPU。请求 `useGpu=true` 必须明确失败，不能静默伪装为 GPU 推理。GPU/Vulkan
   需在独立决策、设备验证和体积评估后再加入。

## 后果

- 同一源码可以产出基础 APK 或含 YOLO SO 的 APK；后者之后可在 App 内下载模型文件而无需再构建
  APK。模型下载应另行定义来源、校验、版本和删除策略。
- 不把 native `.so` 当作普通下载资源动态装载。若未来希望在基础 APK 安装后再增加原生 YOLO
  运行时，应采用同签名的拆分 APK / 动态功能模块等受 Android 包管理器控制的分发方式，而不是
  让应用从任意目录 `System.load` 下载库。
- Lua `m.yolo` 已由 0016 单独确定；旧 `YoloV5` 兼容入口、JS 和 Go 的公开映射仍待后续决定。
  本记录继续只固定可复用的运行时装配和 C ABI 边界。
