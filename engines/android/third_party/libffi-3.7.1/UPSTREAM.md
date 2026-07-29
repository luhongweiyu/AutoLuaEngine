# libffi 3.7.1

- 上游：<https://github.com/libffi/libffi>
- 固定提交：`5c1c43091ed611fdea774374355eb938c73a9157`（标签 `v3.7.1`）
- 上游源码归档 SHA-256：`D5E9A6638DDBD2513DDB54518EB67E4BBE6FA707BCC01C10F6212F0A088D819D`
- 许可证：MIT，完整文本见 [LICENSE](LICENSE)。

本目录保留 Android 四个 ABI 所需的通用、`aarch64`、`arm` 与 `x86` 源文件及头文件；
其余平台源码不导入。`android/fficonfig.h` 是 Android NDK 的等价配置，`ffi.h` 则在
CMake 配置期由上游 `include/ffi.h.in` 生成。Android 使用 libffi 的静态 trampoline，
以支持回调而不依赖运行时写入可执行代码页。

升级时应从上游固定提交重新导入受控源码集和许可证，审阅 Android 配置差异，并验证四个
ABI 的编译和回调探针。
