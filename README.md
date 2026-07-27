# 小鱼精灵

本仓库用于实现一个面向自动化脚本开发的运行时与 IDE 工具链。

当前第一版目标非常明确：**只做 Android + Lua**，但工程结构必须为后续支持 Windows、iOS、JavaScript、Go、VS Code/Qt IDE 复用预留接口。

## 当前阶段

- 阶段：Android + Lua 第一版基础闭环
- 第一版平台：Android
- 第一版脚本语言：Lua 5.4.8
- 第一版 IDE：VSCode 插件负责脚本开发与控制，Qt 6 独立工具负责抓图、取色和图像分析

## 文档索引

- [文档总入口](docs/README.md)：后续开发和 AI 接手从这里开始
- [公开脚本文档](docs/public/脚本文档.md)
- [交互式脚本文档](docs/public/脚本文档.html)
- [项目总计划](docs/internal/项目总计划.md)
- [架构设计](docs/internal/架构设计.md)
- [统一 API 契约](docs/internal/contracts/API_契约.md)
- [构建与运行](docs/internal/构建与运行.md)
- [IDE 抓图取色工具](docs/internal/ide/PC_抓图取色工具.md)
- [引擎通讯协议](shared/protocol/ENGINE_PROTOCOL.md)

## 当前运行链路

```text
Android APK -> JNI -> libengine.so -> Lua 5.4.8 -> HostApi -> HTTP JSON-RPC -> IDE 工具链
```

VSCode 与 `xiaoyv_tools.exe` 各自直连 Android 引擎。Qt 工具当前已支持设备原始帧截图、
全屏图片投影、Lua 测试运行与日志回传。
