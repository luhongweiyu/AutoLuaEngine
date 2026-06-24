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
- 文本输入：`m.input.text` / `m.inputText`，先走 root `input text`，失败后回退剪贴板粘贴
- 剪贴板粘贴：`m.input.pasteText` / `m.pasteText`
- 通用按键：`m.key.press` / `m.pressKey`
- 返回：`m.key.back` / `m.back`
- Home：`m.key.home` / `m.home`
- 截图：`m.screen.capture()` / `m.capture()`
- 通用命令：`m.root.exec(command, timeoutMs)` / `m.rootExec(...)`
- Root 探测状态：`m.root.status()` / `m.rootStatus()` / HTTP `root.status`
- Root 文件：`m.root.file.exists/readText/writeText/stat/list/remove/mkdir/chmod/chown`，当前是文本、状态、列表和基础权限第一版
- Root 进程：`m.root.process.pidOf/list/info/kill`，当前是查询 PID、进程列表、进程详情和结束进程第一版
- Root 设备：`m.device.screenState/wake/sleep/battery/rotation/setRotation`，当前是屏幕状态、唤醒/息屏、电量和方向控制第一版
- 应用控制：`m.app.open(packageName)` root 优先启动，`m.app.stop(packageName)` root 强停，`m.app.clearData/grant/revoke/current/install/uninstall/disable/enable` 走 root 做数据、权限、前台查询和包管理
- 状态：`m.device.isRootAvailable()`、`m.device.info().rootModeEnabled`、`m.device.info().rootAvailable`、`m.device.info().automationMode`
- 设置：`m.device.setRootModeEnabled(enabled)` / HTTP `device.setRootModeEnabled`

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

Root 模式默认开启，可在 App 主界面的“运行模式：Root 优先（默认）/无障碍优先”按钮切换。
也可以通过脚本或 IDE/PC 协议切换：

```lua
m.device.setRootModeEnabled(true)
```

```json
{
  "method": "device.setRootModeEnabled",
  "params": {
    "enabled": true
  }
}
```

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
m.inputText("hello world")
m.pasteText("中文输入\n第二行")
m.pressKey(66)
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
探测会同时尝试 `su`、`/system/xbin/su`、`/system/bin/su`、`/sbin/su`、`/vendor/bin/su` 这些常见路径。
如果 root 不可用，可以用下面的接口查看每次探测的 stdout、stderr 和退出码：

```lua
local status = m.root.status()
print(status.available, status.commandMode, status.suPath, status.error)
```

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

- 触控、文本输入和按键已优先复用常驻 root shell；root 截图、`m.root.exec` 和 root 文件 API 仍使用短命令。
- root 截图虽已避开 PNG 编码和磁盘 IO，但仍不适合作最终高频找色方案。
- 文本输入当前支持 root `input text` 和剪贴板粘贴；剪贴板路线会覆盖系统剪贴板，且依赖焦点控件支持粘贴。
- Root 文件 API 当前只承诺 UTF-8 文本读写、状态、列表、删除、递归删除、目录创建、chmod 和 chown，不承诺二进制传输。
- Root 进程 API 当前只承诺 `pidOf/list/info/kill`，不承诺资源占用统计或守护能力。
- Root 设备 API 当前只承诺屏幕状态、唤醒/息屏、电量和方向读写；不同系统版本的 `dumpsys` 输出可能需要继续适配。
- Root 应用控制当前只承诺启动、强停、清数据、权限授予和撤销、前台应用查询、安装、卸载、冻结、解冻；组件级启停后续按需要再补。
- root 授权弹窗由系统或 root 管理器控制，App 不能静默授权。
- 部分设备的 `su` 行为可能不同，需要后续适配。

## 4. 后续路线

优先级：

1. root 截图优化：评估常驻 root 进程直接取 framebuffer / Surface，减少每帧创建进程和复制大块 stdout 的开销。
2. root 输入优化：继续在真实 root 设备上压测常驻 root shell，确认点击、滑动、按键连续执行稳定性。
3. root 文件能力：当前已有文本读写、状态、列表、删除、递归删除、目录创建、chmod 和 chown；后续补二进制传输。
4. root 进程能力：当前已有 `pidOf/list/info/kill`；后续补资源占用统计和守护脚本需要的状态查询。
5. root 引擎进程：如果需要更强能力，再参考旧项目 root engine 做独立 root service。

暂不做：

- 直接复制旧项目 so。
- 反编译不可维护的 native 实现后硬塞进当前工程。
- 每次脚本运行都强制 root；无 root 设备仍保留无障碍 fallback。
