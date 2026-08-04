# Android YOLO 可选运行时调研

- 状态：CPU 可选运行时、内部 C ABI、本地扩展导入链路与 Lua `m.yolo` 已实现；JS、Go 后续
- 日期：2026-08-04

本文保存已核对的来源、运行时装配和待用户决定的公开层边界，避免后续重复逆向或重新追查。
它不是公开脚本 API 契约；用户函数形状应在确定后单独加入公开文档。

## 当前结论

1. `libengine.so` 当前始终带有版本化 `EngineYoloApi` / `engine_yolo*` 内部 C ABI；它通过
   Java `YoloPlatformBridge` 查询和桥接可选 `libxiaoyv_yolo.so`。Lua 已提供正式 `m.yolo`；
   Java/全局 `YoloV5`、`lr/cd` 兼容入口和 JS / Go 映射没有导出。
2. 用户已确定 ALPKG 只面向脚本和小型资源；YOLO 的 `param`、`bin`、标签等运行所需文件一律
   使用普通文件路径，不作为 ALPKG 输入，也不增加临时解包、复制或清理流程。现有 ALPKG 运行时
   不解压，且本来也不能直接作为 Android native linker 的库目录。
3. 推理后端已单独为 `libxiaoyv_yolo.so`，静态链接官方 NCNN 的 CPU 运行时；基础 APK 永不打包
   该库。用户把文件放到 `/sdcard/xiaoyv/extensions/yolo/` 并在扩展页导入 `yolo` 目录后，只有模型加载或
   检测真正发生时才按需加载它；查询运行时状态不执行 native 代码。
4. `tools/build_android_yolo.ps1` 负责单独编译并复用 SO。Gradle 不会下载、编译或打包 NCNN/YOLO；
   App 不校验导入文件的签名、哈希、版本、ABI、文件名或依赖，加载错误由用户自行处理。
5. 小鱼精灵是多语言平台，Lua 绑定和 native 插件都建立在 `EngineYoloApi` 之上；后续 JS、Go
   也应复用同一入口，不各自装载可选库。

## 当前验证状态

- 2026-07-30：已在 x86_64 独立构建 `libxiaoyv_yolo.so`，剥离调试符号后约 13.6 MB；动态依赖仅为
  Android 系统的 `liblog.so`、`libm.so`、`libdl.so`、`libc.so`，不携带额外 `libncnn.so`。
- 2026-07-30 曾验证旧的“构建时打进 APK”路径；该路径已被 0011 取代，不能作为当前分发行为依据。
- 2026-08-04 已构建并安装 x86_64 基础 APK，确认 APK 不含 `libxiaoyv_yolo.so`；通过原有扩展页
  导入同 ABI 的独立 SO 后，Root Worker 已验证按需加载。运行时报告 NCNN `1.0.20260730`。
- 2026-08-04 已用固定提交
  [`shaoshengsong/yolov5_62_export_ncnn@eb943dff`](https://github.com/shaoshengsong/yolov5_62_export_ncnn/tree/eb943dff15ec6239673d6a5dcfb482d22711ab1d)
  的 `bus.jpg`、标签和模型文件完成 x86_64 设备端真实推理：默认输入 blob 为 `images`，输出
  blob 为 `output/353/367`，返回 6 个结果并包含 `bus` 与 `person`。模型仓库未明确声明权重
  许可，仅用于本地临时验收；模型文件不得提交、打包或随产品分发。
- 上述验收覆盖整图图片检测、Lua 解包和 `release()`；检测区域坐标尚未用真实模型做独立设备验收，
  因此区域语义仍只以代码和契约为依据，不能标成已完成的端到端能力。

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

2026-08-03 再次核对该在线页面，当前完整示例还确认了以下调用语义：

- 脚本先 `import("com.nx.assist.lua.LuaEngine")` 和
  `import("com.nx.assist.lua.YoloV5")`。
- `YoloV5.init(result, param, bin)` 的 `result` 示例为 `result.txt`，实际承担标签文件路径；
  现有内部 C ABI 将它命名为 `labelsPath`，但这不预先决定后续 Lua 公开参数名。
- `YoloV5.detect(bitmap, false)` 接收 Java `Bitmap`，返回可由 JSON 库解码的数组字符串；调用方
  负责回收截图 Bitmap。
- 页面没有定义多模型、释放模型、检测文件、检测区域、阈值、线程、错误文本和 GPU 不可用时的
  完整语义，且标题与函数行互相矛盾。

因此这份文档用于确定 `init(labels, param, bin)` 的易记参数顺序和 `x/y/w/h/label/prob` 结果字段，
但不直接复制 Bitmap、检测时 GPU 开关或 JSON 字符串返回。小鱼正式入口保留 `EngineYoloApi` 已有
的命名模型、释放、屏幕/文件检测和错误语义；没有新增 Java `YoloV5` 包装类。

## 当前内部装配方式

```text
用户文件 /sdcard/xiaoyv/extensions/yolo/libxiaoyv_yolo.so
  -> App 扩展页导入 yolo 目录（复制为私有只读副本，不加载）
  -> Worker 中 m.yolo、插件或后续语言绑定发起 YOLO 模型加载 / 检测请求
  -> libengine.so 的版本化 EngineYoloApi
  -> 私有 YoloPlatformBridge
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
- 已加载模型保存在本次一次性 Worker 进程中；脚本应在不再使用时显式 `release` 以尽早释放内存，
  即使遗漏也不会跨 Worker 继承，进程退出后由系统统一回收。
- load/detect 是释放 Lua VM Gate 后执行的同步 native 调用，其他 Lua 任务可以继续取得 Gate；普通
  协作停止不会中断正在进行的一次加载或推理，最迟在 native 返回或控制端强制回收 Worker 时结束。
- GPU 参数应保留为可选能力：先实现并验收 CPU，再在设备支持时启用 NCNN Vulkan；不支持或初始化
  失败时必须明确回退或报错，不能把 `useGpu=true` 静默当作 GPU 成功。

## Lua 公开层与剩余边界

Lua 形状由 [0016：Android YOLO Lua 公开接口](../../decisions/0016-Android%20YOLO%20Lua公开接口.md)
固定：高级入口按名称管理多个模型，`init/detect` 使用 `default` 简化常见流程；检测返回 Lua 数组，
相对文件路径基于脚本工作目录。加载 options 与检测 options 分离，当前 CPU 后端对 GPU 请求明确
报错。模块在 `lr/cd` 加载后才挂到 `m`，不自动扩展两套兼容命名空间。

仍未完成的范围只有：

1. 使用真实模型对检测区域坐标做独立设备端验收。
2. JS、Go 各自自然的语言绑定。
3. GPU/Vulkan 后端、设备能力探测和加载/检测语义；不能把 `useGpu=true` 静默当作 CPU 成功。
4. 若以后确实需要 Java Bitmap 或旧全局 `YoloV5` 迁移层，再单独定义对象生命周期和兼容范围。

内部 C ABI 契约以 `API_契约.md` 为准，用户 Lua 用法以公开 YOLO 函数页为准。
