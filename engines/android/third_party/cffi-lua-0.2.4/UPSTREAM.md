# cffi-lua 0.2.4

- 上游：<https://github.com/q66/cffi-lua>
- 固定提交：`1d6d54b4068eefc6dc4c4455175b6d0e530fa72a`（标签 `v0.2.4`）
- 上游源码归档 SHA-256：`B5F1A22786BE067C19D06CE1512178241E86349A8D961DAC01487EEEED3D7F0E`
- 许可证：MIT，完整文本见 [COPYING.md](COPYING.md)。

本目录只保留静态嵌入所需的 `src/`。Android 构建由
`app/src/main/cpp/cmake/cffi_lua.cmake` 统一管理；它使用 Lua 5.4 和同目录
`libffi-3.7.1`，不会在构建时下载依赖或调用上游 Meson。

升级时应重新从上游固定提交导入 `src/` 和许可证，更新本文件中的版本、提交与归档
SHA-256，并重新验证正式支持的 `arm64-v8a`、`x86_64` 构建及 FFI 运行时探针。
