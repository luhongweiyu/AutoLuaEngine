# 项目总计划

## 1. 项目目标

实现一个自动化脚本平台，长期支持：

- 平台：Android、Windows、iOS
- 脚本语言：Lua、JavaScript、Go
- IDE：VS Code 插件、Qt 工具或 Qt IDE

第一版只实现：

- Android 引擎
- Lua 脚本运行
- 最小 Host API
- 为后续多语言、多平台、IDE 复用预留边界

## 2. 核心原则

1. **先统一 API 语义，再实现不同平台。**
   Lua、JS、Go 后续都调用同一套 HostApi 语义。

2. **第一版只做 Android + Lua。**
   不在第一版同时引入 JS、Go、Windows、iOS，避免工程过早发散。

3. **默认不改 Lua 官方源码，只保留必要小补丁。**
   常规扩展通过 C API / JNI / 宿主注册函数实现；当前为支持 UTF-8 中文变量名、函数名，已小范围修改 `llex.c` 的标识符识别逻辑，后续升级 Lua 时必须同步迁移该补丁。

4. **IDE 与引擎通过协议通讯。**
   IDE 不直接绑定 Android。后续 Windows/iOS 引擎只要实现同一套协议，IDE 即可复用。

5. **功能先小闭环，再扩展。**
   每个阶段都必须有可运行、可验证的产物。

## 3. 总体阶段

### 阶段 0：规划与准备

目标：明确工程边界、文档、工具链和最小路线。

任务：

- [x] 建立项目规划文档
- [x] 建立架构文档
- [x] 建立统一 API 契约文档
- [x] 建立 AI 执行规范
- [x] 建立用户待办事项
- [x] 用户升级或确认 Android Studio
- [ ] 用户确认 SDK / NDK / CMake 可用版本
- [x] 用户确认项目名称、包名、最低 Android 版本

验收标准：

- 后续 AI 可以根据文档继续推进
- 用户知道下一步需要准备什么

### 阶段 1：Android APK + Native 引擎最小链路

目标：跑通 `APK -> JNI -> libengine.so`。

任务：

- [x] 创建 Android 工程
- [x] 创建 Native C++ 模块
- [x] Java 层加载 `libengine.so`
- [x] 暴露最小 `nativeInit()`、`nativeRunTest()` JNI 方法
- [x] C++ 层返回固定字符串并写入日志
- [x] Android 页面提供一个“运行测试”按钮
- [x] 命令行构建验证通过
- [ ] 用户在 Android Studio 或设备上运行验证

验收标准：

- APK 能安装并启动
- 点击按钮能调用 native 方法
- Logcat 能看到 native 层输出

### 阶段 2：接入 Lua Runtime

目标：原版 Lua 能在 Android native 层执行。

任务：

- [x] 引入 Lua 5.4.8 源码
- [x] CMake 编译 Lua 到 `libengine.so`
- [x] 创建 `LuaRuntime`
- [x] 支持执行字符串形式 Lua 代码
- [x] 注册 `print()`
- [x] 注册 `sleep(ms)`
- [x] 将 Lua 输出转发到 Android 日志
- [x] 构建并安装到模拟器验证

验收标准：

- 执行 `print("hello lua")` 成功
- 执行 `sleep(1000)` 不崩溃
- Lua 错误能返回到 Java/Kotlin 层或日志中

验证记录：

```text
_VERSION = Lua 5.4
sleep done
```

### 阶段 3：脚本任务模型

目标：让脚本运行具备任务生命周期。

任务：

- [x] 定义 `ScriptTask`
- [x] 每个任务有 `taskId`
- [x] 支持同步启动任务
- [x] 支持协作停止任务
- [x] 支持任务状态：`idle`、`running`、`finished`、`failed`
- [x] 记录最后一次错误
- [x] 统一日志通道

验收标准：

- 可以启动一段 Lua
- 可以请求停止任务
- 任务结束后状态可查询

当前状态：

```text
已实现同步 ScriptTask 模型、Java 后台执行、Lua debug hook 协作停止。
验证：task#1 finished，task#2 failed。
Java UI 层已临时使用后台线程执行 native 同步调用，避免 sleep 阻塞主线程。
Stop 验证：Lua run failed: script stopped。
Android 测试 Activity 已整理为脚本测试、平台测试、控制、状态四个调试区。
Lua print/m.log.print、native 初始化、任务结果、stop 日志均进入统一 log.drain 缓冲。
```

### 阶段 4：HostApi v0.1

目标：实现第一批统一脚本 API。

第一批 API：

- `m.log.print(text)`
- `m.sleep(ms)`
- `m.device.info()`
- `m.file.read(path)`
- `m.file.write(path, content)`
- `m.file.exists(path)`
- `m.file.remove(path)`

任务：

- [x] C++ 层定义 HostApi 注册入口
- [x] Lua 层可调用统一 API
- [x] Android 平台实现对应能力
- [x] 每个 API 有简单测试脚本
- [x] 建立 Lua 层 `m/lr/cd` 命名空间和 `useApi` 切换框架

验收标准：

- Lua 能调用第一批 HostApi
- API 错误有明确返回，不静默失败

验证记录：

```text
m.log.print works
device platform = android
engine version = 0.1.0
file.read = hello from lua file api
file.exists after write = true
file.remove success
file.exists after remove = false
Lua run failed: expected lua runtime error
```

### 阶段 5：Android 基础自动化能力

目标：开始接近自动化引擎功能。

任务：

- [x] `m.touch.tap(x, y)`，优先走无障碍
- [x] `m.touch.swipe(x1, y1, x2, y2, duration)`
- [x] `m.key.isAccessibilityEnabled()`、`m.key.back()`、`m.key.home()`，走无障碍全局动作
- [x] `m.screen.capture()`，优先 MediaProjection
- [x] 图片对象句柄管理，当前支持基础句柄、`m.image.release`、`m.image.getPixel`、`m.image.getPixels`
- [ ] `m.image.findColor(...)`

验收标准：

- 能点击屏幕指定坐标
- 能获取截图
- 能在截图中找颜色

当前状态：

```text
m.touch.tap / m.touch.swipe / m.key.back / m.key.home 已注册。未开启无障碍服务时返回明确错误。
真实点击需要用户在系统设置中手动开启 AutoLuaEngine 无障碍服务。
m.screen.capture 已接入 MediaProjection + ImageReader。未授权时返回明确错误；
授权后返回 native 内存图片句柄和 width、height、rowStride、pixelStride、byteLength、format。
当前已支持在 native 内存图片句柄上单点读取和批量读取 RGB 点阵；
找色、比色等算法后续再做，不在这一阶段提前实现。
```

### 阶段 6：PC/IDE 通讯雏形

目标：让电脑端可以向手机发脚本并接收日志。

任务：

- [x] 选择通讯方式：优先本地 ADB 转发 + HTTP JSON-RPC
- [x] Android 端提供运行脚本接口
- [x] PC 端最小命令行工具发送脚本
- [x] 日志能从 Android 回传 PC，当前通过 `log.drain` 轮询

验收标准：

- PC 可以发送 Lua 文本到手机执行
- PC 可以看到脚本执行结果
- PC 可以通过 `log.drain` 读取脚本日志

### 阶段 7：为 IDE 复用固化协议

目标：形成后续 VS Code 插件和 Qt 工具都能使用的协议。

任务：

- [x] 定义 `script.run`
- [x] 定义 `script.stop`
- [x] 定义 `script.status`
- [ ] 定义 `log.subscribe`
- [x] 定义 `device.info`
- [x] 定义 `screen.capture`
- [x] 定义 `image.release`
- [x] 写协议文档
- [x] 写最小测试客户端，当前为 VS Code 插件雏形和 PowerShell HTTP 工具

验收标准：

- IDE 不需要知道底层是 Android/Lua
- 后续 Windows/iOS 只要实现同协议即可复用 IDE

当前状态：

```text
ide/vscode-extension 已提供最小命令：
- AutoLuaEngine: Check Connection
- AutoLuaEngine: Run Current Lua File
- AutoLuaEngine: Stop Script
- AutoLuaEngine: Drain Logs

插件已提供底部状态栏按钮：AutoLua、Run Lua、Stop、Logs。
插件通过 adb forward + HTTP JSON-RPC 连接 Android 引擎。
```

## 4. 第一版暂不做

以下内容先不进入第一版核心闭环：

- JavaScript 引擎
- Go 脚本
- Windows 引擎
- iOS 引擎
- FFI 完整实现
- Java import 完整实现
- Dex 插件加载
- 多设备管理
- 可视化 IDE 完整版
- OCR / YOLO

这些能力只在架构上预留，不提前实现。

## 5. 下一步具体行动

1. 根据插件使用情况再决定是否做 Qt 调试工具。
2. 找色、比色等算法暂缓，等基础通讯闭环稳定后再做。
