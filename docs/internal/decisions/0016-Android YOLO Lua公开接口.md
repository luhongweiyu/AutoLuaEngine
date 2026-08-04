# 0016：Android YOLO Lua 公开接口

- 状态：已接受
- 日期：2026-08-03
- 更新：[0010：Android 可选 YOLO 运行时与多语言 C ABI](0010-Android可选YOLO运行时与多语言C-ABI.md)中的 Lua 公开层待定项

## 背景

`EngineYoloApi`、`core/api/yolo_api`、可选 `libxiaoyv_yolo.so` 和目录导入链路已经存在，但
Lua 尚无正式入口。懒人精灵提供 `YoloV5.init(labels, param, bin)`、
`YoloV5.detect(Bitmap, useGpu)` 和 JSON 数组结果；本项目底层同时支持命名模型、显式释放、
当前屏幕与图片文件检测，不能为了复刻一个 Java 包装类而丢失这些能力。

## 决定

1. 正式 Lua 入口为 `m.yolo`，提供 `runtimeInfo`、`isAvailable`、`load`、`release`、
   `isLoaded`、`detectScreen` 和 `detectFile`。不新增 Java `YoloV5` 包装类，也不新增全局
   `YoloV5`；JS、Go 绑定以后在同一 `EngineYoloApi` 上单独设计。
2. 额外提供默认模型简化入口：`init(labelsPath, paramPath, binPath[, loadOptions])` 等价于
   `load("default", ...)`；`detect()`、`detect(detectOptions)` 或
   `detect(imagePath[, detectOptions])` 始终使用 `default`。同名不同配置不会暗中替换，必须先
   `release()`。
3. `init` 沿用懒人精灵便于记忆的 labels、param、bin 参数顺序；检测成功直接返回 Lua 数组，
   每项为 `{x, y, w, h, label, prob}`，不要求脚本再次解析 JSON。Lua 简化入口接收当前屏幕或
   普通图片路径，不承诺接收 Java `Bitmap`。
4. 模型加载选项与单次检测选项分开：加载配置为 `input`、按 stride 8/16/32 排列的三个
   `outputs` 和 `useGpu`；检测配置为 `targetSize`、`threads`、`probThreshold`、
   `nmsThreshold`。GPU 属于模型配置，当前 CPU 运行时对 `useGpu=true` 明确报错，检测调用不能
   静默切换 GPU。
5. labels、param、bin 和待检测图片仍是普通文件；相对路径基于当前脚本工作目录解析，ALPKG
   边界不变。
6. `m.yolo` 只属于正式 `m`。bootstrap 在 `lr`、`cd` 完成各自成员复制后再加载 YOLO 模块，
   不能把新入口自动泄漏为尚未统一设计的 `lr.yolo` 或 `cd.yolo`。

## 后果

- Lua 可以直接完成运行时查询、模型加载、屏幕/图片检测和释放；插件仍可使用更底层的完整 C ABI。
- 当前后端只兼容标准 YOLOv5 NCNN 三检测头及固定 anchors，不代表任意 YOLO 或任意 NCNN 模型
  都能直接加载；模型 blob 名称由加载选项显式调整。
- 旧 `YoloV5` 只作为接口调研来源，不构成兼容承诺。若以后需要 Java Bitmap 入口、GPU/Vulkan
  或 JS/Go 绑定，必须在不破坏现有 `m.yolo` 语义的前提下另行扩展。
