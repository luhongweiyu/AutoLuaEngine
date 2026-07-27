# Android ImGui 实现契约

本文档固定小鱼精灵 Android ImGui 的架构、兼容规则和验收边界。实现依据为懒人精灵
`imgui` 文档及其旧版 LSP 元数据；文档检索日期为 2026-07-18。

底层使用官方 Dear ImGui `v1.92.8` 源码，固定存放于 `engines/android/third_party/imgui`。

参考地址：<http://www.lrappsoft.com/lrword/api/android/imgui.html>

## 目标

- 使用开源 Dear ImGui 真实渲染，不用 Android 原生控件伪装 ImGui。
- Dear ImGui、控件模型、事件队列和 C ABI 全部编入 `libengine.so`，不新增业务 SO。
- 透明悬浮 Surface 由 `:engine` 进程中的 Android Service 提供；App 主进程仍只负责 App
  页面和脚本控制悬浮按钮。
- Lua 公开全局 `imgui`，方法名称、参数顺序、索引规则和返回值尽量兼容懒人精灵。
- 后续 JS、Go 绑定复用 `engine_imgui*` C ABI 和同一个事件队列，不复制控件逻辑。

## 文档冲突处理

懒人精灵网页与旧版 LSP 存在少量自相矛盾内容，统一按以下明确规则实现：

1. 控件和窗口句柄统一为大于 `0` 的整数。文档中写作 `string` 的句柄仍按整数接收；无效
   句柄不会隐式转换成其他控件。
2. 组合框、单选框和表格的项、行、列索引从 `0` 开始。
3. 颜色统一使用 `0xAARRGGBB` 整数，尺寸与坐标统一使用屏幕物理像素。
4. `createButton` 的网页标题写成 `(x,y,width,height,text)`，但参数备注及旧版 LSP 使用
   `(parent,label,width,height)`。两种形式都支持，四参数形式作为推荐写法。
5. `addTabBarItem` 的网页返回值写作无，旧版 LSP 又要求用返回句柄承载标签页子控件。实现
   返回标签页句柄；忽略返回值的旧脚本不受影响。
6. `createBitmapShape` 和 `setBitmapShape` 同时接受图片路径与 Java `Bitmap`；固定 C ABI
   使用路径或 RGBA 点阵，Java 对象只在 Lua-Java 动态对象边界转换。
7. 网页编号缺少 `39`、`58`、`72`，不据此虚构三个方法。文档正文反复引用但未单列的
   `getLastError`、`isValidHandle` 和 `post` 作为必要辅助方法实现。
8. `show` 的可选参数按类型识别，分别为一个 `boolean`、一个 `string` 和一个 `number`；
   同类型参数重复属于参数错误。
9. `addRadioBox` 的网页返回值写作无，懒人精灵 `1.7.6` LSP 写作布尔值。实现返回
   `boolean`；忽略返回值的网页写法不受影响。`wrapline` 按单个项目保存，表示下一项是否
   另起一行。
10. `setInputType` 只在类型真正变化时清空已有文本；重复设置当前类型不会清空。
11. `showWindow` 显示时，第一个 ImGui 内容窗口默认填满外层 Surface；显示后仍可通过窗口
    几何方法修改。

## 调用链

```text
Lua imgui.*
  -> runtime/lua/imgui_lua_api
  -> engine_imgui* C ABI
  -> core/api/imgui_api 控件模型与事件队列
  -> platform/imgui_renderer Dear ImGui + EGL/OpenGL ES
  -> :engine/ScriptImGuiService 提供透明 Surface、触摸和输入法
```

渲染线程不得直接调用 Lua。点击、选择、滑块和关闭事件先进入 C++ 队列，再由 Lua 事件泵
取得 VM Gate 后调用已注册回调。回调内可以安全调用其他 `imgui` 方法。

## C ABI 边界

- 顶层 `EngineApi::abiVersion` 为 `18`，仅在函数表尾部新增 `getImGuiApi`。
- `EngineImGuiApi::abiVersion` 为 `1`，结构体字段只能在尾部追加。
- `engine_getImGuiApi()` 与 `engine_getApi()->getImGuiApi()` 返回同一张进程级只读子函数表。
- 每个子函数表成员都有对应的 `engine_imgui*` 直接导出；两种入口调用同一份核心实现。
- 语言回调对象不跨 C ABI。`EngineImGuiEvent` 只携带事件类型、句柄、索引、值和临时文本，
  各语言绑定消费后执行自己的回调对象。
- 句柄只在当前脚本任务内有效；脚本清理或 `engine_imguiReset()` 后不得继续使用。
- 完整稳定声明见 `engines/android/app/src/main/cpp/core/imgui_c_api.h`。

## API 范围

### 框架与窗口

`isSupport`、`show`、`showWindow`、`close`、`setColorTheme`、`createWindow`、
`destroyWindow`、`setOnClose`、`setWindowPos`、`setWindowSize`、`getWindowPos`、
`setWindowFlags`。

### 布局

`createVerticalLayout`、`createHorticalLayout`、`createTreeBoxLayout`、`createTabBar`、
`addTabBarItem`、`sameLine`、`setLayoutBorderVisible`。

### 基础控件

`createButton`、`setOnClick`、`createLabel`、`createCheckBox`、`createSwitch`、
`setChecked`、`isChecked`、`setOnCheck`、`createInputText`、`getInputText`、
`setInputText`、`setInputType`、`createProgressBar`、`setProgressBarPos`、
`getProgressBarPos`、`createSlider`、`setSlider`、`getSliderPos`、`setOnSliderEvent`、
`createColorPicker`。

### 选择与表格

`createComboBox`、`addOptionItem`、`getItemText`、`removeItemAt`、`removeAllItems`、
`getSelectedItemIndex`、`setItemSelected`、`setOnSelectEvent`、`createRadioGroup`、
`addRadioBox`、`createTableView`、`setTableHeaderItem`、`insertTableRow`、
`getTableItemText`、`setTableItemText`、`deleteTableRow`、`clearTable`、`getItemCount`、
`setOnSelectEventEx`。

### 图片、样式与可见性

`createImage`、`setImage`、`setImageFromBitmap`、`setWidgetSize`、`setWidgetVisible`、
`isWidgetVisible`、`setWidgetStyle`、`setWidgetColor`。

### 绘图图形

`createRectangle`、`createCircle`、`createPolygon`、`createLine`、`createBitmapShape`、
`createShapeText`、`setShapePosition`、`setShapeVisibility`、`isShapeVisibility`、
`setShapeTextString`、`setShapeTextColor`、`setShapeTextBackground`、
`setShapeTextFontScale`、`setBitmapShape`、`setShapeThickness`、`removeShape`。

### 辅助方法

`getLastError`、`isValidHandle`、`post`。`post(callback)` 把回调加入 Lua 事件泵，避免
脚本依赖 Android UI 线程执行 Lua。

## 显示语义

- `show(true, ...)` 创建全屏透明可触摸画布，并阻塞当前 Lua 任务直到 `close()`。
- `show(false, ...)` 创建全屏不可触摸画布并立即返回。
- `showWindow(config)` 创建指定位置和尺寸的小型悬浮画布并立即返回，事件由内部 Lua
  子任务持续处理。
- 同一脚本任务只维护一个 ImGui 框架和一个渲染 Surface；重复显示会更新窗口模式，不会
  创建多个渲染线程。
- 脚本结束、停止、强停进程或 `close()` 都必须停止渲染、移除悬浮窗口并释放 Lua 回调。

## 并发与性能

- 渲染线程独占 EGL、OpenGL ES 与 Dear ImGui context。
- Lua/C ABI 只修改持久控件模型；渲染帧在短锁内读取模型，不持有 VM Gate。
- 图片点阵通过共享所有权进入不可变快照；GL 纹理按控件句柄和图片版本复用，替换或删除
  控件后由渲染线程释放。
- 事件队列以 `512` 条作为高频事件软上限；连续滑块事件按控件合并或淘汰，按钮、选择和
  关闭等离散操作不会为了硬性限长而被静默丢弃。
- 未使用 ImGui 时不创建 Service、Surface、渲染线程或 EGL context。

## 验收

1. Android 四个 ABI 均能编译并只生成一个 `libengine.so` 业务库。
2. Root 设备可显示中文窗口、按钮、输入框、组合框、表格、图片和图形。
3. 触摸坐标、软键盘输入、按钮/选择/滑块/关闭回调可用，回调内更新控件不死锁。
4. `show(true)` 可由按钮回调关闭并正常返回；`showWindow` 返回后回调仍可执行。
5. 脚本停止后 ImGui 消失，重新运行脚本不保留旧句柄或旧回调。
6. 无 ImGui 脚本运行时，原有截图、输入、多线程、HUD、Web 和脚本控制回归测试保持通过。
