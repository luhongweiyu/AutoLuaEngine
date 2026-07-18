/**
 * 文件用途：实现 Dear ImGui 稳定 C ABI，把 Lua、JS、Go 和插件调用统一转发到核心模型。
 */
#include "imgui_c_api.h"

#include <string>
#include <utility>
#include <vector>

#include "api/imgui_api.h"

namespace {

constexpr int kImGuiAbiVersion = 1;
thread_local std::string gError;
thread_local std::string gText;
thread_local std::string gEventText;

struct InterruptContext {
    engine_imgui_interrupt_callback callback;
    void* userData;
};

bool interruptAdapter(void* context) {
    auto* value = static_cast<InterruptContext*>(context);
    return value != nullptr && value->callback != nullptr && value->callback(value->userData) != 0;
}

int result(bool ok) {
    gError = ok ? "" : xiaoyv::api::imguiLastError();
    return ok ? 1 : 0;
}

EngineImGuiHandle handleResult(xiaoyv::api::ImGuiHandle handle) {
    gError = handle > 0 ? "" : xiaoyv::api::imguiLastError();
    return handle > 0 ? handle : 0;
}

xiaoyv::api::ImGuiSurfaceConfig coreConfig(const EngineImGuiSurfaceConfig& value) {
    xiaoyv::api::ImGuiSurfaceConfig config;
    config.windowed = value.windowed != 0;
    config.touchable = value.touchable != 0;
    config.x = value.x;
    config.y = value.y;
    config.width = value.width;
    config.height = value.height;
    config.hasTitle = value.hasTitle != 0;
    config.title = value.title == nullptr ? "" : value.title;
    config.titleColor = value.titleColor;
    config.titleBackgroundColor = value.titleBackgroundColor;
    config.hasClose = value.hasClose != 0;
    config.closeColor = value.closeColor;
    config.hasResize = value.hasResize != 0;
    config.resizeColor = value.resizeColor;
    config.hasToggle = value.hasToggle != 0;
    config.toggleColor = value.toggleColor;
    config.titleFontSize = value.titleFontSize;
    config.fontPath = value.fontPath == nullptr ? "" : value.fontPath;
    config.fontSize = value.fontSize;
    return config;
}

int cEventType(xiaoyv::api::ImGuiEventType type) {
    switch (type) {
        case xiaoyv::api::ImGuiEventType::Click: return ENGINE_IMGUI_EVENT_CLICK;
        case xiaoyv::api::ImGuiEventType::Check: return ENGINE_IMGUI_EVENT_CHECK;
        case xiaoyv::api::ImGuiEventType::Select: return ENGINE_IMGUI_EVENT_SELECT;
        case xiaoyv::api::ImGuiEventType::TableSelect: return ENGINE_IMGUI_EVENT_TABLE_SELECT;
        case xiaoyv::api::ImGuiEventType::Slider: return ENGINE_IMGUI_EVENT_SLIDER;
        case xiaoyv::api::ImGuiEventType::WindowClose: return ENGINE_IMGUI_EVENT_WINDOW_CLOSE;
        case xiaoyv::api::ImGuiEventType::Post: return ENGINE_IMGUI_EVENT_POST;
        case xiaoyv::api::ImGuiEventType::FrameworkClosed:
            return ENGINE_IMGUI_EVENT_FRAMEWORK_CLOSED;
        default: return ENGINE_IMGUI_EVENT_NONE;
    }
}

} // namespace

/** 检测当前 Android OpenGL ES 3 渲染能力。 */
extern "C" int engine_imguiIsSupport() {
    return result(xiaoyv::api::imguiIsSupported());
}

/** 创建或更新 Surface；是否阻塞由语言绑定决定。 */
extern "C" int engine_imguiShow(const EngineImGuiSurfaceConfig* config) {
    if (config == nullptr) {
        gError = "ImGui Surface 配置为空";
        return 0;
    }
    return result(xiaoyv::api::imguiShow(coreConfig(*config)));
}

/** 关闭当前 Surface。 */
extern "C" void engine_imguiClose() {
    xiaoyv::api::imguiClose();
    gError.clear();
}

/** 清空当前脚本全部 ImGui 资源。 */
extern "C" void engine_imguiReset() {
    xiaoyv::api::imguiReset();
    gError.clear();
}

/** 设置全局颜色主题。 */
extern "C" int engine_imguiSetColorTheme(int style) {
    return result(xiaoyv::api::imguiSetColorTheme(style));
}

/** 创建窗口。 */
extern "C" EngineImGuiHandle engine_imguiCreateWindow(
        const char* title, float x, float y, float width, float height, int showClose) {
    return handleResult(xiaoyv::api::imguiCreateWindow(
            title == nullptr ? "" : title, x, y, width, height, showClose != 0));
}

/** 销毁窗口和全部子控件。 */
extern "C" int engine_imguiDestroyWindow(EngineImGuiHandle handle) {
    return result(xiaoyv::api::imguiDestroyWindow(handle));
}

/** 创建垂直布局。 */
extern "C" EngineImGuiHandle engine_imguiCreateVerticalLayout(
        EngineImGuiHandle parent, float width, float height) {
    return handleResult(xiaoyv::api::imguiCreateVerticalLayout(parent, width, height));
}

/** 创建水平布局；名称保留懒人精灵的 Hortical 拼写。 */
extern "C" EngineImGuiHandle engine_imguiCreateHorticalLayout(
        EngineImGuiHandle parent, float width, float height) {
    return handleResult(xiaoyv::api::imguiCreateHorizontalLayout(parent, width, height));
}

/** 创建树形布局。 */
extern "C" EngineImGuiHandle engine_imguiCreateTreeBoxLayout(
        EngineImGuiHandle parent, const char* title, float width) {
    return handleResult(xiaoyv::api::imguiCreateTreeLayout(
            parent, title == nullptr ? "" : title, width));
}

/** 创建标签栏。 */
extern "C" EngineImGuiHandle engine_imguiCreateTabBar(
        EngineImGuiHandle parent, const char* title) {
    return handleResult(xiaoyv::api::imguiCreateTabBar(
            parent, title == nullptr ? "" : title));
}

/** 添加标签页，并返回可作为父容器使用的标签页句柄。 */
extern "C" EngineImGuiHandle engine_imguiAddTabBarItem(
        EngineImGuiHandle tabBar, const char* title) {
    return handleResult(xiaoyv::api::imguiAddTabItem(
            tabBar, title == nullptr ? "" : title));
}

/** 设置控件同行。 */
extern "C" int engine_imguiSameLine(EngineImGuiHandle handle, float spacing) {
    return result(xiaoyv::api::imguiSameLine(handle, spacing));
}

/** 设置布局边框可见性。 */
extern "C" int engine_imguiSetLayoutBorderVisible(EngineImGuiHandle handle, int visible) {
    return result(xiaoyv::api::imguiSetLayoutBorderVisible(handle, visible != 0));
}

/** 创建按钮；parent=0 时 x/y 是画布绝对坐标。 */
extern "C" EngineImGuiHandle engine_imguiCreateButton(
        EngineImGuiHandle parent, const char* text, float x, float y, float width, float height) {
    return handleResult(xiaoyv::api::imguiCreateButton(
            parent, text == nullptr ? "" : text, x, y, width, height));
}

/** 创建文本标签。 */
extern "C" EngineImGuiHandle engine_imguiCreateLabel(
        EngineImGuiHandle parent, const char* text, int singleLine) {
    return handleResult(xiaoyv::api::imguiCreateLabel(
            parent, text == nullptr ? "" : text, singleLine != 0));
}

/** 创建复选框。 */
extern "C" EngineImGuiHandle engine_imguiCreateCheckBox(
        EngineImGuiHandle parent, const char* label, int checked) {
    return handleResult(xiaoyv::api::imguiCreateCheckBox(
            parent, label == nullptr ? "" : label, checked != 0));
}

/** 创建开关。 */
extern "C" EngineImGuiHandle engine_imguiCreateSwitch(
        EngineImGuiHandle parent, const char* label, int checked, float height) {
    return handleResult(xiaoyv::api::imguiCreateSwitch(
            parent, label == nullptr ? "" : label, checked != 0, height));
}

/** 创建输入框。 */
extern "C" EngineImGuiHandle engine_imguiCreateInputText(
        EngineImGuiHandle parent, const char* label, const char* value,
        int inputType, float width, float height) {
    return handleResult(xiaoyv::api::imguiCreateInputText(
            parent, label == nullptr ? "" : label, value == nullptr ? "" : value,
            inputType, width, height));
}

/** 创建进度条。 */
extern "C" EngineImGuiHandle engine_imguiCreateProgressBar(
        EngineImGuiHandle parent, float progress, float width, float height) {
    return handleResult(xiaoyv::api::imguiCreateProgressBar(parent, progress, width, height));
}

/** 创建整数滑动条。 */
extern "C" EngineImGuiHandle engine_imguiCreateSlider(
        EngineImGuiHandle parent, const char* label, int minimum, int maximum,
        int initialPosition, float width) {
    return handleResult(xiaoyv::api::imguiCreateSlider(
            parent, label == nullptr ? "" : label, minimum, maximum, initialPosition, width));
}

/** 创建 0xAARRGGBB 颜色选择器。 */
extern "C" EngineImGuiHandle engine_imguiCreateColorPicker(
        EngineImGuiHandle parent, const char* title, uint32_t color, float width, float height) {
    return handleResult(xiaoyv::api::imguiCreateColorPicker(
            parent, title == nullptr ? "" : title, color, width, height));
}

/** 创建组合框，选项数组只在当前调用期间读取。 */
extern "C" EngineImGuiHandle engine_imguiCreateComboBox(
        EngineImGuiHandle parent, const char* const* items, int itemCount, float width) {
    if (itemCount < 0 || (itemCount > 0 && items == nullptr)) {
        gError = "组合框选项数组无效";
        return 0;
    }
    std::vector<std::string> values;
    values.reserve(static_cast<std::size_t>(itemCount));
    for (int index = 0; index < itemCount; ++index) {
        values.emplace_back(items[index] == nullptr ? "" : items[index]);
    }
    return handleResult(xiaoyv::api::imguiCreateComboBox(parent, values, width));
}

/** 创建单选组。 */
extern "C" EngineImGuiHandle engine_imguiCreateRadioGroup(
        EngineImGuiHandle parent, const char* label) {
    return handleResult(xiaoyv::api::imguiCreateRadioGroup(
            parent, label == nullptr ? "" : label));
}

/** 添加组合框选项。 */
extern "C" int engine_imguiAddOptionItem(EngineImGuiHandle handle, const char* text) {
    return result(xiaoyv::api::imguiAddOptionItem(handle, text == nullptr ? "" : text));
}

/** 添加单选项。 */
extern "C" int engine_imguiAddRadioBox(
        EngineImGuiHandle handle, const char* text, int wrapLine) {
    return result(xiaoyv::api::imguiAddRadioBox(
            handle, text == nullptr ? "" : text, wrapLine != 0));
}

/** 返回组合框或单选组的选项文本。 */
extern "C" const char* engine_imguiGetItemText(EngineImGuiHandle handle, int index) {
    if (!xiaoyv::api::imguiGetItemText(handle, index, &gText)) {
        gError = xiaoyv::api::imguiLastError();
        return nullptr;
    }
    gError.clear();
    return gText.c_str();
}

/** 删除指定选项。 */
extern "C" int engine_imguiRemoveItemAt(EngineImGuiHandle handle, int index) {
    return result(xiaoyv::api::imguiRemoveItemAt(handle, index));
}

/** 清空全部选项。 */
extern "C" int engine_imguiRemoveAllItems(EngineImGuiHandle handle) {
    return result(xiaoyv::api::imguiRemoveAllItems(handle));
}

/** 返回组合框、单选组或表格当前选择索引。 */
extern "C" int engine_imguiGetSelectedItemIndex(EngineImGuiHandle handle) {
    int value = xiaoyv::api::imguiGetSelectedItemIndex(handle);
    gError = value == -2 ? xiaoyv::api::imguiLastError() : "";
    return value;
}

/** 设置组合框、单选组或表格当前选择索引。 */
extern "C" int engine_imguiSetItemSelected(EngineImGuiHandle handle, int index) {
    return result(xiaoyv::api::imguiSetItemSelected(handle, index));
}

/** 返回选项数或表格行数。 */
extern "C" int engine_imguiGetItemCount(EngineImGuiHandle handle) {
    int value = xiaoyv::api::imguiGetItemCount(handle);
    gError = value < 0 ? xiaoyv::api::imguiLastError() : "";
    return value;
}

/** 创建表格控件。 */
extern "C" EngineImGuiHandle engine_imguiCreateTableView(
        EngineImGuiHandle parent,
        const char* title,
        int columns,
        int showHeader,
        float width,
        float height
) {
    return handleResult(xiaoyv::api::imguiCreateTable(
            parent,
            title == nullptr ? "" : title,
            columns,
            showHeader != 0,
            width,
            height
    ));
}

/** 设置表头文本。 */
extern "C" int engine_imguiSetTableHeaderItem(
        EngineImGuiHandle handle,
        int column,
        const char* text
) {
    return result(xiaoyv::api::imguiSetTableHeader(
            handle,
            column,
            text == nullptr ? "" : text
    ));
}

/** 插入一行并返回实际行索引，失败返回 -1。 */
extern "C" int engine_imguiInsertTableRow(EngineImGuiHandle handle, int after) {
    const int row = xiaoyv::api::imguiInsertTableRow(handle, after);
    gError = row < 0 ? xiaoyv::api::imguiLastError() : "";
    return row;
}

/** 返回指定单元格文本。 */
extern "C" const char* engine_imguiGetTableItemText(
        EngineImGuiHandle handle,
        int row,
        int column
) {
    if (!xiaoyv::api::imguiGetTableItemText(handle, row, column, &gText)) {
        gError = xiaoyv::api::imguiLastError();
        return nullptr;
    }
    gError.clear();
    return gText.c_str();
}

/** 设置指定单元格文本。 */
extern "C" int engine_imguiSetTableItemText(
        EngineImGuiHandle handle,
        int row,
        int column,
        const char* text
) {
    return result(xiaoyv::api::imguiSetTableItemText(
            handle,
            row,
            column,
            text == nullptr ? "" : text
    ));
}

/** 删除表格行。 */
extern "C" int engine_imguiDeleteTableRow(EngineImGuiHandle handle, int row) {
    return result(xiaoyv::api::imguiDeleteTableRow(handle, row));
}

/** 清空表格行，保留表头与列定义。 */
extern "C" int engine_imguiClearTable(EngineImGuiHandle handle) {
    return result(xiaoyv::api::imguiClearTable(handle));
}

/** 设置复选框或开关状态。 */
extern "C" int engine_imguiSetChecked(EngineImGuiHandle handle, int checked) {
    return result(xiaoyv::api::imguiSetChecked(handle, checked != 0));
}

/** 读取复选框或开关状态。 */
extern "C" int engine_imguiIsChecked(EngineImGuiHandle handle, int* checked) {
    if (checked == nullptr) {
        gError = "复选状态输出地址为空";
        return 0;
    }
    bool value = false;
    if (!xiaoyv::api::imguiIsChecked(handle, &value)) {
        gError = xiaoyv::api::imguiLastError();
        return 0;
    }
    *checked = value ? 1 : 0;
    gError.clear();
    return 1;
}

/** 返回输入框当前 UTF-8 文本。 */
extern "C" const char* engine_imguiGetInputText(EngineImGuiHandle handle) {
    if (!xiaoyv::api::imguiGetInputText(handle, &gText)) {
        gError = xiaoyv::api::imguiLastError();
        return nullptr;
    }
    gError.clear();
    return gText.c_str();
}

/** 替换输入框文本。 */
extern "C" int engine_imguiSetInputText(EngineImGuiHandle handle, const char* text) {
    return result(xiaoyv::api::imguiSetInputText(handle, text == nullptr ? "" : text));
}

/** 设置输入框类型。 */
extern "C" int engine_imguiSetInputType(EngineImGuiHandle handle, int inputType) {
    return result(xiaoyv::api::imguiSetInputType(handle, inputType));
}

/** 设置进度条位置。 */
extern "C" int engine_imguiSetProgressBarPos(EngineImGuiHandle handle, float progress) {
    return result(xiaoyv::api::imguiSetProgress(handle, progress));
}

/** 读取进度条位置。 */
extern "C" int engine_imguiGetProgressBarPos(EngineImGuiHandle handle, float* progress) {
    return result(xiaoyv::api::imguiGetProgress(handle, progress));
}

/** 设置滑动条整数位置。 */
extern "C" int engine_imguiSetSlider(EngineImGuiHandle handle, int position) {
    return result(xiaoyv::api::imguiSetSlider(handle, position));
}

/** 读取滑动条整数位置。 */
extern "C" int engine_imguiGetSliderPos(EngineImGuiHandle handle, int* position) {
    return result(xiaoyv::api::imguiGetSlider(handle, position));
}

/** 设置任意控件尺寸。 */
extern "C" int engine_imguiSetWidgetSize(
        EngineImGuiHandle handle,
        float width,
        float height
) {
    return result(xiaoyv::api::imguiSetWidgetSize(handle, width, height));
}

/** 设置控件可见性。 */
extern "C" int engine_imguiSetWidgetVisible(EngineImGuiHandle handle, int visible) {
    return result(xiaoyv::api::imguiSetWidgetVisible(handle, visible != 0));
}

/** 读取控件可见性。 */
extern "C" int engine_imguiIsWidgetVisible(EngineImGuiHandle handle, int* visible) {
    if (visible == nullptr) {
        gError = "控件可见状态输出地址为空";
        return 0;
    }
    bool value = false;
    if (!xiaoyv::api::imguiIsWidgetVisible(handle, &value)) {
        gError = xiaoyv::api::imguiLastError();
        return 0;
    }
    *visible = value ? 1 : 0;
    gError.clear();
    return 1;
}

/** 设置控件的 Dear ImGui 样式浮点值。 */
extern "C" int engine_imguiSetWidgetStyle(
        EngineImGuiHandle handle,
        int style,
        float first,
        float second
) {
    return result(xiaoyv::api::imguiSetWidgetStyle(handle, style, first, second));
}

/** 设置控件的 0xAARRGGBB 样式颜色。 */
extern "C" int engine_imguiSetWidgetColor(
        EngineImGuiHandle handle,
        int type,
        uint32_t color
) {
    return result(xiaoyv::api::imguiSetWidgetColor(handle, type, color));
}

/** 从图片路径创建图片控件。 */
extern "C" EngineImGuiHandle engine_imguiCreateImage(
        EngineImGuiHandle parent,
        const char* path,
        float width,
        float height
) {
    return handleResult(xiaoyv::api::imguiCreateImage(
            parent,
            path == nullptr ? "" : path,
            width,
            height
    ));
}

/** 从图片路径替换图片控件内容。 */
extern "C" int engine_imguiSetImage(EngineImGuiHandle handle, const char* path) {
    return result(xiaoyv::api::imguiSetImage(handle, path == nullptr ? "" : path));
}

/** 从调用方 RGBA8888 点阵复制并替换图片控件内容。 */
extern "C" int engine_imguiSetImageRgba(
        EngineImGuiHandle handle,
        const unsigned char* rgba,
        int width,
        int height
) {
    return result(xiaoyv::api::imguiSetImageRgba(handle, rgba, width, height));
}

/** 设置普通 ImGui 窗口左上角坐标。 */
extern "C" int engine_imguiSetWindowPos(EngineImGuiHandle handle, float x, float y) {
    return result(xiaoyv::api::imguiSetWindowPos(handle, x, y));
}

/** 设置普通 ImGui 窗口尺寸。 */
extern "C" int engine_imguiSetWindowSize(
        EngineImGuiHandle handle,
        float width,
        float height
) {
    return result(xiaoyv::api::imguiSetWindowSize(handle, width, height));
}

/** 读取普通 ImGui 窗口坐标与尺寸。 */
extern "C" int engine_imguiGetWindowPos(
        EngineImGuiHandle handle,
        EngineImGuiGeometry* geometry
) {
    if (geometry == nullptr) {
        gError = "窗口几何输出地址为空";
        return 0;
    }
    return result(xiaoyv::api::imguiGetWindowGeometry(
            handle,
            &geometry->x,
            &geometry->y,
            &geometry->width,
            &geometry->height
    ));
}

/** 设置普通 ImGui 窗口标志位。 */
extern "C" int engine_imguiSetWindowFlags(EngineImGuiHandle handle, int flags) {
    return result(xiaoyv::api::imguiSetWindowFlags(handle, flags));
}

/** 创建矩形图形。 */
extern "C" EngineImGuiHandle engine_imguiCreateRectangle(
        float x,
        float y,
        float x2,
        float y2,
        uint32_t color,
        int filled,
        float rounding
) {
    return handleResult(xiaoyv::api::imguiCreateRectangle(
            x, y, x2, y2, color, filled != 0, rounding
    ));
}

/** 创建圆形图形。 */
extern "C" EngineImGuiHandle engine_imguiCreateCircle(
        float x,
        float y,
        float radius,
        uint32_t color,
        int filled,
        int segments
) {
    return handleResult(xiaoyv::api::imguiCreateCircle(
            x, y, radius, color, filled != 0, segments
    ));
}

/** 创建多边形图形；顶点数组只在当前调用期间读取。 */
extern "C" EngineImGuiHandle engine_imguiCreatePolygon(
        const EngineImGuiPointF* points,
        int pointCount,
        uint32_t color,
        int closed,
        int filled,
        float thickness
) {
    if (pointCount < 0 || (pointCount > 0 && points == nullptr)) {
        gError = "多边形顶点数组无效";
        return 0;
    }
    std::vector<std::pair<float, float>> values;
    values.reserve(static_cast<std::size_t>(pointCount));
    for (int index = 0; index < pointCount; ++index) {
        values.emplace_back(points[index].x, points[index].y);
    }
    return handleResult(xiaoyv::api::imguiCreatePolygon(
            values, color, closed != 0, filled != 0, thickness
    ));
}

/** 创建线段图形。 */
extern "C" EngineImGuiHandle engine_imguiCreateLine(
        float x1,
        float y1,
        float x2,
        float y2,
        uint32_t color,
        float thickness
) {
    return handleResult(xiaoyv::api::imguiCreateLine(
            x1, y1, x2, y2, color, thickness
    ));
}

/** 从图片路径创建位图图形。 */
extern "C" EngineImGuiHandle engine_imguiCreateBitmapShape(
        float x,
        float y,
        float width,
        float height,
        const char* path
) {
    return handleResult(xiaoyv::api::imguiCreateBitmapShape(
            x, y, width, height, path == nullptr ? "" : path
    ));
}

/** 从调用方 RGBA8888 点阵创建位图图形。 */
extern "C" EngineImGuiHandle engine_imguiCreateBitmapShapeRgba(
        float x,
        float y,
        float width,
        float height,
        const unsigned char* rgba,
        int imageWidth,
        int imageHeight
) {
    return handleResult(xiaoyv::api::imguiCreateBitmapShapeRgba(
            x, y, width, height, rgba, imageWidth, imageHeight
    ));
}

/** 创建带可选背景的 UTF-8 文本图形。 */
extern "C" EngineImGuiHandle engine_imguiCreateShapeText(
        float x,
        float y,
        float width,
        float height,
        const char* text,
        uint32_t textColor,
        uint32_t backgroundColor,
        int hasBackground,
        float fontScale
) {
    return handleResult(xiaoyv::api::imguiCreateShapeText(
            x,
            y,
            width,
            height,
            text == nullptr ? "" : text,
            textColor,
            backgroundColor,
            hasBackground != 0,
            fontScale
    ));
}

/** 移动图形；保持兼容返回值：成功 0，失败 -1。 */
extern "C" int engine_imguiSetShapePosition(EngineImGuiHandle handle, float x, float y) {
    const int value = xiaoyv::api::imguiSetShapePosition(handle, x, y);
    gError = value == 0 ? "" : xiaoyv::api::imguiLastError();
    return value;
}

/** 设置图形可见性；保持兼容返回值：成功 0，失败 -1。 */
extern "C" int engine_imguiSetShapeVisibility(EngineImGuiHandle handle, int visible) {
    const int value = xiaoyv::api::imguiSetShapeVisibility(handle, visible != 0);
    gError = value == 0 ? "" : xiaoyv::api::imguiLastError();
    return value;
}

/** 读取图形可见性。 */
extern "C" int engine_imguiIsShapeVisibility(EngineImGuiHandle handle, int* visible) {
    if (visible == nullptr) {
        gError = "图形可见状态输出地址为空";
        return 0;
    }
    bool value = false;
    if (!xiaoyv::api::imguiIsShapeVisible(handle, &value)) {
        gError = xiaoyv::api::imguiLastError();
        return 0;
    }
    *visible = value ? 1 : 0;
    gError.clear();
    return 1;
}

/** 替换文本图形字符串。 */
extern "C" int engine_imguiSetShapeTextString(
        EngineImGuiHandle handle,
        const char* text
) {
    return result(xiaoyv::api::imguiSetShapeText(handle, text == nullptr ? "" : text));
}

/** 设置文本图形文字颜色。 */
extern "C" int engine_imguiSetShapeTextColor(EngineImGuiHandle handle, uint32_t color) {
    return result(xiaoyv::api::imguiSetShapeTextColor(handle, color));
}

/** 设置文本图形背景颜色及是否显示背景。 */
extern "C" int engine_imguiSetShapeTextBackground(
        EngineImGuiHandle handle,
        uint32_t color,
        int hasBackground
) {
    return result(xiaoyv::api::imguiSetShapeTextBackground(
            handle, color, hasBackground != 0
    ));
}

/** 设置文本图形字号缩放。 */
extern "C" int engine_imguiSetShapeTextFontScale(
        EngineImGuiHandle handle,
        float scale
) {
    return result(xiaoyv::api::imguiSetShapeTextFontScale(handle, scale));
}

/** 从图片路径替换位图图形内容。 */
extern "C" int engine_imguiSetBitmapShape(EngineImGuiHandle handle, const char* path) {
    return result(xiaoyv::api::imguiSetBitmapShape(
            handle, path == nullptr ? "" : path
    ));
}

/** 从调用方 RGBA8888 点阵替换位图图形内容。 */
extern "C" int engine_imguiSetBitmapShapeRgba(
        EngineImGuiHandle handle,
        const unsigned char* rgba,
        int width,
        int height
) {
    return result(xiaoyv::api::imguiSetBitmapShapeRgba(handle, rgba, width, height));
}

/** 设置线框类图形的粗细。 */
extern "C" int engine_imguiSetShapeThickness(EngineImGuiHandle handle, float thickness) {
    return result(xiaoyv::api::imguiSetShapeThickness(handle, thickness));
}

/** 删除图形；保持兼容返回值：成功 0，失败 -1。 */
extern "C" int engine_imguiRemoveShape(EngineImGuiHandle handle) {
    const int value = xiaoyv::api::imguiRemoveShape(handle);
    gError = value == 0 ? "" : xiaoyv::api::imguiLastError();
    return value;
}

/** 检查窗口、控件或图形句柄是否仍然有效。 */
extern "C" int engine_imguiIsValidHandle(EngineImGuiHandle handle) {
    const bool valid = xiaoyv::api::imguiIsValidHandle(handle);
    gError.clear();
    return valid ? 1 : 0;
}

/** 向统一事件队列投递语言运行时自己的回调标识。 */
extern "C" int engine_imguiPost(long long postId) {
    return result(xiaoyv::api::imguiPost(postId));
}

/** 处理窗口关闭回调结果。 */
extern "C" int engine_imguiResolveWindowClose(
        EngineImGuiHandle handle,
        int allowClose
) {
    return result(xiaoyv::api::imguiResolveWindowClose(handle, allowClose != 0));
}

/**
 * 等待下一条结构化事件。
 *
 * 文本复制到当前线程存储，返回指针在下一次本线程 waitEvent 前保持有效；等待期间核心
 * 不访问任何语言虚拟机，停止检测由调用方提供的轻量回调完成。
 */
extern "C" int engine_imguiWaitEvent(
        EngineImGuiEvent* output,
        int timeoutMs,
        engine_imgui_interrupt_callback interrupt,
        void* userData
) {
    if (output == nullptr) {
        gError = "ImGui 事件输出地址为空";
        return 0;
    }

    InterruptContext context{interrupt, userData};
    xiaoyv::api::ImGuiEvent event;
    const bool received = xiaoyv::api::imguiWaitEvent(
            timeoutMs,
            interrupt == nullptr ? nullptr : interruptAdapter,
            interrupt == nullptr ? nullptr : &context,
            &event
    );
    if (!received) {
        gError = xiaoyv::api::imguiLastError();
        return 0;
    }

    gEventText = std::move(event.text);
    output->type = cEventType(event.type);
    output->handle = event.handle;
    output->index = event.index;
    output->row = event.row;
    output->column = event.column;
    output->integerValue = event.integerValue;
    output->boolValue = event.boolValue ? 1 : 0;
    output->text = gEventText.c_str();
    gError.clear();
    return 1;
}

/** 阻塞等待整个 ImGui 框架关闭。 */
extern "C" int engine_imguiWaitClosed(
        engine_imgui_interrupt_callback interrupt,
        void* userData
) {
    InterruptContext context{interrupt, userData};
    return result(xiaoyv::api::imguiWaitClosed(
            interrupt == nullptr ? nullptr : interruptAdapter,
            interrupt == nullptr ? nullptr : &context
    ));
}

/** 返回当前线程最近一次 C ABI 错误。 */
extern "C" const char* engine_imguiLastError() {
    return gError.c_str();
}

namespace {

/**
 * 按字段名初始化子函数表，避免同签名函数较多时因位置初始化写错映射。
 *
 * 结构体只允许在尾部追加字段；abiVersion 变化后，旧调用方仍可按自己已知的前缀读取。
 */
EngineImGuiApi createImGuiApi() {
    EngineImGuiApi api{};
    api.abiVersion = kImGuiAbiVersion;
    api.isSupport = engine_imguiIsSupport;
    api.show = engine_imguiShow;
    api.close = engine_imguiClose;
    api.reset = engine_imguiReset;
    api.setColorTheme = engine_imguiSetColorTheme;
    api.createWindow = engine_imguiCreateWindow;
    api.destroyWindow = engine_imguiDestroyWindow;
    api.createVerticalLayout = engine_imguiCreateVerticalLayout;
    api.createHorticalLayout = engine_imguiCreateHorticalLayout;
    api.createTreeBoxLayout = engine_imguiCreateTreeBoxLayout;
    api.createTabBar = engine_imguiCreateTabBar;
    api.addTabBarItem = engine_imguiAddTabBarItem;
    api.sameLine = engine_imguiSameLine;
    api.setLayoutBorderVisible = engine_imguiSetLayoutBorderVisible;
    api.createButton = engine_imguiCreateButton;
    api.createLabel = engine_imguiCreateLabel;
    api.createCheckBox = engine_imguiCreateCheckBox;
    api.createSwitch = engine_imguiCreateSwitch;
    api.createInputText = engine_imguiCreateInputText;
    api.createProgressBar = engine_imguiCreateProgressBar;
    api.createSlider = engine_imguiCreateSlider;
    api.createColorPicker = engine_imguiCreateColorPicker;
    api.createComboBox = engine_imguiCreateComboBox;
    api.createRadioGroup = engine_imguiCreateRadioGroup;
    api.addOptionItem = engine_imguiAddOptionItem;
    api.addRadioBox = engine_imguiAddRadioBox;
    api.getItemText = engine_imguiGetItemText;
    api.removeItemAt = engine_imguiRemoveItemAt;
    api.removeAllItems = engine_imguiRemoveAllItems;
    api.getSelectedItemIndex = engine_imguiGetSelectedItemIndex;
    api.setItemSelected = engine_imguiSetItemSelected;
    api.getItemCount = engine_imguiGetItemCount;
    api.createTableView = engine_imguiCreateTableView;
    api.setTableHeaderItem = engine_imguiSetTableHeaderItem;
    api.insertTableRow = engine_imguiInsertTableRow;
    api.getTableItemText = engine_imguiGetTableItemText;
    api.setTableItemText = engine_imguiSetTableItemText;
    api.deleteTableRow = engine_imguiDeleteTableRow;
    api.clearTable = engine_imguiClearTable;
    api.setChecked = engine_imguiSetChecked;
    api.isChecked = engine_imguiIsChecked;
    api.getInputText = engine_imguiGetInputText;
    api.setInputText = engine_imguiSetInputText;
    api.setInputType = engine_imguiSetInputType;
    api.setProgressBarPos = engine_imguiSetProgressBarPos;
    api.getProgressBarPos = engine_imguiGetProgressBarPos;
    api.setSlider = engine_imguiSetSlider;
    api.getSliderPos = engine_imguiGetSliderPos;
    api.setWidgetSize = engine_imguiSetWidgetSize;
    api.setWidgetVisible = engine_imguiSetWidgetVisible;
    api.isWidgetVisible = engine_imguiIsWidgetVisible;
    api.setWidgetStyle = engine_imguiSetWidgetStyle;
    api.setWidgetColor = engine_imguiSetWidgetColor;
    api.createImage = engine_imguiCreateImage;
    api.setImage = engine_imguiSetImage;
    api.setImageRgba = engine_imguiSetImageRgba;
    api.setWindowPos = engine_imguiSetWindowPos;
    api.setWindowSize = engine_imguiSetWindowSize;
    api.getWindowPos = engine_imguiGetWindowPos;
    api.setWindowFlags = engine_imguiSetWindowFlags;
    api.createRectangle = engine_imguiCreateRectangle;
    api.createCircle = engine_imguiCreateCircle;
    api.createPolygon = engine_imguiCreatePolygon;
    api.createLine = engine_imguiCreateLine;
    api.createBitmapShape = engine_imguiCreateBitmapShape;
    api.createBitmapShapeRgba = engine_imguiCreateBitmapShapeRgba;
    api.createShapeText = engine_imguiCreateShapeText;
    api.setShapePosition = engine_imguiSetShapePosition;
    api.setShapeVisibility = engine_imguiSetShapeVisibility;
    api.isShapeVisibility = engine_imguiIsShapeVisibility;
    api.setShapeTextString = engine_imguiSetShapeTextString;
    api.setShapeTextColor = engine_imguiSetShapeTextColor;
    api.setShapeTextBackground = engine_imguiSetShapeTextBackground;
    api.setShapeTextFontScale = engine_imguiSetShapeTextFontScale;
    api.setBitmapShape = engine_imguiSetBitmapShape;
    api.setBitmapShapeRgba = engine_imguiSetBitmapShapeRgba;
    api.setShapeThickness = engine_imguiSetShapeThickness;
    api.removeShape = engine_imguiRemoveShape;
    api.isValidHandle = engine_imguiIsValidHandle;
    api.post = engine_imguiPost;
    api.resolveWindowClose = engine_imguiResolveWindowClose;
    api.waitEvent = engine_imguiWaitEvent;
    api.waitClosed = engine_imguiWaitClosed;
    api.lastError = engine_imguiLastError;
    return api;
}

const EngineImGuiApi kImGuiApi = createImGuiApi();

} // namespace

/** 返回进程级只读 ImGui 子函数表。 */
extern "C" const EngineImGuiApi* engine_getImGuiApi() {
    return &kImGuiApi;
}
