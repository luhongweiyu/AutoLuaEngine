# Android 可选 native 运行时缓存

这个目录只保存本地可复用的预编译产物，不提交二进制文件。

- `yolo/<abi>/libxiaoyv_yolo.so`：由 `tools/build_android_yolo.ps1` 编译。
- `opencv/<abi>/libc++_shared.so`、`opencv/<abi>/libopencv_java4.so`：由
  `tools/prepare_android_optional_runtimes.ps1` 从项目锁定版本的 OpenCV AAR 提取。
- `rapidocr/<abi>/libonnxruntime.so`：由同一脚本从项目锁定版本的 ONNX Runtime AAR 提取。

设备分发时不保留这里的分类与 ABI 目录：只取目标 ABI 的文件，平铺复制到
`/sdcard/xiaoyv/extensions/`，再由 App 扩展页导入。
