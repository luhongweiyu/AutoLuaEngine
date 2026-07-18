-- 文件用途：在真实 Android 设备上覆盖 ImGui 全部 Lua 方法、回调和生命周期的回归验收。

import("android.graphics.Bitmap")
import("android.graphics.Color")

local function expectHandle(value, name)
    assert(type(value) == "number" and value > 0, name .. ": " .. imgui.getLastError())
    return value
end

local function expectNoError(name)
    local message = imgui.getLastError()
    assert(message == "", name .. ": " .. tostring(message))
end

assert(imgui.isSupport(), imgui.getLastError())
assert(imgui.setColorTheme(imgui.Theme.Dark), imgui.getLastError())
assert(m.capture("/sdcard/xiaoyv_imgui_smoke_source.png"))

local window = expectHandle(
    imgui.createWindow("完整 API 验收", 20, 40, 620, 760, true),
    "createWindow"
)
local vertical = expectHandle(
    imgui.createVerticalLayout(window, -1, -1),
    "createVerticalLayout"
)
imgui.setLayoutBorderVisible(vertical, true)
expectNoError("setLayoutBorderVisible")

local horizontal = expectHandle(
    imgui.createHorticalLayout(vertical, -1, 70),
    "createHorticalLayout"
)
local tree = expectHandle(
    imgui.createTreeBoxLayout(vertical, "树形布局", -1),
    "createTreeBoxLayout"
)
expectHandle(imgui.createLabel(tree, "树形内容", true), "tree label")
local tabs = expectHandle(imgui.createTabBar(vertical, "标签栏"), "createTabBar")
local tab = expectHandle(imgui.addTabBarItem(tabs, "第一页"), "addTabBarItem")
expectHandle(imgui.createLabel(tab, "标签内容", false), "tab label")
imgui.setWidgetVisible(tab, false)
assert(not imgui.isWidgetVisible(tab))
imgui.setWidgetVisible(tab, true)

local button = expectHandle(imgui.createButton(horizontal, "按钮", 150, 54), "createButton parent")
local absoluteButton = expectHandle(
    imgui.createButton(10, 10, 120, 46, "绝对按钮"),
    "createButton absolute"
)
assert(imgui.sameLine(button, 8), imgui.getLastError())
local label = expectHandle(imgui.createLabel(vertical, "基础控件", true), "createLabel")
local checkbox = expectHandle(imgui.createCheckBox(vertical, "复选框", true), "createCheckBox")
local switch = expectHandle(imgui.createSwitch(vertical, "开关", false, 42), "createSwitch")
local input = expectHandle(
    imgui.createInputText(vertical, "输入", "初始", imgui.InputType.Text, -1, 42),
    "createInputText"
)
local progress = expectHandle(
    imgui.createProgressBar(vertical, 0.2, -1, 30),
    "createProgressBar"
)
local slider = expectHandle(
    imgui.createSlider(vertical, "滑块", 0, 100, 25, -1),
    "createSlider"
)
expectHandle(
    imgui.createColorPicker(vertical, "颜色", 0xFF336699, -1, 180),
    "createColorPicker"
)

-- 组合框字符串同时验证普通反斜杠保留，以及 \| 转义竖线。
local combo = expectHandle(
    imgui.createComboBox(vertical, [[普通|C:\temp|竖线\|内容]], -1),
    "createComboBox"
)
assert(imgui.getItemCount(combo) == 3)
assert(imgui.getItemText(combo, 1) == [[C:\temp]])
assert(imgui.getItemText(combo, 2) == "竖线|内容")
imgui.addOptionItem(combo, "追加")
assert(imgui.getItemCount(combo) == 4)
imgui.removeItemAt(combo, 3)
assert(imgui.getItemCount(combo) == 3)

local disposableCombo = expectHandle(
    imgui.createComboBox(vertical, {"甲", "乙"}, 180),
    "createComboBox table"
)
imgui.removeAllItems(disposableCombo)
assert(imgui.getItemCount(disposableCombo) == 0)
imgui.addOptionItem(disposableCombo, "重新追加")
assert(imgui.getSelectedItemIndex(disposableCombo) == -1)

local radio = expectHandle(imgui.createRadioGroup(vertical, "单选组"), "createRadioGroup")
assert(imgui.addRadioBox(radio, "甲", false))
assert(imgui.addRadioBox(radio, "乙", true))
assert(imgui.addRadioBox(radio, "丙", false))
assert(imgui.getItemCount(radio) == 3)

local tableView = expectHandle(
    imgui.createTableView(vertical, "表格", 2, true, -1, 160),
    "createTableView"
)
imgui.setTableHeaderItem(tableView, 0, "名称")
imgui.setTableHeaderItem(tableView, 1, "状态")
local row0 = imgui.insertTableRow(tableView, -2)
assert(row0 == 0)
imgui.setTableItemText(tableView, row0, 0, "核心")
imgui.setTableItemText(tableView, row0, 1, "正常")
assert(imgui.getTableItemText(tableView, row0, 1) == "正常")
local row1 = imgui.insertTableRow(tableView, row0)
assert(row1 == 1)
imgui.deleteTableRow(tableView, row1)
assert(imgui.getItemCount(tableView) == 1)

local emptyTable = expectHandle(
    imgui.createTableView(vertical, "临时表", 1, false, 160, 80),
    "temporary table"
)
imgui.insertTableRow(emptyTable, -2)
imgui.clearTable(emptyTable)
assert(imgui.getItemCount(emptyTable) == 0)

imgui.setInputText(input, "更新文本")
imgui.setInputType(input, imgui.InputType.Password)
assert(imgui.getInputText(input) == "")
imgui.setInputText(input, "更新文本")
imgui.setProgressBarPos(progress, 0.65)
assert(math.abs(imgui.getProgressBarPos(progress) - 0.65) < 0.001)
imgui.setWidgetSize(label, -1, 0)
imgui.setWidgetVisible(label, false)
assert(imgui.isWidgetVisible(label) == false)
imgui.setWidgetVisible(label, true)
assert(imgui.isWidgetVisible(label) == true)
imgui.setWidgetStyle(button, imgui.StyleVar.FrameRounding, 7)
imgui.setWidgetStyle(button, imgui.StyleVar.FramePadding, 8, 4)
imgui.setWidgetStyle(tableView, imgui.StyleVar.TableAngledHeadersTextAlign, 0.5, 0.5)
imgui.setWidgetColor(button, imgui.Color.Button, 0xFF16897C)

local bitmap = Bitmap.createBitmap(48, 48, Bitmap.Config.ARGB_8888)
bitmap.eraseColor(Color.rgb(35, 120, 210))
local image = expectHandle(imgui.createImage(vertical, nil, 48, 48), "createImage")
imgui.setImage(image, "/sdcard/xiaoyv_imgui_smoke_source.png")
imgui.setImageFromBitmap(image, bitmap)
expectNoError("setImageFromBitmap")

imgui.setWindowPos(window, 24, 48)
imgui.setWindowSize(window, 610, 750)
local geometry = imgui.getWindowPos(window)
assert(geometry and geometry.x == 24 and geometry.y == 48)
assert(geometry.width == 610 and geometry.height == 750)
imgui.setWindowFlags(window, imgui.WindowFlags.NoSavedSettings)

local rectangle = expectHandle(
    imgui.createRectangle(10, 10, 90, 60, 0xFFFF0000, false, 4),
    "createRectangle"
)
local circle = expectHandle(
    imgui.createCircle(130, 40, 24, 0xFF00FF00, true, 20),
    "createCircle"
)
local polygon = expectHandle(
    imgui.createPolygon({{170, 10}, {220, 60}, {150, 65}}, 0xFF00AAFF, true, false, 3),
    "createPolygon"
)
local line = expectHandle(
    imgui.createLine(10, 75, 220, 75, 0xFFFFFFFF, 2),
    "createLine"
)
local bitmapShape = expectHandle(
    imgui.createBitmapShape(
        240,
        10,
        48,
        48,
        "/sdcard/xiaoyv_imgui_smoke_source.png"
    ),
    "createBitmapShape path"
)
imgui.setBitmapShape(bitmapShape, bitmap)
local shapeText = expectHandle(
    imgui.createShapeText(
        300,
        10,
        220,
        50,
        "图形文字",
        0xFFFFFFFF,
        0xCC000000,
        true,
        1
    ),
    "createShapeText"
)
assert(imgui.setShapePosition(rectangle, 12, 12) == 0)
assert(imgui.setShapeVisibility(circle, false) == 0)
assert(imgui.isShapeVisibility(circle) == false)
assert(imgui.setShapeVisibility(circle, true) == 0)
assert(imgui.setShapeTextString(shapeText, "更新图形文字"))
assert(imgui.setShapeTextColor(shapeText, 0xFFFFFF00))
assert(imgui.setShapeTextBackground(shapeText, 0xAA000000, true))
assert(imgui.setShapeTextFontScale(shapeText, 0.9))
assert(imgui.setShapeThickness(line, 4))
assert(not imgui.setShapeThickness(bitmapShape, 2))

-- 销毁窗口必须同步释放其全部后代回调，避免长脚本反复建窗时保留 Lua 闭包。
local disposableWindow = expectHandle(
    imgui.createWindow("回调清理", 0, 0, 120, 80, false),
    "disposable window"
)
local disposableButton = expectHandle(
    imgui.createButton(disposableWindow, "临时", 80, 40),
    "disposable button"
)
local disposableCallback = function() end
local weakCallback = setmetatable({disposableCallback}, {__mode = "v"})
imgui.setOnClick(disposableButton, disposableCallback)
disposableCallback = nil
imgui.destroyWindow(disposableWindow)
collectgarbage("collect")
assert(weakCallback[1] == nil, "销毁窗口后仍保留子控件回调")

local events = {
    check = false,
    select = false,
    table = false,
    slider = false,
    post = false,
}
imgui.setOnClick(button, function(handle)
    assert(handle == button)
end)
imgui.setOnCheck(checkbox, function(handle, checked)
    events.check = handle == checkbox and checked == false
end)
imgui.setOnSelectEvent(combo, function(handle, index, text)
    events.select = handle == combo and index == 1 and text == [[C:\temp]]
end)
imgui.setOnSelectEventEx(tableView, function(handle, row, column, text)
    events.table = handle == tableView and row == 0 and column == 0 and text == "核心"
end)
imgui.setOnSliderEvent(slider, function(handle, value)
    events.slider = handle == slider and value == 77
end)
imgui.setOnClose(window, function(handle)
    return handle == window
end)

imgui.setChecked(checkbox, false)
assert(imgui.isChecked(checkbox) == false)
imgui.setChecked(switch, true)
assert(imgui.isChecked(switch) == true)
imgui.setItemSelected(combo, 1)
assert(imgui.getSelectedItemIndex(combo) == 1)
imgui.setItemSelected(radio, 0)
assert(imgui.getSelectedItemIndex(radio) == 0)
imgui.setItemSelected(tableView, 0)
assert(imgui.getSelectedItemIndex(tableView) == 0)
imgui.setSlider(slider, 77)
assert(imgui.getSliderPos(slider) == 77)

assert(imgui.isValidHandle(window))
assert(not imgui.isValidHandle(999999999))
assert(imgui.showWindow({
    x = 20,
    y = 90,
    width = 680,
    height = 960,
    title = "完整 API 验收",
    fontsize = 26,
    contentfontsize = 22,
}), imgui.getLastError())
local windowedGeometry = imgui.getWindowPos(window)
assert(windowedGeometry.x == 0 and windowedGeometry.y == 0)
assert(windowedGeometry.width == 680 and windowedGeometry.height == 960)
assert(imgui.post(function()
    events.post = true
end), imgui.getLastError())

local deadline = tickCount() + 3000
while tickCount() < deadline
        and not (events.check and events.select and events.table and events.slider and events.post) do
    sleep(20)
end
assert(events.check, "复选框回调未执行")
assert(events.select, "组合框回调未执行")
assert(events.table, "表格回调未执行")
assert(events.slider, "滑块回调未执行")
assert(events.post, "post 回调未执行")
assert(m.capture("/sdcard/xiaoyv_imgui_full_smoke.png"))
imgui.close()

imgui.setOnClick(button, nil)
imgui.setOnCheck(checkbox, nil)
imgui.setOnSelectEvent(combo, nil)
imgui.setOnSelectEventEx(tableView, nil)
imgui.setOnSliderEvent(slider, nil)
imgui.setOnClose(window, nil)
assert(imgui.removeShape(rectangle) == 0)
assert(imgui.removeShape(circle) == 0)
assert(imgui.removeShape(polygon) == 0)
assert(imgui.removeShape(line) == 0)
assert(imgui.removeShape(bitmapShape) == 0)
assert(imgui.removeShape(shapeText) == 0)
imgui.destroyWindow(window)
assert(not imgui.isValidHandle(window))

-- 不可触摸模式立即返回；可触摸模式由另一 Lua 用户线程关闭并解除主任务阻塞。
assert(imgui.show(false, 20), imgui.getLastError())
sleep(250)
imgui.close()
local blockingWindow = expectHandle(
    imgui.createWindow("阻塞显示", 60, 120, 420, 240, false),
    "blocking window"
)
expectHandle(imgui.createLabel(blockingWindow, "show(true) 等待关闭", true), "blocking label")
m.thread.beginThread(function()
    sleep(500)
    imgui.close()
end)
assert(imgui.show(true, 22), imgui.getLastError())
imgui.destroyWindow(blockingWindow)
imgui.close()

-- 保持根级绝对按钮引用到脚本末尾，确保绝对坐标控件也经过至少一次真实渲染。
assert(imgui.isValidHandle(absoluteButton))
print("ImGui 85 个 Lua 方法完整验收通过")
