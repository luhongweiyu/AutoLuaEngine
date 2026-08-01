# 0012：Android 扩展目录包与相对路径

- 状态：已接受（更新 0011 的平铺单文件导入规则）
- 日期：2026-08-01

## 背景

OpenCV 依赖两个 native 库，RapidOCR 同时需要运行库、三个 ONNX 模型和字典；YOLO 后续也会同时携带
运行库、模型和标签。把每个文件平铺在扩展目录并逐个导入，既难以识别一个能力是否完整，也会让用户反复
点击导入。

## 决定

1. `/sdcard/xiaoyv/extensions/` 只枚举最外层的普通文件和目录，不递归显示其内容。单个文件仍可单独导入；
   目录视为一个扩展包。
2. 导入目录时递归复制其完整内容到 App 私有只读副本，并保留内部相对路径。同名目录再次导入时整体替换旧
   副本，不能与旧内容合并，避免删除过的模型或依赖残留。
3. 运行时通过相对导入路径查找已导入文件；路径不得越出扩展包根目录。默认包固定使用：

   ```text
   yolo/libxiaoyv_yolo.so
   opencv/libc++_shared.so
   opencv/libopencv_java4.so
   rapidocr/libonnxruntime.so
   rapidocr/ch_PP-OCRv4_det_mobile.onnx
   rapidocr/ch_PP-OCRv4_rec_mobile.onnx
   rapidocr/ch_ppocr_mobile_v2.0_cls_mobile.onnx
   rapidocr/ppocr_keys_v1.txt
   ```

4. App 不校验扩展包名称、后缀、签名、哈希、版本、ABI、内容或依赖，也不在导入时执行 native 代码。
   路径边界检查仅防止复制或读取越出用户选定的扩展目录，不构成兼容性校验。
5. 只有脚本运行期间实际请求功能时，才按需加载已导入的 native 文件。ALPKG 仍不承载这些运行时或模型。

## 后果

- 扩展页把默认 OpenCV、RapidOCR 和 YOLO 分别显示为一个目录条目，用户一次导入一个完整包。
- 旧版已导入的平铺默认运行时不会被自动迁移；用户将默认包按新目录结构放入共享扩展目录后重新导入即可。
- 用户自定义扩展仍可使用单文件，或自行定义目录包；其实际调用方式另由对应公开 API 决定。
