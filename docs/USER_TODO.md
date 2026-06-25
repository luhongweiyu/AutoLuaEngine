# 用户待办事项

这份文档记录需要用户亲自完成或确认的事项。AI 会尽量承担工程实现，用户主要负责少量把关。

## 1. 当前必须做

### 1.1 升级或确认 Android Studio

你当前已有：

```text
Android Studio Iguana | 2023.2.1
```

建议升级到当前 Stable 稳定版。不要安装 Canary、Beta、RC。

完成后记录：

```text
Android Studio 版本：已升级，具体版本待补充
```

### 1.2 安装 SDK / NDK / CMake

在 Android Studio 中检查：

路径：

```text
Settings -> Languages & Frameworks -> Android SDK
```

需要确认安装：

- Android SDK Platform
- Android SDK Build-Tools
- Android SDK Platform-Tools
- NDK
- CMake

完成后记录：

```text
SDK 是否安装：
NDK 版本：
CMake 版本：
```

### 1.3 准备测试设备

二选一即可：

- 安卓真机，开启开发者模式和 USB 调试
- Android Studio 模拟器

建议第一轮用模拟器，减少权限干扰。

完成后记录：

```text
测试设备：
Android 版本：
```

## 2. 需要你确认的项目选项

AI 创建工程前，需要你确认以下信息。

### 2.1 项目名称

建议：

```text
AutoLuaEngine
```

你也可以改。

最终选择：

```text
项目名称：AutoLuaEngine
```

### 2.2 Android 包名

建议：

```text
com.autolua.engine
```

如果未来要商业化，包名最好使用你自己的域名反写。

最终选择：

```text
包名：com.autolua.engine
```

### 2.3 最低 Android 版本

建议第一版：

```text
minSdk 23
```

理由：

- 覆盖面足够
- Android 自动化相关能力更好处理
- 不为太旧系统增加额外负担

最终选择：

```text
minSdk：23
```

## 3. 暂时不用你做

以下内容暂时不需要你处理：

- 学 Java/Kotlin 语法
- 学 CMake 细节
- 学 JNI 细节
- 写 Lua 引擎接入代码
- 写 IDE 插件
- 写 Qt 工具

这些由 AI 逐步实现，你只需要在关键选择上把关。

## 3.1 后续触控测试需要你手动开启

Android 不允许 App 静默开启无障碍服务。无 root 或 App 不能获取 root 时，测试真实 `touch.tap` 前，需要你在模拟器或真机中手动开启：

```text
Settings -> Accessibility -> AutoLuaEngine automation service
```

所选路线不可用时，脚本会返回类似：

```text
touch tap failed; root or accessibility service is not available
```

## 3.2 无障碍优先模式截图需要你手动授权

Root 模式截图只使用 root helper。切到无障碍优先模式后，截图会使用 MediaProjection，需要你点击 App 内：

```text
开启截图授权
```

然后在系统弹窗中确认授权。这个权限不能静默获取。
授权后再点击 `测试截图和取色`，脚本会返回一帧内存图片句柄，并在日志中输出尺寸和字节数。

## 4. 你后续主要负责

1. 确认工具是否安装完成
2. 提供 Android Studio 报错截图或错误文本
3. 在真机/模拟器上点击运行
4. 判断脚本 API 用起来是否顺手
5. 对功能优先级做取舍

## 5. 下一步回复建议

你可以直接回复：

```text
Android Studio 已升级/未升级
SDK/NDK/CMake 已安装/未安装
项目名用 xxx
包名用 xxx
minSdk 用 xx
测试设备是模拟器/真机
```
