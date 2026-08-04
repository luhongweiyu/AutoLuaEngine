---
params: ""
returns: ""
---

Lua 仍是动态语言，下面的类型是 API 契约：`integer` 表示整数，`number` 表示整数或小数，
`string` 可承载文本或二进制数据，`table` 表示 Lua 表，`userdata` 表示 native/Java 对象，
`any` 表示任意 Lua 值，类型后的 `?` 表示该值可能为 `nil`。

小鱼精灵默认启用 `m` API。总览使用完整的 `m.xxx` 名称标识 API 归属和层级；函数详情页的
语法与示例则使用实际推荐写法，例如直接调用 `appIsFront()`、`capture(path)` 或 `thread.newThread()`。

常用脚本通常按“确认运行环境/应用 → 读取截图 → 找图或找色 → 输入操作 → 等待或循环”的顺序
组织；交互式目录按这个顺序展示命令。扩展、模型和兼容入口放在各自分类的后段。

| 分类 | 函数 | 参数类型 | 返回值类型 | 说明 |
|---|---|---|---|---|
| 日志 | `print(...)` / `printEx(...)` | `...: any` | 无 | 输出到引擎日志 |
| 设备 | `sleep(ms)` | `ms: integer` | `boolean` | 脚本延时，成功返回 `true` |
| 设备 | `systemTime()` | 无 | `integer` | Unix 时间戳，单位毫秒 |
| 设备 | `tickCount()` | 无 | `integer` | 当前脚本运行时间，单位毫秒 |
| 设备 | `m.appIsFront()`、`m.getBrand()`、`m.exec()` 等 | 见左侧「设备」分类 | 见设备文档 | 应用管理、硬件信息、系统控制和 Root 命令 |
| 设备 | `m.getRunEnvType()`（可直接写 `getRunEnvType()`） | 无 | `integer` | `0` 为 Root，`1` 为无障碍，`-1` 为未就绪 |
| 设备 | `m.readPasteboard()` | 无 | `string` 或 `nil, string` | 读取系统文本剪贴板；没有文本为空字符串，平台失败附带原因 |
| 设备 | `m.writePasteboard(text[, kind])` | `text: string, kind: integer?` | 无 | 写入系统文本剪贴板；Android 的 `kind` 只能省略或为 `0` |
| 设备 | `m.getScriptVersion()` | 无 | `integer` | 读取脚本工作目录的 `version` 整数 |
| 日志 | `setLogOff(disabled)` | `disabled: boolean` | 无 | 关闭或恢复普通 `print` 输出 |
| 设备 | `playAudio(path)` / `stopAudio()` / `scanImage(path)` | 文件路径 | 无 | 媒体播放、停止和图片媒体库扫描 |
| 设备 | `zip(source, zipPath)` / `unZip(zipPath, output[, password[, charset]])` | 文件路径与可选解密参数 | 无 | 创建或解压 ZIP 文件 |
| 设备 | `extractApkAssets(asset, output)` / `extractAssets(assetArchive, output[, pattern])` | assets 路径与输出路径 | 无 | 提取 APK 内置资源 |
| 设备 | `setDpiToVir(dpi)` / `setDpiToRealy()` | `dpi: integer` | 无 | 设置或恢复显示密度 |
| 设备 | `showControlBar(show)` / `setControlBarPosNew(x, y)` | 显示状态与相对坐标 | 无 | 控制小鱼精灵控制栏 |
| 设备 | `restartScript()` / `setTimer(callback, delay, ...)` | 回调、毫秒和透传参数 | 无 | 重启脚本或延时执行回调 |
| 设备 | `setRootEnvMode(enabled)` / `setAccessibilityEnvMode()` | `enabled: boolean` | 无 | 请求切换 Root 或无障碍运行环境 |
| 概述 | `useApi(name)` / `switchApi(name)` | `name: string` | `boolean` 或 `nil, string` | 切换全局 API 命名空间 |
| Java | `import(className)` | `className: string` | 无 | 导入 Java 类或包 |
| Java | `LuaEngine.getContext()` | 无 | `userdata` | 返回 Android Application Context |
| Java | `LuaEngine.httpGet(url, headers[, timeout])` | `url: string, headers: table, timeout: integer?` | `string?` | HTTP GET，超时单位秒 |
| Java | `LuaEngine.httpPost(url, params, headers[, timeout])` | `url: string, params: table, headers: table, timeout: integer?` | `string?` | 表单 HTTP POST |
| Java | `LuaEngine.httpPostData(url, data, contentType, timeout)` | `url: string, data: string, contentType: string, timeout: integer` | `string?` | 文本数据 HTTP POST |
| Java | `LuaEngine.loadApk(nameOrPath)` | `nameOrPath: string` | `userdata?` | 加载 APK/JAR/DEX 插件 |
| Java | `LuaEngine.sendMail*` | SMTP、正文、可选附件与回调 | 无 | 异步发送邮件或附件 |
| Java | `PaddleOcr.loadModel/loadOnnxModel/detect*` | 模型路径或 Bitmap | `boolean` / `string?` | 复用当前 PP-OCRv4 ONNX 推理核心 |
| Java | `LuaEngine.registerExitCallback(callback)` | `callback: function` | 无 | 注册当前 Lua 运行时的结束回调 |
| 多线程 | `m.thread.beginThread(callback, ...)` | `callback: function, ...: any` | 无 | 启动 native 子线程 |
| 多线程 | `m.thread.newThread(callback, ...)` | `callback: function, ...: any` | `userdata` | 启动并返回线程对象 |
| 多线程 | `thread:stopThread()` | 无 | 无 | 停止并等待指定子线程退出 |
| 多线程 | `setMainThreadPause()` / `setMainThreadResume()` | 无 | `boolean` | 暂停或恢复主脚本任务，子线程继续运行 |
| 多线程 | `setStopCallBack(callback)` | `callback: function` | 无 | 主任务和子线程结束后的生命周期回调 |
| 原生界面 | `m.dialog.alert(title, message[, buttonText])` | `title: string, message: string, buttonText: string?` | `boolean \| nil, string?` | 原生提示框 |
| 原生界面 | `m.dialog.confirm(title, message[, positiveText[, negativeText]])` | `title: string, message: string, positiveText: string?, negativeText: string?` | `boolean \| nil, string?` | 原生确认框 |
| 原生界面 | `m.dialog.input(title, hint[, defaultText[, options]])` | `title: string, hint: string, defaultText: string?, options: table?` | `string?` 或 `nil, string` | 原生输入框 |
| 原生界面 | `m.dialog.select(title, items[, selectedIndex[, options]])` | `title: string, items: table, selectedIndex: integer?, options: table?` | `integer, string` 或 `nil, string?` | 原生单选框 |
| 原生界面 | `m.ui.form(spec)` | `spec: table` | `table?` 或 `nil, string` | 原生多字段表单 |
| HUD | `m.hud.show(id, spec)` | `id: any, spec: table` | `integer \| nil, string?` | 创建 HUD |
| HUD | `m.hud.update(id, patch)` | `id: any, patch: table` | `boolean \| nil, string?` | 更新 HUD |
| HUD | `m.hud.hide(id)` | `id: any` | `boolean \| nil, string?` | 关闭 HUD |
| HUD | `m.hud.waitEvent(id[, timeoutMs])` | `id: any, timeoutMs: integer?` | `table \| nil, string?` | 等待 HUD 事件 |
| HTML | `m.web.open(spec)` | `spec: table` | `integer \| nil, string?` | 打开 WebView/HTML 界面 |
| HTML | `m.web.waitEvent(handle[, timeoutMs])` | `handle: integer, timeoutMs: integer?` | `table \| nil, string?` | 等待页面事件 |
| HTML | `m.web.postMessage(handle, data)` | `handle: integer, data: any` | `boolean \| nil, string?` | 向页面发送 JSON 数据 |
| HTML | `m.web.close(handle)` | `handle: integer` | `boolean \| nil, string?` | 关闭页面 |
| ImGui | `imgui.show([touchable], [font], [fontSize])` | `boolean?, string?, number?`，顺序不限 | `boolean` | 显示全屏 Dear ImGui Surface；可触摸时阻塞至关闭 |
| ImGui | `imgui.showWindow(config)` | `config: table` | `boolean` | 显示可拖动、收起和缩放的独立悬浮 Surface |
| ImGui | `imgui.createWindow(...)`、布局和控件 | 见左侧「ImGui」分类 | `integer` 或 `nil` | Dear ImGui 窗口、布局、输入、选择、表格和图片 |
| ImGui | `imgui.createRectangle(...)` 等 | 坐标、颜色和图形参数 | `integer` 或 `nil` | 在当前 Surface 绘制图形、位图和文本 |
| ImGui | `imgui.setOnClick(...)` 等 | `handle: integer, callback: function?` | 无 | 在脚本运行环境中依次执行交互回调 |
| 脚本包 | `m.read_alpkg_file(path)` | `path: string` | `string \| nil, string?` | 读取当前 `.alpkg` 的原始资源 |
| 提示 | `m.toast(text[, durationMs])` | `text: any, durationMs: integer?` | `integer \| nil, string?` | 显示自动关闭的 HUD 提示 |
| 输入 | `touchDown([id,] x, y)` | `id: integer?, x: integer, y: integer` | 无 | 按住不放，仅 Root 模式 |
| 输入 | `touchMove([id,] x, y)` | `id: integer?, x: integer, y: integer` | 无 | 移动手指，仅 Root 模式 |
| 输入 | `touchUp([id,] x, y)` | `id: integer?, x: integer, y: integer` | 无 | 在给定坐标抬起手指，仅 Root 模式 |
| 输入 | `keyDown(keycode)` | `keycode: string \| integer` | `boolean` | 按下按键不弹起，仅 Root 模式 |
| 输入 | `keyUp(keycode)` | `keycode: string \| integer` | `boolean` | 弹起按键，仅 Root 模式 |
| 输入 | `keyPress(keycode)` | `keycode: string \| integer` | `boolean` | 按一下按键并弹起，仅 Root 模式 |
| 输入 | `inputText(text)` | `text: string` | `boolean` | 模拟输入文字，仅 Root 模式 |
| 输入法 | `m.ime.lock()` | 无 | `boolean` | 锁定 小鱼精灵 输入法，仅 Root 模式 |
| 输入法 | `m.ime.setText(text)` | `text: string` | `boolean` | 通过已锁定输入法提交 Unicode 文本 |
| 输入法 | `m.ime.unlock()` | 无 | `boolean` | 恢复锁定前的默认输入法，仅 Root 模式 |
| 输入法 | `m.ime.deleteChar()` | 无 | `boolean` | 删除当前输入法中的一个字符 |
| 输入法 | `m.ime.finishInput()` | 无 | `boolean` | 完成当前输入法提交 |
| 输入法 | `m.ime.keyEvent(action, keyCode)` | `action, keyCode: integer` | `boolean` | 向当前输入法发送 Android 按键事件 |
| 图像 | `m.getScreenPixels()` | 无 | `integer, integer, integer` 或 `nil, string` | 返回宽、高和点阵地址 |
| 图像 | `m.setScreenPixels(imagePath)` | `imagePath: string` | `boolean` 或 `nil, string` | 把图片设置为固定屏幕点阵 |
| 图像 | `m.restoreScreenPixels()` | 无 | `boolean` | 还原物理屏幕点阵 |
| 图像 | `m.capture(path[, left, top, right, bottom])` | `path: string, left..bottom: integer?` | `boolean` 或 `nil, string` | 保存全屏或指定区域截图 |
| 图像 | `m.snapShot(path[, left, top, right, bottom])` | 同 `m.capture` | 同 `m.capture` | `m.capture` 的完整别名 |
| 图像 | `m.keepCapture()` | 无 | `boolean` | 锁住当前截图帧 |
| 图像 | `m.releaseCapture()` | 无 | `boolean` | 取消锁帧，恢复按时间缓存 |
| 图像 | `m.setCaptureCacheMs(ms)` | `ms: integer` | `integer \| nil, string?` | 设置并返回截图缓存时间 |
| 找色 | `m.findColors(x1, y1, x2, y2, dir, sim, colors)` | `x1..y2: integer, dir: integer, sim: integer, colors: string` | `integer, integer` 或 `nil, string` | 在当前截图缓存上多点找色 |
| 图像 | `m.findPic(x1, y1, x2, y2, picName, deltaColor, dir, sim)` | `x1..y2: integer, picName: string, deltaColor: string, dir: integer, sim: number` | `integer, integer` 或 `nil, string` | 在当前截图缓存中查找模板图片 |
| 图像 | `m.findPicAll(x1, y1, x2, y2, picName, deltaColor, dir, sim)` | 同 `m.findPic` | `table` 或 `nil, string` | 返回全部非重叠模板命中 |
| 图像 | `m.clearImageCache([picName])` | `picName: string?` | `boolean` | 清理一个或全部模板图片缓存 |
| 图像 | `m.setImageCacheMaxBytes(maxBytes)` | `maxBytes: integer` | `integer` 或 `nil, string` | 设置当前脚本的模板缓存字节上限 |
| 图像 | `m.findPicEx` / `m.findImage` | 区域、模板列表、相似度 | `name, x, y` 或 `nil, -1, -1` | 兼容多模板找图 |
| 图像 | `m.findPicAllPoint` / `m.findPicFast` | 区域、模板或模板列表 | 坐标 table 或索引与坐标 table | 兼容的全部和快速多模板找图 |
| OCR | `m.ocr.loadBuiltin([name[, threads]])` | `name: string?, threads: integer?` | `boolean` 或 `nil, string` | 加载预设中文/英文 PP-OCRv4 mobile 模型（需先导入运行时和模型文件） |
| OCR | `m.ocr.load(name, detPath, recPath, clsPath, keysPath[, threads])` | `name, detPath, recPath, keysPath: string, clsPath: string?, threads: integer?` | `boolean` 或 `nil, string` | 显式加载或复用 RapidOCR ONNX 模型 |
| OCR | `m.ocr.release(name)` | `name: string` | `boolean` 或 `nil, string` | 释放一个模型名称持有的引用 |
| OCR | `m.ocr.isLoaded(name)` | `name: string` | `boolean` 或 `nil, string` | 查询模型名称是否已加载 |
| OCR | `m.ocr.read(name, imagePath[, options])` | `name, imagePath: string, options: table?` | `table` 或 `nil, string` | 识别普通图片中的全部文字 |
| OCR | `m.ocr.findText(name, imagePath, text[, options])` | `name, imagePath, text: string, options: table?` | `table` 或 `nil, string` | 在 OCR 结果中查找文字 |
| YOLO | `m.yolo.runtimeInfo()` / `isAvailable()` | 无 | `table`、`boolean` 或 `nil, string` | 查询可选 YOLO 运行时的导入与加载状态 |
| YOLO | `m.yolo.load(name, labelsPath, paramPath, binPath[, loadOptions])` | 路径与加载配置 | `boolean` 或 `nil, string` | 加载或复用命名 YOLOv5 NCNN 模型 |
| YOLO | `m.yolo.init(labelsPath, paramPath, binPath[, loadOptions])` | 路径与加载配置 | `boolean` 或 `nil, string` | 使用固定名称 `default` 初始化单模型流程 |
| YOLO | `m.yolo.release([name])` / `isLoaded([name])` | `name: string?` | `boolean` 或 `nil, string` | 释放或查询模型；省略名称时使用 `default` |
| YOLO | `m.yolo.detectScreen(name[, detectOptions])` | 模型名称与检测配置 | `table` 或 `nil, string` | 检测完整当前屏幕 |
| YOLO | `m.yolo.detectScreen(name, left, top, right, bottom[, detectOptions])` | 模型名称、区域与检测配置 | `table` 或 `nil, string` | 检测屏幕左闭右开区域，结果坐标仍相对完整屏幕 |
| YOLO | `m.yolo.detectFile(name, imagePath[, detectOptions])` | 模型名称、图片路径与检测配置 | `table` 或 `nil, string` | 检测普通图片文件 |
| YOLO | `m.yolo.detect()` / `detect(detectOptions)` / `detect(imagePath[, detectOptions])` | 默认模型与可选图片路径 | `table` 或 `nil, string` | 使用 `default` 模型检测屏幕或图片 |
| 点阵字库 | `m.font.setDict(index, dictionary)` | `index: integer, dictionary: string` | `boolean` 或 `nil, string` | 设置可变尺寸点阵字库 |
| 点阵字库 | `m.font.addDict(index, dictionary)` | `index: integer, dictionary: string` | `boolean` 或 `nil, string` | 向字库追加字形 |
| 点阵字库 | `m.font.useDict(index)` | `index: integer` | `boolean` 或 `nil, string` | 选择当前 Lua native 线程的字库 |
| 点阵字库 | `m.font.getFontPixel(x1, y1, x2, y2, color)` | `x1..y2: integer, color: string` | `string` 或 `nil, string` | 从当前截图生成字形点阵 |
| 点阵字库 | `m.font.read(x1, y1, x2, y2, color, sim)` | `x1..y2: integer, color: string, sim: number` | `table` 或 `nil, string` | 返回结构化识字结果 |
| 点阵字库 | `m.font.ocr(x1, y1, x2, y2, color, sim)` | `x1..y2: integer, color: string, sim: number` | `string` 或 `nil, string` | 返回大漠风格文字结果 |
| 点阵字库 | `m.font.ocrEx(x1, y1, x2, y2, color, sim)` | `x1..y2: integer, color: string, sim: number` | `string` 或 `nil, string` | 返回大漠风格文字和坐标 |
| 点阵字库 | `m.font.findStr(x1, y1, x2, y2, text, color, sim)` | `x1..y2: integer, text, color: string, sim: number` | `integer, integer` 或 `nil, string` | 查找第一处目标文字 |
| 点阵字库 | `m.font.findStrEx(x1, y1, x2, y2, text, color, sim)` | `x1..y2: integer, text, color: string, sim: number` | `string` 或 `nil, string` | 返回所有目标文字坐标 |
| 点阵字库 | `m.font.findStrFast(x1, y1, x2, y2, text, color, sim)` | `x1..y2: integer, text, color: string, sim: number` | `integer, integer` 或 `nil, string` | 只搜索目标字形并返回第一处坐标 |
| 点阵字库 | `m.font.findStrFastEx(x1, y1, x2, y2, text, color, sim)` | `x1..y2: integer, text, color: string, sim: number` | `string` 或 `nil, string` | 只搜索目标字形并返回全部坐标 |
| 点阵字库 | `m.setDict` / `m.useDict` / `m.ocr` / `m.ocrj` / `m.findStr*` | 见「点阵字库 / 兼容入口」 | `1`、`0`、文字或 JSON | 迁移旧脚本的点阵字库兼容入口 |
| 安全与数据 | `cryptLib.aes_*`、`cryptLib.rsa_*` | 见「安全与数据 / 加密」 | 二进制字符串或 PEM | AES、RSA 和安全随机密钥 |
| 网络与通信 | `m.httpGet` / `m.httpPost` / `m.asynHttpGet` / `m.asynHttpPost` | URL、正文、超时、请求头 | 正文、状态或线程对象 | 项目 HTTP 同步和异步请求 |
| 网络与通信 | `m.downloadFile` / `m.uploadFile` | URL、本地路径与可选超时 | 状态或响应正文 | 文件下载和 multipart 上传 |
| 网络与通信 | `m.startWebSocket` / `m.sendWebSocket` / `m.closeWebSocket` | URL、句柄、文本与回调 | 句柄或 boolean | WebSocket 事件连接 |
| 网络与通信 | `require("socket")`、`require("socket.http")`、`require("ssl.https")` | 见「网络与通信 / LuaSocket」 | module、socket 或响应 | 固定 LuaSocket 3.1.0 模块与 HTTPS 请求兼容入口 |
| 输入 | `setScreenScale`、`touchDown`、`touchMove`、`touchUp`、`tap`、`longTap`、`swipe` | 见「输入 / 坐标缩放与手势」 | 无 | 虚拟坐标与常用手势 |
| 找色 | `m.getPixelColor` / `m.getScreenPixel` / `m.colorToRGB` / `m.colorDiff` | 坐标或颜色 | 颜色、通道或像素 table | 取色与颜色转换 |
| 找色 | `m.cmpColor` / `m.cmpColorEx` / `m.getColorNum` | 坐标、颜色、相似度 | `1`、`0` 或数量 | 单点、多点比色与统计 |
| 找色 | `m.findColors` / `m.findColor` / `m.findMultiColor` / `m.findMultiColorAll` | 区域、颜色规则与方向 | 坐标、颜色或 table | 原生和兼容找色 |
| 找色 | `m.isDisplayDead` / `m.findCircle` | 区域、等待时间或霍夫圆参数 | boolean 或圆形 table | 画面静止检测与圆形分析 |
| 图像 / 运行时 | `cv.snapShot`、`cv.new/get/set*`、`cv.deletePtr`、`import("org.opencv.*")`、`ffi.cdef`、`ffi.load` | 见「图像 / OpenCV」和「运行时扩展」 | userdata、table 或标量 | OpenCV `Mat`、兼容值指针、Java API 与 CFFI（声明、结构体、数组、浮点、回调和可变参数） |
| 无障碍 | 选择器、查询、节点信息、节点动作、`nodeLib.*` | 见「无障碍 / 节点自动化」 | selector、node、table 或状态 | 查询和操作 Android 无障碍节点 |

`m.sleep`、`m.systemTime`、`m.tickCount`、`m.touchDown` 等同名成员与默认全局函数的参数、
返回值一致；`m.ime` 是正式输入法模块，默认全局 `ime` 指向同一张表。`m.html` 是 `m.web`
的别名。设备和应用能力统一归属 `m`，完整清单见左侧「设备」分类。

`lr` / `cd` 是迁移既有脚本的独立命名空间，后续兼容映射不改变 `m` 的正式契约。完整的切换
规则见「命名空间与别名」，扩展能力按左侧对应功能分类查看。
