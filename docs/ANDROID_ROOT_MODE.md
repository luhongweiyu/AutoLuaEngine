# Android Root 模式

本文档记录 Android root 模式的当前实现和后续路线。

## 1. 当前目标

用户明确要求优先考虑 root 版本，因为非 root 权限太低，很多能力无法实现。

当前 root 模式已落地：

```text
RootShellBridge -> 探测可用 su 格式 -> 常驻 root shell / 短命令
```

当前覆盖：

- 点击：`m.touch.tap` / `m.tap`
- 滑动：`m.touch.swipe` / `m.swipe`
- 返回：`m.key.back` / `m.back`
- Home：`m.key.home` / `m.home`
- 截图：`m.screen.capture()` / `m.capture()`
- 通用命令：`m.root.exec(command, timeoutMs)` / `m.rootExec(...)`
- 状态：`m.device.isRootAvailable()`、`m.device.info().rootModeEnabled`、`m.device.info().rootAvailable`、`m.device.info().automationMode`

## 2. 执行策略

当前 Android 平台自动化执行顺序：

```text
Root 模式开启且 root 可用 -> 优先常驻 root shell 执行 input ...
常驻 root shell 不可用 -> 回退短命令 su input ...
root 不可用或命令失败 -> 回退无障碍服务
两者都不可用 -> 返回 nil, errorMessage
```

截图执行顺序：

```text
Root 模式开启且 root 可用 -> 优先 su screencap 原始输出
root 不可用或 root 截图失败 -> 回退 MediaProjection
两者都不可用 -> 返回 nil, errorMessage
```

Root 模式默认开启，可在 App 主界面的“Root 模式：开启/关闭”按钮切换。
关闭后，触控、按键、截图不再优先走 root；显式调用 `m.root.exec` 仍然会尝试 root 命令。

`m.device.info().automationMode` 当前取值为：

```text
root-first：root 可用，触控和按键优先走 root
accessibility：root 不可用，但无障碍服务可用
none：root 和无障碍都不可用
```

也就是说，脚本 API 不需要切换函数名：

```lua
m.tap(300, 500)
m.swipe(300, 800, 300, 300, 500)
m.back()
m.home()
```

底层会自动优先走 root。

## 3. 当前边界

当前 root 模式还不是独立 root 引擎。触控和按键优先复用常驻 root shell：

```text
su -c sh
su 0 sh
su root sh
```

常驻 shell 不可用时，回退短命令：

```text
su -c "input tap x y"
su 0 sh -c "input tap x y"
su root sh -c "input tap x y"
```

启动时会优先探测 `su -c`，如果设备不支持，再尝试 `su 0 sh -c` 和 `su root sh -c`。

root 截图仍使用短命令：

```text
su ... "screencap"
```

这里读取的是 `screencap` 原始输出：头部为宽、高、像素格式等信息，后面是像素数据。
当前已支持 `RGBA_8888`、`RGBX_8888`、`BGRA_8888`，进入脚本前统一归一成 RGBA 内存帧。

优点：

- 实现简单，可维护。
- 不影响无 root 设备，失败后仍可走无障碍。
- 可以快速验证 root 方向是否符合预期。

限制：

- 触控和按键已优先复用常驻 root shell；root 截图和 `m.root.exec` 仍使用短命令。
- root 截图虽已避开 PNG 编码和磁盘 IO，但仍不适合作最终高频找色方案。
- root 授权弹窗由系统或 root 管理器控制，App 不能静默授权。
- 部分设备的 `su` 行为可能不同，需要后续适配。

## 4. 后续路线

优先级：

1. root 截图优化：评估常驻 root 进程直接取 framebuffer / Surface，减少每帧创建进程和复制大块 stdout 的开销。
2. root 输入优化：继续在真实 root 设备上压测常驻 root shell，确认点击、滑动、按键连续执行稳定性。
3. root 文件和进程能力：基于 `m.root.exec` 补进程列表、文件权限操作等。
4. root 引擎进程：如果需要更强能力，再参考旧项目 root engine 做独立 root service。

暂不做：

- 直接复制旧项目 so。
- 反编译不可维护的 native 实现后硬塞进当前工程。
- 每次脚本运行都强制 root；无 root 设备仍保留无障碍 fallback。
