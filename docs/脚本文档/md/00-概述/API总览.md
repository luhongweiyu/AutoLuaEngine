---
params: ""
returns: ""
---

Lua 仍是动态语言，下面的类型是 API 契约：`integer` 表示整数，`number` 表示整数或小数，
`string` 可承载文本或二进制数据，`table` 表示 Lua 表，`userdata` 表示 native/Java 对象，
`any` 表示任意 Lua 值，类型后的 `?` 表示该值可能为 `nil`。

小鱼精灵默认启用 `m` API。总览使用完整的 `m.xxx` 名称标识 API 归属和层级；函数详情页的
语法与示例则使用实际推荐写法，例如直接调用 `appIsFront()`、`capture(path)` 或 `thread.newThread()`。

| 分类 | 函数 | 参数类型 | 返回值类型 | 说明 |
|---|---|---|---|---|
| 全局 | `print(...)` | `...: any` | 无 | 输出到引擎日志 |
| 全局 | `sleep(ms)` | `ms: integer` | `boolean` | 脚本延时，成功返回 `true` |
| 全局 | `systemTime()` | 无 | `integer` | Unix 时间戳，单位毫秒 |
| 全局 | `tickCount()` | 无 | `integer` | 当前脚本运行时间，单位毫秒 |
| 全局 | `getRunEnvType()` | 无 | `integer` | `0` 为 Root，`1` 为无障碍，`-1` 为未就绪 |
| 设备 | `m.appIsFront()`、`m.getBrand()`、`m.exec()` 等 | 见左侧「设备」分类 | 见设备文档 | 应用管理、硬件信息、系统控制和 Root 命令 |
| 全局 | `useApi(name)` / `switchApi(name)` | `name: string` | `boolean` 或 `nil, string` | 切换全局 API 命名空间 |
| Java | `import(className)` | `className: string` | 无 | 导入 Java 类或包 |
| Java | `LuaEngine.getContext()` | 无 | `userdata` | 返回 Android Application Context |
| Java | `LuaEngine.httpGet(url, headers[, timeout])` | `url: string, headers: table, timeout: integer?` | `string?` | HTTP GET，超时单位秒 |
| Java | `LuaEngine.httpPost(url, params, headers[, timeout])` | `url: string, params: table, headers: table, timeout: integer?` | `string?` | 表单 HTTP POST |
| Java | `LuaEngine.httpPostData(url, data, contentType, timeout)` | `url: string, data: string, contentType: string, timeout: integer` | `string?` | 文本数据 HTTP POST |
| Java | `LuaEngine.loadApk(nameOrPath)` | `nameOrPath: string` | `userdata?` | 加载 APK/JAR/DEX 插件 |
| 多线程 | `m.thread.beginThread(callback, ...)` | `callback: function, ...: any` | 无 | 启动 native 子线程 |
| 多线程 | `m.thread.newThread(callback, ...)` | `callback: function, ...: any` | `userdata` | 启动并返回线程对象 |
| 多线程 | `thread:stopThread()` | 无 | 无 | 停止并等待指定子线程退出 |
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
| 脚本包 | `m.read_alpkg_file(path)` | `path: string` | `string \| nil, string?` | 读取当前 `.alpkg` 的原始资源 |
| 提示 | `m.toast(text[, durationMs])` | `text: any, durationMs: integer?` | `integer \| nil, string?` | 显示自动关闭的 HUD 提示 |
| 日志 | `m.log.print(text)` | `text: string` | `boolean` | 输出日志文本 |
| 输入 | `touchDown(id, x, y)` | `id: integer, x: integer, y: integer` | 无 | 按住不放，仅 Root 模式 |
| 输入 | `touchMove(id, x, y)` | `id: integer, x: integer, y: integer` | `boolean` | 移动手指，仅 Root 模式 |
| 输入 | `touchUp(id)` | `id: integer` | 无 | 弹起手指，仅 Root 模式 |
| 输入 | `keyDown(keycode)` | `keycode: string \| integer` | `boolean` | 按下按键不弹起，仅 Root 模式 |
| 输入 | `keyUp(keycode)` | `keycode: string \| integer` | `boolean` | 弹起按键，仅 Root 模式 |
| 输入 | `keyPress(keycode)` | `keycode: string \| integer` | `boolean` | 按一下按键并弹起，仅 Root 模式 |
| 输入 | `inputText(text)` | `text: string` | `boolean` | 模拟输入文字，仅 Root 模式 |
| 输入法 | `imeLib.lock()` | 无 | `boolean` | 锁定 小鱼精灵 输入法，仅 Root 模式 |
| 输入法 | `imeLib.setText(text)` | `text: string` | `boolean` | 通过已锁定输入法提交 Unicode 文本 |
| 输入法 | `imeLib.unlock()` | 无 | `boolean` | 恢复锁定前的默认输入法，仅 Root 模式 |
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
| 图像 | `m.clearImageCache([picName])` | `picName: string?` | `boolean` | 清理一个或全部模板图片缓存 |
| 图像 | `m.setImageCacheMaxBytes(maxBytes)` | `maxBytes: integer` | `integer` 或 `nil, string` | 设置当前脚本的模板缓存字节上限 |
| OCR | `m.ocr.loadBuiltin([name[, threads]])` | `name: string?, threads: integer?` | `boolean` 或 `nil, string` | 加载 APK 内置中文/英文 PP-OCRv4 mobile 模型 |
| OCR | `m.ocr.load(name, detPath, recPath, clsPath, keysPath[, threads])` | `name, detPath, recPath, keysPath: string, clsPath: string?, threads: integer?` | `boolean` 或 `nil, string` | 显式加载或复用 RapidOCR ONNX 模型 |
| OCR | `m.ocr.release(name)` | `name: string` | `boolean` 或 `nil, string` | 释放一个模型名称持有的引用 |
| OCR | `m.ocr.isLoaded(name)` | `name: string` | `boolean` 或 `nil, string` | 查询模型名称是否已加载 |
| OCR | `m.ocr.read(name, imagePath[, options])` | `name, imagePath: string, options: table?` | `table` 或 `nil, string` | 识别普通图片中的全部文字 |
| OCR | `m.ocr.findText(name, imagePath, text[, options])` | `name, imagePath, text: string, options: table?` | `table` 或 `nil, string` | 在 OCR 结果中查找文字 |
| 点阵字库 | `m.font.setDict(index, dictionary)` | `index: integer, dictionary: string` | `boolean` 或 `nil, string` | 设置可变尺寸点阵字库 |
| 点阵字库 | `m.font.addDict(index, dictionary)` | `index: integer, dictionary: string` | `boolean` 或 `nil, string` | 向字库追加字形 |
| 点阵字库 | `m.font.useDict(index)` | `index: integer` | `boolean` 或 `nil, string` | 选择当前 Lua native 线程的字库 |
| 点阵字库 | `m.font.getFontPixel(x1, y1, x2, y2, color)` | `x1..y2: integer, color: string` | `string` 或 `nil, string` | 从当前截图生成字形点阵 |
| 点阵字库 | `m.font.read(x1, y1, x2, y2, color, sim)` | `x1..y2: integer, color: string, sim: number` | `table` 或 `nil, string` | 返回结构化识字结果 |
| 点阵字库 | `m.font.ocr(x1, y1, x2, y2, color, sim)` | `x1..y2: integer, color: string, sim: number` | `string` 或 `nil, string` | 返回大漠风格文字结果 |
| 点阵字库 | `m.font.ocrEx(x1, y1, x2, y2, color, sim)` | `x1..y2: integer, color: string, sim: number` | `string` 或 `nil, string` | 返回大漠风格文字和坐标 |
| 点阵字库 | `m.font.findStr(x1, y1, x2, y2, text, color, sim)` | `x1..y2: integer, text, color: string, sim: number` | `integer, integer` 或 `nil, string` | 查找第一处目标文字 |
| 点阵字库 | `m.font.findStrEx(x1, y1, x2, y2, text, color, sim)` | `x1..y2: integer, text, color: string, sim: number` | `string` 或 `nil, string` | 返回所有目标文字坐标 |

`m.sleep`、`m.systemTime`、`m.tickCount`、`m.touchDown` 等同名成员与默认全局函数的参数、
返回值一致；`m.ime` 与 `imeLib` 是同一组输入法函数。`m.html` 是 `m.web` 的别名。设备和
应用能力的实现统一归属 `m`，完整清单见左侧「设备」分类。

`lr` / `cd` 当前保留已有的最小兼容映射。后续新增功能默认只进入 `m`，`lr` 和 `cd` 兼容层
单独集中完善，不随每个新功能同步增加。完整的切换规则见「命名空间与别名」。
