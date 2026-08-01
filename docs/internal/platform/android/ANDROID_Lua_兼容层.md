# Android Lua 兼容层

本文记录懒人精灵、触动精灵常用 Android Lua 接口在小鱼精灵中的真实分层，供后续实现和
AI 接手使用。公开参数和示例以 `docs/public/脚本文档/catalog.json` 及其链接的函数页为准；
本页说明内部边界，不属于发布目录。

## 加载顺序

每个 Lua 状态按以下顺序执行运行时资源：

```text
api_m.lua
compat_extended.lua
compat_lr.lua
compat_cd.lua
bootstrap.lua
```

- `api_m.lua` 只组装稳定 HostApi 和小鱼原生契约。
- `compat_extended.lua` 在同一个 `m` 表上增加兼容接口，并复用已经存在的截图、点阵字库、
  输入、输入法、线程和设备能力。
- `compat_lr.lua` / `compat_cd.lua` 先保留各自必须不同的入口，再复制语义相同的 `m` 成员。
  `lr.findPic` 是当前明确的专属覆盖；默认 `m.findPic` 不得被改成懒人方向或返回形状。
- `bootstrap.lua` 最后导出默认 `m` 一级成员，因此新增兼容函数在普通脚本里也可直接调用。

不要在各兼容文件直接散落 `_G.xxx = ...`。全局切换只能由 bootstrap 的 `useApi` /
`switchApi` 管理。

`m` 是默认的正式 Lua 脚本层：函数名、参数、返回值和文档分类按懒人精灵或触动精灵的公开
契约确定。`lr`、`cd` 是独立维护的旧脚本迁移层，不能反向扩展 `m` 的签名。`_host`、Lua 桥接、
Java 和平台路由是私有实现，可用更适合本项目的分组和命名；C ABI 则是独立文档化的扩展开发
契约。不得为了内部对齐而迫使 `m` 改名，也不得让 `m` 反向束缚内部重构。

## 公开适配要点

| 功能 | 新脚本的公开形状 | 兼容与内部边界 |
|---|---|---|
| LuaSocket | `require("socket")`、`require("socket.http")`、`require("ssl.https")` | TCP/UDP/DNS/HTTP 使用上游 LuaSocket；TLS 不属于 LuaSocket，HTTPS request 保留 Android 网络层适配；不导出 `m.http`。 |
| 触控缩放 | `setScreenScale(true, width, height)` / `setScreenScale(false)` | 默认 `m` 使用布尔开关和无返回；历史参数只在后续各兼容命名空间中单独处理。 |
| 基础触控 | `touchDown([id,] x, y)`、`touchMove([id,] x, y)`、`touchUp([id,] x, y)` | 默认 `m` 三者均无返回，且抬起时必须给出坐标。HostApi 的注入状态只供组合手势内部判断。 |
| 常用手势 | `tap(x, y[, duration])`、`longTap(x, y[, duration])`、`swipe(...)` | 时长和返回值按公开兼容契约整理，不能因底层布尔结果泄漏而改变脚本语义。 |
| 输入法 | `m.ime.*`；默认全局为 `ime.*` | `m.ime` 是正式模块；兼容命名空间的历史模块名以后单独整理。HostApi 可自行调整输入法模块划分。 |
| 其他扩展 | `cryptLib.*`、图色/找图、`cv.*`、`nodeLib.*` | 公开页按用户任务分类，不再以“兼容接口”作为总目录。 |

## 平台调用路径

固定、稳定的设备能力继续使用具名 C ABI。兼容模块中变化较快或返回结构较多的 Android
能力使用受控 JSON 子通道：

```text
compat_extended.lua
  -> _host.platformCall(operation, arguments)
  -> engine_deviceCallJson(operation, argumentsJson)
  -> core/api/device_api
  -> AndroidBridge / DevicePlatformBridge
  -> Crypto / Network / OpenCV / Utility / AccessibilityNode bridge
```

`EngineDeviceApi::callJson` 从 ABI 20 起追加在函数表尾部。它不是任意 JNI 反射入口：
`DevicePlatformBridge` 必须逐个前缀或操作名分发，未知操作必须失败。Lua table 在 HostApi
转换为 JSON 对象，平台返回的 JSON 值再转换回 Lua 值。

任意二进制字符串跨 JSON 边界时使用无换行 Base64；Lua 入口必须在调用前后完成编码和
还原，不能把 Base64 泄漏为公开返回值。

## 错误语义

- `platformCall` 返回 `value`，失败返回 `nil, errorMessage`，适合 HTTP、下载等公开契约本来
  就包含失败返回的接口。
- `platformCallOrError` 在失败时抛出 Lua 错误，适合旧接口约定“无返回”或只有业务状态的
  操作；不能把平台失败静默伪装成成功。
- 普通业务 `false` 与平台失败必须区分。例如 WebSocket 的发送拒绝是 `false`，分发或参数
  错误才抛出。
- C ABI 返回的 JSON、字符串和 lastError 都由当前线程持有，HostApi 必须在释放 VM Gate
  的阻塞调用返回后立即复制。

## 当前模块

| 模块 | 主要实现 | 约束 |
|---|---|---|
| cipher | `CryptoPlatformBridge` | AES 二进制、PEM RSA；JSON 边界 Base64 |
| network / luasocket | `NetworkPlatformBridge`、LuaSocket C core、运行时 Lua modules | 项目 HTTP/WebSocket 使用 OkHttp；LuaSocket 提供 TCP/UDP/DNS/HTTP/MIME，HTTPS request 经平台适配；原始 socket 遵循上游同步超时语义 |
| io / time | Lua 5.4 标准库、runtime/device API | 已有标准函数不重复包装；网络时间在总接收等待约 3 秒内依次尝试 3 个真实 NTP 服务 |
| ffi | `ffi_lua_api` + 静态 cffi-lua / libffi | 公开入口为 `m.ffi`、全局 `ffi` 和 `require("ffi")`；支持声明、cdata、结构体、数组、浮点、回调和可变参数。Android 按当前 ABI 静态编入调用与回调 trampoline；ARM64 已完成真实运行时验证，项目不要求逐 ABI 重复验收；不是 LuaJIT FFI |
| touch | 现有 InputApi + Lua 坐标换算 | 缩放只改变兼容入口坐标，底层始终使用真实画面 |
| color | `color_compat_lua_api` + 现有截图缓存 + `PaddleOcr` | `0,0,0,0` 为全屏；Java Bitmap OCR 复用 ONNX 模型缓存，并在实际加载模型时按需加载已导入运行时 |
| image / cv | 现有模板核心、`cv_compat_lua_api`、`OpenCvPlatformBridge`、`LuaEngine.snapShotMat` | 找图热路径不迁入 OpenCV；值指针用 native userdata，霍夫找圆和 Mat 使用官方 AAR，并在实际使用时按需加载已导入库 |
| device / file | `PlatformUtilityBridge`、现有 DeviceApi | 操作型旧接口保持无返回；相对路径基于脚本工作目录；`getScriptVersion` 读取 `version` 文件 |
| node | `AccessibilityNodePlatformBridge` | 节点用短期句柄；查询和动作都回到当前无障碍树 |

外部依赖只承担现有平台没有等价实现的部分：

- OkHttp：HTTP、文件传输和 WebSocket。
- Android JavaMail / Activation：兼容邮件方法。
- Zip4j：带密码和字符集的 ZIP 解压。
- OpenCV Android AAR：真实 `Mat` 和霍夫圆检测；native 库由扩展页导入后按需加载。
- ONNX Runtime：原生 OCR 与 `PaddleOcr` Java 兼容入口共用 PP-OCRv4 推理会话；核心库和预设模型由
  扩展页导入后按需加载。

## 截图与坐标

取色、点阵识字、模板匹配、找圆和 `cv.snapShot` 必须共享当前引擎截图缓存。这样
`keepCapture`、缓存时长和 `LuaEngine.setSnapCacheBitmap` 对所有图像入口一致生效。

`setScreenScale(true, virtualWidth, virtualHeight)` 的状态位于当前 Lua 状态。只有
`compat_extended.lua` 中明确经过坐标换算的触控、扩展图色、兼容找图和兼容点阵字库入口会在
进入 native 前把区域、点和半径换算为物理坐标，并在返回前换回虚拟坐标；小鱼原生 `m.findColors`、
`m.findPic` 和 `m.font.*` 保持各自的原生坐标契约。`m.findPic` 保持原生 `1..8`
方向；懒人多模板入口使用 `0..4`，只能在最靠近底层调用的位置转换一次。多点找色的
偏移量按尺寸比例缩放，不使用端点坐标比例；`findPicFast` 返回第一个命中模板的
0 起始索引和该模板的全部非重叠坐标。默认 `m` 只接受 `true` / `false`；历史 `1` / `0`
若需要支持，必须在对应兼容命名空间中另行实现，不能再扩展默认 API。

## 无障碍节点生命周期

Java 侧为查询结果分配数值句柄，Lua 节点对象只保存句柄和查询时的字段快照。界面变化后
旧句柄可能不再对应有效节点，脚本应重新查询。`lockNode` 只有在当前确实取得根节点时返回
成功；不能为了兼容返回常量真值。

## 明确不复制的旧系统

- 旧产品专属的悬浮控制栏事件模型、配置页、拖动控件和插件生命周期。
- 已有 `dialog`、`hud`、`web`、`ui` 或 `imgui` 能等价表达的第二套 UI。
- 已有 Lua 5.4 标准库能力的同名伪实现。
- 无法在当前架构中真实执行的空函数、固定成功值或伪造设备信息。
- 旧版本遗留的多套 Root / 无障碍后备路线。

`installLrPkg` 依赖懒人私有更新包和宿主安装生命周期，`getUIConfig`、旧 UI 控件访问、
`setUserEventCallBack`、插件/通知事件及拖动视图依赖旧产品界面或事件总线，
`setHandleEnvMode` 依赖当前引擎不存在的激活句柄运行时。这些入口不导出，也不能用空函数
或固定成功值伪装。`setStopCallBack` 和 `LuaEngine.registerExitCallback` 属于通用脚本
生命周期，已由当前 `LuaRuntime` 真实执行，不在排除范围。

旧 `YoloV5` 与 `m.yolo` 尚未导出；但内部已具备可选 NCNN YOLO 运行时和语言中立 C ABI。公开层
的已核对事实和待定边界见 [Android YOLO 可选运行时调研](ANDROID_YOLO_可选运行时调研.md)，它不构成当前公开能力。`createOcr/ocrText*` 是 Tesseract
句柄体系，当前 RapidOCR 没有同构句柄和白名单语义。因此两组暂不导出。`PaddleOcr` 的
ONNX 路线已有真实等价实现；NCNN 加载入口为保持 Java 调用兼容而存在，但明确返回
`false`，公开文档不得写成已支持 NCNN。

## 脚本结束回调

`setStopCallBack` 的函数引用保存在当前 `LuaRuntime` registry，主任务返回后调度器先停止并
join 全部子线程，再调用回调。结束码固定为 `0` 正常、`1` 主动停止、`2` 运行错误；
`error` 在非正常结束时为 `true`。回调错误只记录日志，不能改写主脚本原始结果。

`LuaEngine.registerExitCallback` 保存 Java 单方法接口代理。退出分发时 native 仍持有 VM Gate，
使 `LuaCallback.nativeInvoke` 走同步重入；不能在主任务结束后把它排入 Lua 回调队列，否则
队列已经没有消费者会造成死锁。Java 侧在调用前取出并清空代理，避免跨脚本泄漏。

## 后续新增规则

1. 先核对当前源码和 `API_契约.md`，再核对懒人精灵、触动精灵公开签名。
2. 系统接口先且只先参考 `T:\老项目\recovered_project\script_1`；只有无法判断时才看更旧项目。
3. 相同能力优先进入现有 core/HostApi；只有平台结构化扩展才使用 `callJson`。
4. 有实质差异时选择更清晰、可长期稳定的一方；只有准备采用两者之外的新签名时才询问用户。
5. 同步更新内部契约、公开函数页、API 总览和 catalog，最后集中运行文档、Lua、C++/Java
   构建检查。
