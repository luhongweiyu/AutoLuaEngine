# Android Lua Automation Engine

本仓库用于实现一个面向自动化脚本开发的运行时与 IDE 工具链。

当前第一版目标非常明确：**只做 Android + Lua**，但工程结构必须为后续支持 Windows、iOS、JavaScript、Go、VS Code/Qt IDE 复用预留接口。

## 当前阶段

- 阶段：Android + Lua 第一版基础闭环
- 第一版平台：Android
- 第一版脚本语言：Lua 5.4.8
- 第一版 IDE 方向：已加入 VS Code 插件雏形和本地 PowerShell 工具，后续再评估 Qt

## 文档索引

- [项目总计划](docs/项目总计划.md)
- [架构设计](docs/架构设计.md)
- [统一 API 契约](docs/API_契约.md)
- [引擎脚本文档](docs/脚本文档.md)
- [Android 脚本 UI](docs/ANDROID_脚本_UI.md)
- [构建与运行](docs/构建与运行.md)
- [引擎通讯协议](shared/protocol/ENGINE_PROTOCOL.md)
- [旧项目参考记录](docs/旧项目参考记录.md)
- [Android 引擎独立进程拆分方案](docs/ANDROID_引擎进程拆分.md)
- [Android Root 模式](docs/ANDROID_ROOT_模式.md)
- [AI 执行规范](docs/AI_执行指南.md)
- [用户待办事项](docs/用户待办事项.md)

## 最小可运行目标

第一轮只证明这条链路跑通：

```text
Android APK -> JNI -> libengine.so -> Lua 5.4 Runtime -> print/sleep/log
```

跑通以后，再逐步增加触控、截图、图色、线程、IDE 通讯等能力。

当前已跑通：

```text
Android APK -> JNI -> libengine.so -> Lua 5.4.8 -> HostApi -> HTTP JSON-RPC -> VS Code/PC 工具
```
