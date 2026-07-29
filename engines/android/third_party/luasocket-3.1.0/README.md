# LuaSocket 3.1.0

本目录包含编入 Android `libengine.so` 的 LuaSocket 原生核心源码。

- 上游仓库：<https://github.com/lunarmodules/luasocket>
- 固定来源：官方 `v3.1.0` tag
- 许可证：MIT，全文见 [`LICENSE`](LICENSE)

`src/` 仅保留 Android 构建 `socket.core` 与 `mime.core` 所需的上游 C/H 文件。对应的上层 Lua
模块放在 `app/src/main/assets/runtime/luasocket/`，由运行时注册到 `package.preload`。
