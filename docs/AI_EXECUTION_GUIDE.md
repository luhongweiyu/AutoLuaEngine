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
4. 检查工作区是否有已有文件和未完成改动
5. 不要删除或重写用户已有改动

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

等待用户完成工具链准备，然后创建 Android Native 工程。

用户需要确认：

- Android Studio 是否升级完成
- SDK / NDK / CMake 是否安装
- 项目名称
- Android 包名
- 最低 Android 版本
- 测试设备或模拟器
