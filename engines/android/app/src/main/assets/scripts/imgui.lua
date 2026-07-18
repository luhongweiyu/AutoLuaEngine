-- 文件用途：完整演示 Dear ImGui 中文、控件、回调、表格、图片和图形绘制能力。

import("android.graphics.Bitmap")
import("android.graphics.Color")

local screenWidth, screenHeight = getDisplaySize()
local windowWidth = math.max(420, math.min(760, screenWidth - 40))
local windowHeight = math.max(620, math.min(1060, screenHeight - 120))

local window = imgui.createWindow(
    "小鱼精灵 ImGui",
    20,
    80,
    windowWidth,
    windowHeight,
    true
)
assert(window and window > 0, imgui.getLastError())

-- 主布局使用滚动子窗口，内容超过窗口高度后仍可正常操作。
local layout = imgui.createVerticalLayout(window, -1, -1)
imgui.setLayoutBorderVisible(layout, true)

imgui.createLabel(layout, "Dear ImGui 1.92.8 / 中文显示正常", true)
local input = imgui.createInputText(
    layout,
    "输入内容",
    "小鱼精灵",
    imgui.InputType.Text,
    -1,
    0
)
local enabled = imgui.createCheckBox(layout, "启用功能", true)
local mode = imgui.createComboBox(layout, "普通模式|快速模式", -1)
imgui.addOptionItem(mode, "调试模式")
imgui.setItemSelected(mode, 0)

local slider = imgui.createSlider(layout, "进度", 0, 100, 35, -1)
local progress = imgui.createProgressBar(layout, 0.35, -1, 34)

local tableView = imgui.createTableView(layout, "运行状态", 2, true, -1, 180)
imgui.setTableHeaderItem(tableView, 0, "项目")
imgui.setTableHeaderItem(tableView, 1, "状态")
local firstRow = imgui.insertTableRow(tableView, -2)
imgui.setTableItemText(tableView, firstRow, 0, "渲染后端")
imgui.setTableItemText(tableView, firstRow, 1, "OpenGL ES 3")
local secondRow = imgui.insertTableRow(tableView, -2)
imgui.setTableItemText(tableView, secondRow, 0, "脚本回调")
imgui.setTableItemText(tableView, secondRow, 1, "等待操作")

-- Java Bitmap 在 Lua-Java 边界转换为 RGBA，再通过统一 C ABI 进入图片控件。
local bitmap = Bitmap.createBitmap(72, 72, Bitmap.Config.ARGB_8888)
bitmap.eraseColor(Color.rgb(23, 132, 111))
local image = imgui.createImage(layout, nil, 72, 72)
imgui.setImageFromBitmap(image, bitmap)

local buttonRow = imgui.createHorticalLayout(layout, -1, 70)
local readButton = imgui.createButton(buttonRow, "读取输入", 210, 58)
local closeButton = imgui.createButton(buttonRow, "关闭", 160, 58)

-- 图形使用屏幕坐标，不属于窗口布局；这里绘制一个不会截获触摸事件的状态标记。
imgui.createRectangle(20, 18, 260, 66, 0xD91A7F72, true, 8)
imgui.createShapeText(
    32,
    25,
    220,
    34,
    "ImGui 示例运行中",
    0xFFFFFFFF,
    0x00000000,
    false,
    0.9
)

imgui.setOnCheck(enabled, function(handle, checked)
    print("复选框状态：", handle, checked)
end)

imgui.setOnSelectEvent(mode, function(handle, index, text)
    print("模式变化：", handle, index, text)
end)

imgui.setOnSliderEvent(slider, function(handle, value)
    imgui.setProgressBarPos(progress, value / 100)
end)

imgui.setOnSelectEventEx(tableView, function(handle, row, column, text)
    print("表格选择：", handle, row, column, text)
end)

imgui.setOnClick(readButton, function()
    local value = imgui.getInputText(input)
    imgui.setTableItemText(tableView, secondRow, 1, value or "")
    -- post 的函数仍由 Lua VM Gate 事件泵执行，可安全修改其他 ImGui 控件。
    imgui.post(function()
        print("输入内容：", value)
    end)
end)

imgui.setOnClick(closeButton, function()
    imgui.close()
end)

imgui.setOnClose(window, function()
    imgui.close()
    return true
end)

imgui.setWidgetStyle(window, imgui.StyleVar.WindowRounding, 8, 0)
imgui.setWidgetColor(readButton, imgui.Color.Button, 0xFF167D72)

local shown = imgui.show(true, 30)
if not shown then
    error("ImGui 显示失败：" .. imgui.getLastError())
end

print("ImGui 示例已关闭")
