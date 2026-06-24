# Android Root 模式

本文档记录 Android root 模式的当前实现和后续路线。

## 1. 当前目标

用户明确要求优先考虑 root 版本，因为非 root 权限太低，很多能力无法实现。

第一版 root 模式先落地：

```text
RootShellBridge -> 探测可用 su 格式 -> input tap / swipe / keyevent
```

当前覆盖：

- 点击：`m.touch.tap` / `m.tap`
- 滑动：`m.touch.swipe` / `m.swipe`
- 返回：`m.key.back` / `m.back`
- Home：`m.key.home` / `m.home`
- 状态：`m.device.isRootAvailable()`、`m.device.info().rootAvailable`、`m.device.info().automationMode`

## 2. 执行策略

当前 Android 平台自动化执行顺序：

```text
root 可用 -> 优先 su input ...
root 不可用或命令失败 -> 回退无障碍服务
两者都不可用 -> 返回 nil, errorMessage
```

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

当前 root 模式还不是常驻 root 引擎，只是短命令执行：

```text
su -c "input tap x y"
su 0 sh -c "input tap x y"
su root sh -c "input tap x y"
```

启动时会优先探测 `su -c`，如果设备不支持，再尝试 `su 0 sh -c` 和 `su root sh -c`。

优点：

- 实现简单，可维护。
- 不影响无 root 设备，失败后仍可走无障碍。
- 可以快速验证 root 方向是否符合预期。

限制：

- 每次命令都会创建 `su` 进程，不适合高频点阵、截图、找色。
- root 授权弹窗由系统或 root 管理器控制，App 不能静默授权。
- 部分设备的 `su` 行为可能不同，需要后续适配。

## 4. 后续路线

优先级：

1. root 截图：评估 `su screencap` 或常驻 root 进程直接取 framebuffer / Surface。
2. root 输入优化：评估常驻 root shell，减少每次点击创建进程的开销。
3. root 文件和进程能力：补 `m.root.exec`、进程列表、文件权限操作等。
4. root 引擎进程：如果需要更强能力，再参考旧项目 root engine 做独立 root service。

暂不做：

- 直接复制旧项目 so。
- 反编译不可维护的 native 实现后硬塞进当前工程。
- 每次脚本运行都强制 root；无 root 设备仍保留无障碍 fallback。
