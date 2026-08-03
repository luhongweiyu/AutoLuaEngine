# Android 可选 native 运行时缓存

这个目录只保存本地可复用的预编译产物，不提交二进制文件。

- `yolo/<abi>/libxiaoyv_yolo.so`：由 `tools/build_android_yolo.ps1` 编译。
- `opencv/<abi>/libc++_shared.so`、`opencv/<abi>/libopencv_java4.so`：由
  `tools/prepare_android_optional_runtimes.ps1` 从项目锁定版本的 OpenCV AAR 提取。
- `rapidocr/<abi>/libonnxruntime.so`：由同一脚本从项目锁定版本的 ONNX Runtime AAR 提取。

设备分发时只去掉 ABI 这一层，按能力目录放入共享扩展目录：

```text
/sdcard/xiaoyv/extensions/yolo/libxiaoyv_yolo.so
/sdcard/xiaoyv/extensions/opencv/libc++_shared.so
/sdcard/xiaoyv/extensions/opencv/libopencv_java4.so
/sdcard/xiaoyv/extensions/rapidocr/libonnxruntime.so
```

RapidOCR 的模型和字典也放在同一个 `rapidocr/` 目录。随后在 App“扩展”页分别导入
`yolo`、`opencv` 或 `rapidocr` 顶层目录；App 会递归复制整个目录并保留内部相对路径。
