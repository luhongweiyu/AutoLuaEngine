# Android Root 模式

本文档记录 Android root 模式的当前实现和后续路线。

## 1. 当前目标

用户明确要求优先考虑 root 版本，因为非 root 权限太低，很多能力无法实现。

当前 root 模式已落地：

```text
EngineService(:engine) -> root 授权检查 -> RootShellBridge / RootHelperBridge 常驻执行
```

当前覆盖：

- 点击：`m.touch.tap` / `m.tap`
- 滑动：`m.touch.swipe` / `m.swipe`
- 文本输入：`m.input.text` / `m.inputText`，只走 root `input text`
- 剪贴板粘贴：`m.input.pasteText` / `m.pasteText`
- 通用按键：`m.key.press` / `m.pressKey`
- 返回：`m.key.back` / `m.back`
- Home：`m.key.home` / `m.home`
- 截图：`m.screen.capture()` / `m.capture()`
- 显式 root 截图：`m.root.screen.capture()` / `m.rootCapture()`
- 通用命令：`m.root.exec(command, timeoutMs)` / `m.rootExec(...)`
- Root 探测状态：`m.root.status()` / `m.rootStatus()` / HTTP `root.status`
- Root 文件：`m.root.file.exists/readText/writeText/stat/list/remove/mkdir/chmod/chown`，当前是文本、状态、列表和基础权限第一版
- Root 进程：`m.root.process.pidOf/list/info/stats/kill`，当前是查询 PID、进程列表、进程详情、资源统计和结束进程第一版
- Root 设备：`m.device.screenState/wake/sleep/battery/rotation/setRotation/settings/prop/display`，当前是屏幕状态、唤醒/息屏、电量、方向控制、系统设置、系统属性、显示参数和亮度控制第一版
- 应用控制：`m.app.open(packageName)` root-only 启动，`m.app.stop(packageName)` root 强停，`m.app.clearData/grant/revoke/current/install/uninstall/disable/enable/disableComponent/enableComponent` 走 root 做数据、权限、前台查询、包管理和组件启停
- 状态：`m.device.isRootAvailable()`、`m.device.info().rootModeEnabled`、`m.device.info().rootAvailable`、`m.device.info().automationMode`
- 设置：`m.device.setRootModeEnabled(enabled)` / HTTP `device.setRootModeEnabled`

## 2. 执行策略

当前 Android 平台 root 模式执行顺序：

```text
脚本运行前检查 App 进程 root 授权
root 可用 -> 运行脚本，触控/按键/输入/截图只走 root 路线
root 不可用 -> 直接拒绝运行脚本
```

截图执行顺序：

```text
Root 模式 -> root helper 常驻进程截图；root 不可用或截图失败时直接返回错误
无障碍优先模式 -> MediaProjection；授权不可用或截图失败时直接返回错误
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

关闭后，触控、按键、截图走无障碍/系统授权路线；显式调用 `m.root.exec` 和 `m.root.screen.capture` 仍然只走 root。

`m.device.info().automationMode` 当前取值为：

```text
root-first：root 模式开启且 root 可用，触控和按键只走 root
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

底层会直接走 root；无障碍和 MediaProjection 只在手动切到“无障碍优先”后使用。

## 3. 当前边界

当前 root 模式拆成两层：

```text
RootShellBridge -> su -c sh 常驻 shell，承接 root.exec、触控、按键、文件、设备、应用控制等命令型能力
RootHelperBridge -> su -c app_process 常驻 uid=0 helper，当前承接 root 截图
```

不再尝试其他 su 参数格式。

```text
root 模式下 root shell 不可用 -> 当前操作失败
```

脚本运行前会先检查 root 授权。如果 root 不可用，可以用下面的接口查看 stdout、stderr 和退出码：

```lua
local status = m.root.status()
print(status.available, status.commandMode, status.suPath, status.error)
```

root 截图使用 root helper：

```text
su -c app_process /system/bin com.autolua.engine.RootHelperMain
```

helper 通过系统隐藏截图 API 获取当前屏幕，再通过“文本头 + 原始 RGBA 帧”传回引擎进程。
截图成功后会在图片句柄里返回 `source` 和 `captureDurationMs`：

```lua
local img = m.screen.capture()
print(img.source, img.captureDurationMs)
```

`source = "root-helper"` 表示本帧走 root helper；`source = "media-projection"` 只会在无障碍优先模式下出现。

优点：

- root 模式逻辑清晰，不在运行期来回切路线。
- 失败原因更直接，脚本可以自己决定是否继续。
- 可以快速验证 root 方向是否符合预期。

限制：

- Root 模式下如果 App 没有 root 授权，脚本会直接拒绝运行。
- root 截图当前已改为 root helper 常驻进程，不再每帧启动 `su screencap`；下一步要做帧引擎和共享内存，继续减少跨进程复制。
- 文本输入当前只支持 root `input text`；中文、换行和复杂符号由脚本显式调用 `m.input.pasteText`。
- Root 文件 API 当前只承诺 UTF-8 文本读写、状态、列表、删除、递归删除、目录创建、chmod 和 chown，不承诺二进制传输。
- Root 进程 API 当前只承诺 `pidOf/list/info/stats/kill`，其中 `stats` 读取 `/proc/<pid>/status` 的常用资源字段，暂不承诺守护能力。
- Root 设备 API 当前只承诺屏幕状态、唤醒/息屏、电量、方向读写、显示参数和亮度控制；不同系统版本的 `dumpsys`、`wm` 和 `settings` 输出可能需要继续适配。
- Root 系统设置和属性 API 当前只承诺 `settings get/put/delete`、`getprop/setprop` 底层能力；更高层语义后续单独封装。
- Root 显示 API 当前只承诺 `wm size`、`wm density` 和亮度 settings 的第一版封装；修改分辨率和 DPI 后建议脚本主动恢复默认值。
- Root 应用控制当前只承诺启动、强停、清数据、权限授予和撤销、前台应用查询、安装、卸载、冻结、解冻和组件级启停。
- root 授权弹窗由系统或 root 管理器控制，App 不能静默授权。
- 部分设备的 `su` 行为可能不同，需要后续适配。

## 4. 后续路线

优先级：

1. root 截图优化：在 root helper 内实现帧引擎和共享内存/直接 native buffer，减少当前原始帧跨进程复制。
2. root 输入优化：继续在真实 root 设备上压测常驻 root shell，确认点击、滑动、按键连续执行稳定性。
3. root 文件能力：当前已有文本读写、状态、列表、删除、递归删除、目录创建、chmod 和 chown；后续补二进制传输。
4. root 进程能力：当前已有 `pidOf/list/info/stats/kill`；后续补守护脚本需要的状态查询。
5. root 引擎进程：如果需要更强能力，再参考旧项目 root engine 做独立 root service。

暂不做：

- 直接复制旧项目 so。
- 反编译不可维护的 native 实现后硬塞进当前工程。
- Root 模式运行期自动切到无障碍或 MediaProjection。

## 5. 验证记录

旧记录：2026-06-25 在雷达模拟器 `emulator-5560` 上验证过短命令截图：

```text
root.status -> available=true, commandMode=SU_C, suPath=su
root.screen.capture 连续 5 次 -> source=root-screencap, 1280x720
captureDurationMs -> 166, 145, 164, 164, 205
```

该结果只保留作对比，当前 root 截图主路线已经改为 root helper。

2026-06-25 同设备更新后验证：

```text
进程 -> com.autolua.engine + com.autolua.engine:engine
root.exec -> 常驻 root shell 可返回中文 stdout/stderr
root helper -> su -c app_process 常驻 uid=0，可完成截图
m.screen.capture 连续 10 次 -> source=root-helper, 1280x720
captureDurationMs -> avg 53.5, min 33, max 59
```

当前瓶颈已经从 `su screencap` 短命令切换为 root helper 截图和跨进程帧复制；下一步做帧引擎。
