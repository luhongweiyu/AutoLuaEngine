# 小鱼精灵

本仓库用于实现一个面向自动化脚本开发的运行时与 IDE 工具链。

当前第一版目标非常明确：**只做 Android + Lua**，但工程结构必须为后续支持 Windows、iOS、JavaScript、Go、VS Code/Qt IDE 复用预留接口。

## 当前阶段

- 阶段：Android + Lua 第一版基础闭环
- 第一版平台：Android
- 第一版脚本语言：Lua 5.4.8
- 第一版 IDE：VSCode 插件负责脚本开发与控制，Qt 6 独立工具负责抓图、取色和图像分析

## 文档索引

- [项目总计划](docs/项目总计划.md)
- [架构设计](docs/架构设计.md)
- [统一 API 契约](docs/API_契约.md)
- [引擎脚本文档](docs/脚本文档.md)
- [Android 脚本 UI](docs/ANDROID_脚本_UI.md)
- [构建与运行](docs/构建与运行.md)
- [引擎通讯协议](shared/protocol/ENGINE_PROTOCOL.md)
- [PC 抓图取色工具](docs/PC_抓图取色工具.md)
- [旧项目参考记录](docs/旧项目参考记录.md)
- [Android 引擎独立进程拆分方案](docs/ANDROID_引擎进程拆分.md)
- [Android Root 模式](docs/ANDROID_ROOT_模式.md)
- [AI 执行规范](docs/AI_执行指南.md)
- [用户待办事项](docs/用户待办事项.md)

## 当前运行链路

```text
Android APK -> JNI -> libengine.so -> Lua 5.4.8 -> HostApi -> HTTP JSON-RPC -> VS Code/PC 工具
```

VSCode 与 `xiaoyv_tools.exe` 各自直连 Android 引擎。Qt 工具当前已支持设备原始帧截图、
全屏图片投影、Lua 测试运行与日志回传。
