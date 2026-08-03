# RapidOCR 可选资源

这里保存 PP-OCRv4 mobile 的本地可选分发文件；它们不进入 Android 基础 APK、ALPKG 或主仓库。
仓库只保留来源和许可证说明；模型由发布包或开发者本地资源提供。

设备侧将完整运行时包放入 `/sdcard/xiaoyv/extensions/rapidocr/`：

- `libonnxruntime.so`（按目标 CPU ABI 取对应文件）
- `ch_PP-OCRv4_det_mobile.onnx`
- `ch_PP-OCRv4_rec_mobile.onnx`
- `ch_ppocr_mobile_v2.0_cls_mobile.onnx`
- `ppocr_keys_v1.txt`

随后在 App 的“扩展”页导入一次 `rapidocr` 顶层目录。App 会递归复制整个目录并保留这些
固定相对路径，不需要逐个导入文件。

脚本调用 `m.ocr.loadBuiltin`（以及现有 `PaddleOcr` 的内置 ONNX 兼容入口）时，才会加载已导入的
`libonnxruntime.so` 并打开这四个固定名称的模型文件。App 不在导入时执行 native 代码，也不判断
文件内容、ABI 或依赖是否正确；加载失败由 Android linker 或 ONNX Runtime 原样报出。

`libonnxruntime4j_jni.so` 是 ONNX Runtime 官方 Java 绑定所需的小型 JNI 壳，基础 APK 保留它，以便
Java API 在不修改 Android 类加载器搜索路径的前提下连接到按需导入的核心运行时。真正占空间的
`libonnxruntime.so` 和本目录中的模型均不打入基础 APK。
