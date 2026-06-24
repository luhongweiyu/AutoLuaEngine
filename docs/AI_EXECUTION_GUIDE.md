# AI 执行规范

本文档用于保证后续 AI 能连续、有序地推进工程。

## 1. 当前工程目标

第一版只做：

```text
Android + Lua + 最小 HostApi
```

不要在第一版主动引入：

- JavaScript
- Go
- Windows 引擎
- iOS 引擎
- 完整 IDE
- FFI 完整实现
- Java import 完整实现

除非用户明确要求切换阶段。

## 2. 每次开始工作前必须做

1. 阅读 `README.md`
2. 阅读 `docs/PROJECT_PLAN.md`
3. 阅读当前要改动相关的架构/API 文档
4. 如果改 Android 引擎、悬浮窗、脚本 API 或兼容层，阅读 `docs/OLD_PROJECT_REFERENCE.md`
5. 如果改 Android 服务进程、HTTP 服务或 NativeEngine 启动链路，阅读 `docs/ANDROID_ENGINE_PROCESS_SPLIT.md`
6. 检查工作区是否有已有文件和未完成改动
7. 不要删除或重写用户已有改动

## 3. 推进顺序

默认按以下顺序推进：

1. Android Native 工程
2. JNI 最小调用
3. Lua 5.4.8 Runtime
4. `print` / `sleep`
5. ScriptTask
6. HostApi v0.1
7. Android 自动化能力 v0.2
8. PC/IDE 通讯协议

不得跳过最小闭环直接做复杂功能。

## 4. 代码组织要求

代码必须保持可读、可维护、可直接运行。

文件和文件夹必须按职责归类，不能把不同层级的代码混放。

推荐归类：

```text
docs/                                   项目规划、架构、API 文档
engines/android/app/src/main/java/      Android Kotlin/Java 层
engines/android/app/src/main/cpp/       Android Native C++ 引擎层
engines/android/app/src/main/assets/    Android 内置 Lua 示例和资源
engines/windows/                        Windows 引擎预留目录
engines/ios/                            iOS 引擎预留目录
ide/                                    VS Code / Qt IDE 相关代码
shared/                                 跨平台协议、API 描述、公共模型
tools/                                  PC 辅助脚本或调试工具
```

新增 C++ 函数时：

- 函数放置有序
- 命名清晰
- 复杂逻辑需要详细注释
- 不过度封装
- 不做过度兜底

新增 Kotlin/Java 代码时：

- Android 权限、生命周期、JNI 边界写清楚注释
- 不把大量业务逻辑塞进 Activity
- Native 调用统一通过 `NativeEngine`

## 5. 文档同步规则

以下情况必须更新文档：

- 新增阶段任务
- 修改架构边界
- 新增脚本 API
- 修改返回值规则
- 引入新的第三方库
- 改变 Android Studio / Gradle / NDK 版本要求

需要同步的文档：

- 总体计划：`docs/PROJECT_PLAN.md`
- 架构：`docs/ARCHITECTURE.md`
- API：`docs/API_CONTRACT.md`
- 脚本文档：`docs/SCRIPT_MANUAL.md`
- 用户需要做的事：`docs/USER_TODO.md`

## 6. 验证要求

每完成一个阶段，必须能给出明确验证方式。

示例：

- APK 是否能启动
- JNI 是否被调用
- Lua 是否能执行
- Logcat 是否能看到输出
- API 是否返回预期值

不能只说“理论上可以”。

## 7. 当前下一步

当前已进入 Android + Lua 第一版基础闭环完善阶段。

优先顺序：

1. 按 `docs/ANDROID_ENGINE_PROCESS_SPLIT.md` 继续拆掉主进程对 `NativeEngine` 的直接依赖。
2. 继续补基础 `m.*` API，并同步维护 `docs/SCRIPT_MANUAL.md` 顶部速查表。
3. 后续实现 Toast、剪贴板、启动 App、输入法输入等系统 API。
4. 找色、比色等算法继续暂缓，等基础控制和系统 API 稳定后再做。
