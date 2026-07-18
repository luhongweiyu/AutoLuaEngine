---
params: ""
returns: ""
---

# ImGui 完整 C ABI 对应关系

`EngineImGuiApi* imguiApi = engine_getImGuiApi()` 返回 ImGui 子函数表。下表中的直接导出和
子函数表成员最终进入同一份 `core/api/imgui_api` 模型，不是两套实现。

## 框架与窗口

| Lua | 直接 C ABI | `EngineImGuiApi` 成员 |
|---|---|---|
| `imgui.isSupport()` | `engine_imguiIsSupport()` | `imguiApi->isSupport()` |
| `imgui.show(...)` | `engine_imguiShow(&config)` | `imguiApi->show(&config)` |
| `imgui.showWindow(config)` | `engine_imguiShow(&windowedConfig)` | `imguiApi->show(&windowedConfig)` |
| `imgui.close()` | `engine_imguiClose()` | `imguiApi->close()` |
| `imgui.setColorTheme(style)` | `engine_imguiSetColorTheme(style)` | `imguiApi->setColorTheme(style)` |
| `imgui.createWindow(...)` | `engine_imguiCreateWindow(...)` | `imguiApi->createWindow(...)` |
| `imgui.destroyWindow(handle)` | `engine_imguiDestroyWindow(handle)` | `imguiApi->destroyWindow(handle)` |
| `imgui.setWindowPos(...)` | `engine_imguiSetWindowPos(...)` | `imguiApi->setWindowPos(...)` |
| `imgui.setWindowSize(...)` | `engine_imguiSetWindowSize(...)` | `imguiApi->setWindowSize(...)` |
| `imgui.getWindowPos(handle)` | `engine_imguiGetWindowPos(handle, &geometry)` | `imguiApi->getWindowPos(...)` |
| `imgui.setWindowFlags(...)` | `engine_imguiSetWindowFlags(...)` | `imguiApi->setWindowFlags(...)` |

`show` 和 `showWindow` 的差别由 `EngineImGuiSurfaceConfig.windowed` 表达，所以共用一个 C ABI，
不重复增加 `engine_imguiShowWindow`。

## 布局与基础控件

| Lua | 直接 C ABI | `EngineImGuiApi` 成员 |
|---|---|---|
| `imgui.createVerticalLayout(...)` | `engine_imguiCreateVerticalLayout(...)` | `imguiApi->createVerticalLayout(...)` |
| `imgui.createHorticalLayout(...)` | `engine_imguiCreateHorticalLayout(...)` | `imguiApi->createHorticalLayout(...)` |
| `imgui.createTreeBoxLayout(...)` | `engine_imguiCreateTreeBoxLayout(...)` | `imguiApi->createTreeBoxLayout(...)` |
| `imgui.createTabBar(...)` | `engine_imguiCreateTabBar(...)` | `imguiApi->createTabBar(...)` |
| `imgui.addTabBarItem(...)` | `engine_imguiAddTabBarItem(...)` | `imguiApi->addTabBarItem(...)` |
| `imgui.sameLine(...)` | `engine_imguiSameLine(...)` | `imguiApi->sameLine(...)` |
| `imgui.setLayoutBorderVisible(...)` | `engine_imguiSetLayoutBorderVisible(...)` | `imguiApi->setLayoutBorderVisible(...)` |
| `imgui.createButton(...)` | `engine_imguiCreateButton(...)` | `imguiApi->createButton(...)` |
| `imgui.createLabel(...)` | `engine_imguiCreateLabel(...)` | `imguiApi->createLabel(...)` |
| `imgui.createCheckBox(...)` | `engine_imguiCreateCheckBox(...)` | `imguiApi->createCheckBox(...)` |
| `imgui.createSwitch(...)` | `engine_imguiCreateSwitch(...)` | `imguiApi->createSwitch(...)` |
| `imgui.setChecked(...)` | `engine_imguiSetChecked(...)` | `imguiApi->setChecked(...)` |
| `imgui.isChecked(...)` | `engine_imguiIsChecked(..., &value)` | `imguiApi->isChecked(...)` |
| `imgui.createInputText(...)` | `engine_imguiCreateInputText(...)` | `imguiApi->createInputText(...)` |
| `imgui.getInputText(...)` | `engine_imguiGetInputText(...)` | `imguiApi->getInputText(...)` |
| `imgui.setInputText(...)` | `engine_imguiSetInputText(...)` | `imguiApi->setInputText(...)` |
| `imgui.setInputType(...)` | `engine_imguiSetInputType(...)` | `imguiApi->setInputType(...)` |
| `imgui.createProgressBar(...)` | `engine_imguiCreateProgressBar(...)` | `imguiApi->createProgressBar(...)` |
| `imgui.setProgressBarPos(...)` | `engine_imguiSetProgressBarPos(...)` | `imguiApi->setProgressBarPos(...)` |
| `imgui.getProgressBarPos(...)` | `engine_imguiGetProgressBarPos(..., &value)` | `imguiApi->getProgressBarPos(...)` |
| `imgui.createSlider(...)` | `engine_imguiCreateSlider(...)` | `imguiApi->createSlider(...)` |
| `imgui.setSlider(...)` | `engine_imguiSetSlider(...)` | `imguiApi->setSlider(...)` |
| `imgui.getSliderPos(...)` | `engine_imguiGetSliderPos(..., &value)` | `imguiApi->getSliderPos(...)` |
| `imgui.createColorPicker(...)` | `engine_imguiCreateColorPicker(...)` | `imguiApi->createColorPicker(...)` |

## 选择与表格

| Lua | 直接 C ABI | `EngineImGuiApi` 成员 |
|---|---|---|
| `imgui.createComboBox(...)` | `engine_imguiCreateComboBox(...)` | `imguiApi->createComboBox(...)` |
| `imgui.addOptionItem(...)` | `engine_imguiAddOptionItem(...)` | `imguiApi->addOptionItem(...)` |
| `imgui.getItemText(...)` | `engine_imguiGetItemText(...)` | `imguiApi->getItemText(...)` |
| `imgui.removeItemAt(...)` | `engine_imguiRemoveItemAt(...)` | `imguiApi->removeItemAt(...)` |
| `imgui.removeAllItems(...)` | `engine_imguiRemoveAllItems(...)` | `imguiApi->removeAllItems(...)` |
| `imgui.getSelectedItemIndex(...)` | `engine_imguiGetSelectedItemIndex(...)` | `imguiApi->getSelectedItemIndex(...)` |
| `imgui.setItemSelected(...)` | `engine_imguiSetItemSelected(...)` | `imguiApi->setItemSelected(...)` |
| `imgui.createRadioGroup(...)` | `engine_imguiCreateRadioGroup(...)` | `imguiApi->createRadioGroup(...)` |
| `imgui.addRadioBox(...)` | `engine_imguiAddRadioBox(...)` | `imguiApi->addRadioBox(...)` |
| `imgui.getItemCount(...)` | `engine_imguiGetItemCount(...)` | `imguiApi->getItemCount(...)` |
| `imgui.createTableView(...)` | `engine_imguiCreateTableView(...)` | `imguiApi->createTableView(...)` |
| `imgui.setTableHeaderItem(...)` | `engine_imguiSetTableHeaderItem(...)` | `imguiApi->setTableHeaderItem(...)` |
| `imgui.insertTableRow(...)` | `engine_imguiInsertTableRow(...)` | `imguiApi->insertTableRow(...)` |
| `imgui.getTableItemText(...)` | `engine_imguiGetTableItemText(...)` | `imguiApi->getTableItemText(...)` |
| `imgui.setTableItemText(...)` | `engine_imguiSetTableItemText(...)` | `imguiApi->setTableItemText(...)` |
| `imgui.deleteTableRow(...)` | `engine_imguiDeleteTableRow(...)` | `imguiApi->deleteTableRow(...)` |
| `imgui.clearTable(...)` | `engine_imguiClearTable(...)` | `imguiApi->clearTable(...)` |

## 图片、样式与图形

| Lua | 直接 C ABI | `EngineImGuiApi` 成员 |
|---|---|---|
| `imgui.setWidgetSize(...)` | `engine_imguiSetWidgetSize(...)` | `imguiApi->setWidgetSize(...)` |
| `imgui.setWidgetVisible(...)` | `engine_imguiSetWidgetVisible(...)` | `imguiApi->setWidgetVisible(...)` |
| `imgui.isWidgetVisible(...)` | `engine_imguiIsWidgetVisible(..., &value)` | `imguiApi->isWidgetVisible(...)` |
| `imgui.setWidgetStyle(...)` | `engine_imguiSetWidgetStyle(...)` | `imguiApi->setWidgetStyle(...)` |
| `imgui.setWidgetColor(...)` | `engine_imguiSetWidgetColor(...)` | `imguiApi->setWidgetColor(...)` |
| `imgui.createImage(path)` | `engine_imguiCreateImage(...)` | `imguiApi->createImage(...)` |
| `imgui.setImage(path)` | `engine_imguiSetImage(...)` | `imguiApi->setImage(...)` |
| `imgui.setImageFromBitmap(...)` | `engine_imguiSetImageRgba(...)` | `imguiApi->setImageRgba(...)` |
| `imgui.createRectangle(...)` | `engine_imguiCreateRectangle(...)` | `imguiApi->createRectangle(...)` |
| `imgui.createCircle(...)` | `engine_imguiCreateCircle(...)` | `imguiApi->createCircle(...)` |
| `imgui.createPolygon(...)` | `engine_imguiCreatePolygon(...)` | `imguiApi->createPolygon(...)` |
| `imgui.createLine(...)` | `engine_imguiCreateLine(...)` | `imguiApi->createLine(...)` |
| `imgui.createBitmapShape(path)` | `engine_imguiCreateBitmapShape(...)` | `imguiApi->createBitmapShape(...)` |
| `imgui.createBitmapShape(Bitmap)` | `engine_imguiCreateBitmapShapeRgba(...)` | `imguiApi->createBitmapShapeRgba(...)` |
| `imgui.createShapeText(...)` | `engine_imguiCreateShapeText(...)` | `imguiApi->createShapeText(...)` |
| `imgui.setShapePosition(...)` | `engine_imguiSetShapePosition(...)` | `imguiApi->setShapePosition(...)` |
| `imgui.setShapeVisibility(...)` | `engine_imguiSetShapeVisibility(...)` | `imguiApi->setShapeVisibility(...)` |
| `imgui.isShapeVisibility(...)` | `engine_imguiIsShapeVisibility(..., &value)` | `imguiApi->isShapeVisibility(...)` |
| `imgui.setShapeTextString(...)` | `engine_imguiSetShapeTextString(...)` | `imguiApi->setShapeTextString(...)` |
| `imgui.setShapeTextColor(...)` | `engine_imguiSetShapeTextColor(...)` | `imguiApi->setShapeTextColor(...)` |
| `imgui.setShapeTextBackground(...)` | `engine_imguiSetShapeTextBackground(...)` | `imguiApi->setShapeTextBackground(...)` |
| `imgui.setShapeTextFontScale(...)` | `engine_imguiSetShapeTextFontScale(...)` | `imguiApi->setShapeTextFontScale(...)` |
| `imgui.setBitmapShape(path)` | `engine_imguiSetBitmapShape(...)` | `imguiApi->setBitmapShape(...)` |
| `imgui.setBitmapShape(Bitmap)` | `engine_imguiSetBitmapShapeRgba(...)` | `imguiApi->setBitmapShapeRgba(...)` |
| `imgui.setShapeThickness(...)` | `engine_imguiSetShapeThickness(...)` | `imguiApi->setShapeThickness(...)` |
| `imgui.removeShape(...)` | `engine_imguiRemoveShape(...)` | `imguiApi->removeShape(...)` |

Java `Bitmap` 只在 Lua-Java 边界转换；固定 C ABI 始终接收路径或紧凑 RGBA 点阵。

## 回调与辅助方法

| Lua | C ABI 路线 | 说明 |
|---|---|---|
| `imgui.setOnClick(...)` | `engine_imguiWaitEvent(...)` | Lua 保存函数对象，消费 `CLICK` 事件。 |
| `imgui.setOnCheck(...)` | `engine_imguiWaitEvent(...)` | 消费 `CHECK` 事件。 |
| `imgui.setOnSelectEvent(...)` | `engine_imguiWaitEvent(...)` | 消费 `SELECT` 事件。 |
| `imgui.setOnSelectEventEx(...)` | `engine_imguiWaitEvent(...)` | 消费 `TABLE_SELECT` 事件。 |
| `imgui.setOnSliderEvent(...)` | `engine_imguiWaitEvent(...)` | 消费 `SLIDER` 事件。 |
| `imgui.setOnClose(...)` | `waitEvent` + `engine_imguiResolveWindowClose(...)` | 回调结果决定是否关闭。 |
| `imgui.post(callback)` | `engine_imguiPost(id)` + `waitEvent` | C ABI 只保存语言无关的回调标识。 |
| `imgui.isValidHandle(handle)` | `engine_imguiIsValidHandle(handle)` | 对应 `imguiApi->isValidHandle`。 |
| `imgui.getLastError()` | `engine_imguiLastError()` | 对应 `imguiApi->lastError`。 |

Lua、JS、Go 的函数对象都不能进入稳定 C ABI，因此没有 `engine_imguiSetOnClick` 之类的函数。
可复用的控件状态、事件队列、事件等待和关闭决策仍全部位于 `libengine.so`；各语言绑定只保存
自己的函数引用并分发统一事件。
