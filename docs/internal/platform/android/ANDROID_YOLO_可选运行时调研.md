# Android YOLO 可选运行时调研

- 状态：CPU 可选运行时、内部 C ABI 与本地扩展导入链路已实现；公开脚本映射尚未决定
- 日期：2026-08-01

本文保存已核对的来源、运行时装配和待用户决定的公开层边界，避免后续重复逆向或重新追查。
它不是公开脚本 API 契约；用户函数形状应在确定后单独加入公开文档。

## 当前结论

1. `libengine.so` 当前始终带有版本化 `EngineYoloApi` / `engine_yolo*` 内部 C ABI；它通过
   Java `YoloPlatformBridge` 查询和桥接可选 `libxiaoyv_yolo.so`。Lua `m.yolo`、`YoloV5` 兼容入口
   和 JS / Go 的公开映射尚未导出。
2. 用户已确定 ALPKG 只面向脚本和小型资源；YOLO 的 `param`、`bin`、标签等运行所需文件一律
   使用普通文件路径，不作为 ALPKG 输入，也不增加临时解包、复制或清理流程。现有 ALPKG 运行时
   不解压，且本来也不能直接作为 Android native linker 的库目录。
3. 推理后端已单独为 `libxiaoyv_yolo.so`，静态链接官方 NCNN 的 CPU 运行时；基础 APK 永不打包
   该库。用户把文件放到 `/sdcard/xiaoyv/extensions/yolo/` 并在扩展页导入 `yolo` 目录后，只有模型加载或
   检测真正发生时才按需加载它；查询运行时状态不执行 native 代码。
4. `tools/build_android_yolo.ps1` 负责单独编译并复用 SO。Gradle 不会下载、编译或打包 NCNN/YOLO；
   App 不校验导入文件的签名、哈希、版本、ABI、文件名或依赖，加载错误由用户自行处理。
5. 小鱼精灵是多语言平台，所有语言绑定和 native 插件都建立在 `EngineYoloApi` 之上；它不要求
   Lua、JS 或 Go 各自装载可选库。

## 当前验证状态

- 2026-07-30：已在 x86_64 独立构建 `libxiaoyv_yolo.so`，剥离调试符号后约 13.6 MB；动态依赖仅为
  Android 系统的 `liblog.so`、`libm.so`、`libdl.so`、`libc.so`，不携带额外 `libncnn.so`。
- 2026-07-30 曾验证旧的“构建时打进 APK”路径；该路径已被 0011 取代，不能作为当前分发行为依据。
- 2026-08-01 已构建新的基础 APK，确认其中不含 `libxiaoyv_yolo.so`；本地扩展页、导入副本和脚本
  按需加载仍需在设备上以实际 SO 继续验收。
- 尚未对具体 `labels + param + bin` 模型做端到端检测验收：公开模型版本、blob 名称和最终公开语言
  调用形状尚待用户决定。因此当前只能称运行时、C ABI 与打包链路已验证，不能宣称某个模型的识别
  效果已经验收。

## 已核对的旧实现

旧项目恢复目录中的
`T:\老项目\recovered_project\script_1\jadx_java\sources\com\p000nx\assist\lua\YoloV5.java`
包含：

```java
static boolean init(String result, String param, String bin)
static String detect(Bitmap bitmap, boolean useGpu)
```

- `init` 懒加载两份 native 库后调用 `nativeInit(Obj.class, result, param, bin)`；
  `detect` 调用 `nativeDetect(bitmap, useGpu)`，再把每个对象的 `x`、`y`、`w`、`h`、`label`、`prob`
  序列化为 JSON 数组。
- 恢复 APK 的 `libyolo.so` 导出了 `nativeInit`、`nativeDetect` 和 `YoloV5Focus`；其依赖包含
  `libncnn.so`、`libopencv_java4.so`、`libc++_shared.so` 与 Android 图形库。模型参数/权重不在 APK
  内，由脚本传入文件路径。
- 这证明旧实现是 **NCNN + YOLOv5 参数/权重文件 + Android Bitmap** 路线，但不要求新实现复刻
  `libopencv_java4.so` 依赖；若新后端只需 Bitmap/RGBA，可优先使用 NDK `AndroidBitmap` / RGBA 入口，
  避免把 OpenCV 作为 YOLO 的额外运行时依赖。

懒人精灵的 [图色文档](http://www.lrappsoft.com/lrword/api/android/color.html#_2-yolov5-detect-yolov5%E7%9B%AE%E6%A0%87%E6%A3%80%E6%B5%8B-yolov5-detect)
当前正文将标题写为 `YoloV5.detect`，示例使用：

```lua
YoloV5.init(result, param, bin)
YoloV5.detect(bmp, false)
```

但函数行写成了 `LuaEngine.detect(bmp,usegpu)`，存在文档命名不一致。因此它可作为兼容层形状的
参考，不能直接决定小鱼正式 `m` API。

## 当前内部装配方式

```text
用户文件 /sdcard/xiaoyv/extensions/yolo/libxiaoyv_yolo.so
  -> App 扩展页导入 yolo 目录（复制为私有只读副本，不加载）
  -> 脚本中的 YOLO 模型加载 / 检测请求
  -> libengine.so 的版本化 EngineYoloApi
  -> 私有 YoloRuntimeBridge
  -> libxiaoyv_yolo.so（按需加载，NCNN 静态链接）
  -> 普通文件系统中的模型 param / bin / 标签资源
```

- `libxiaoyv_yolo.so` 静态链接 NCNN，使用户可选项只需一份功能库，而不是旧实现那样要求
  `libyolo.so + libncnn.so` 成对存在。
- 由独立 PowerShell 构建脚本产出 SO；它是本地可复用构建缓存，Gradle 从不参与其编译或打包。
- 扩展页接受最外层的任意普通文件或目录并保留目录内部相对路径。YOLO 运行时固定请求
  `yolo/libxiaoyv_yolo.so`；改名、放错 ABI 或缺依赖时，按需加载直接返回错误，不在导入阶段拦截。
- `EngineYoloApi` 保留可用性查询：`available` 表示该相对路径文件已导入、可以尝试，`loaded` 表示当前
  引擎进程已实际加载。load/detect 失败通过同一 C ABI 错误文本返回，各语言绑定不各自处理 linker。
- GPU 参数应保留为可选能力：先实现并验收 CPU，再在设备支持时启用 NCNN Vulkan；不支持或初始化
  失败时必须明确回退或报错，不能把 `useGpu=true` 静默当作 GPU 成功。

## 用户待决定的公开层

1. Lua、JS、Go 如何在 `EngineYoloApi` 之上提供各自自然的公开调用；内部 C ABI 不能反向约束
   `m` 的命名和参数形状。
2. Lua 是否同时提供 `YoloV5` 兼容类；如提供，是否只作为迁移适配，而不替代 `m.yolo` 的
   推荐用法。
3. 模型管理采用旧的全局单模型流程（`init` / `detect`），还是由语言中立层按名称管理多个模型；
   前者迁移成本最低，后者更适合多模型、跨语言统一和显式释放。
4. GPU 控制的未来形态；检测阈值、NMS 阈值、线程数与检测区域当前已作为内部 `options`，公开层
   是否直接暴露仍需由跨语言契约、旧项目与懒人/触动文档共同决定。

在上述选择确定前，不得把 YOLO 加入公开函数目录或 `catalog.json`；内部 C ABI 契约以
`API_契约.md` 为准。
