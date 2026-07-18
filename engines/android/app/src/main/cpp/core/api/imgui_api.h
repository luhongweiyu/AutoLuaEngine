/**
 * 文件用途：声明语言无关的 ImGui 控件模型、事件队列和渲染快照接口。
 *
 * Lua、后续 JS/Go 以及插件只能通过稳定 C ABI 修改这里的模型；EGL 渲染线程只读取
 * 不可变快照并提交交互结果，任何渲染回调都不会直接进入脚本虚拟机。
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "runtime_api.h"

namespace xiaoyv::api {

using ImGuiHandle = long long;

/** ImGui 悬浮 Surface 的完整配置，尺寸和坐标均为屏幕物理像素。 */
struct ImGuiSurfaceConfig {
    bool windowed = false;
    bool touchable = true;
    int x = 0;
    int y = 0;
    int width = -1;
    int height = -1;
    bool hasTitle = false;
    std::string title;
    std::uint32_t titleColor = 0xFFFFFFFFU;
    std::uint32_t titleBackgroundColor = 0xFF87CEFAU;
    bool hasClose = true;
    std::uint32_t closeColor = 0xFFFFFFFFU;
    bool hasResize = true;
    std::uint32_t resizeColor = 0xFFFFFFFFU;
    bool hasToggle = true;
    std::uint32_t toggleColor = 0xFFFFFFFFU;
    float titleFontSize = 100.0F;
    std::string fontPath;
    float fontSize = 45.0F;
};

/** 控件种类。该枚举只在 libengine.so 内部使用，不作为稳定 C ABI 数值。 */
enum class ImGuiWidgetKind {
    Window,
    VerticalLayout,
    HorizontalLayout,
    TreeLayout,
    TabBar,
    TabItem,
    Button,
    Label,
    CheckBox,
    Switch,
    InputText,
    ProgressBar,
    Slider,
    ColorPicker,
    ComboBox,
    RadioGroup,
    Table,
    Image
};

/** 绘图图形种类。 */
enum class ImGuiShapeKind {
    Rectangle,
    Circle,
    Polygon,
    Line,
    Bitmap,
    Text
};

/**
 * 已解码 RGBA 图片。
 *
 * shared_ptr 让渲染快照与可变模型共享同一份大块像素；替换图片只创建新对象，正在渲染
 * 的旧快照仍可安全完成当前帧，不会读到已释放内存。
 */
struct ImGuiImagePixels {
    int width = 0;
    int height = 0;
    std::uint64_t revision = 0;
    std::shared_ptr<const std::vector<unsigned char>> rgba;
};

/** 单个 ImGui 控件的不可变渲染数据。 */
struct ImGuiWidgetSnapshot {
    ImGuiHandle handle = 0;
    ImGuiHandle parent = 0;
    ImGuiWidgetKind kind = ImGuiWidgetKind::Label;
    std::vector<ImGuiHandle> children;

    std::string title;
    std::string text;
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    bool visible = true;
    bool sameLine = false;
    float sameLineSpacing = -1.0F;

    bool checked = false;
    bool singleLine = true;
    int inputType = 0;
    float progress = 0.0F;
    int minimum = 0;
    int maximum = 100;
    int integerValue = 0;
    std::uint32_t color = 0xFF000000U;

    std::vector<std::string> items;
    int selectedIndex = -1;
    /**
     * 单选项是否在自身之后换行，与 items 使用相同下标。
     *
     * wrapline 是 addRadioBox 的逐项参数，不能压缩成整个单选组共用的一个布尔值；否则
     * 混合使用横向排列和手动换行时，最后一次 addRadioBox 会错误覆盖前面所有项目。
     */
    std::vector<unsigned char> radioWrapAfter;

    int columns = 0;
    bool showHeader = true;
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    int selectedRow = -1;
    int selectedColumn = -1;

    bool showClose = false;
    bool closePending = false;
    bool borderVisible = false;
    int windowFlags = 0;
    std::unordered_map<int, std::pair<float, float>> styleValues;
    std::unordered_map<int, std::uint32_t> styleColors;
    std::shared_ptr<const ImGuiImagePixels> image;
};

/** 单个图形的不可变渲染数据。 */
struct ImGuiShapeSnapshot {
    ImGuiHandle handle = 0;
    ImGuiShapeKind kind = ImGuiShapeKind::Line;
    bool visible = true;
    float x = 0.0F;
    float y = 0.0F;
    float x2 = 0.0F;
    float y2 = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    float radius = 0.0F;
    std::vector<std::pair<float, float>> points;
    std::uint32_t color = 0xFFFFFFFFU;
    bool filled = false;
    bool closed = false;
    float rounding = 0.0F;
    float thickness = 1.0F;
    int segments = 0;
    std::string text;
    std::uint32_t textColor = 0xFFFFFFFFU;
    std::uint32_t backgroundColor = 0x00000000U;
    bool hasBackground = false;
    float fontScale = 1.0F;
    std::shared_ptr<const ImGuiImagePixels> image;
};

/** 一帧渲染读取的完整不可变模型。 */
struct ImGuiRenderSnapshot {
    std::uint64_t revision = 0;
    /** 每次脚本主动 show/showWindow 时递增，渲染器据此重置上一次窗口的收起状态。 */
    std::uint64_t surfaceGeneration = 0;
    bool displayed = false;
    int colorTheme = 0;
    ImGuiSurfaceConfig surface;
    std::unordered_map<ImGuiHandle, ImGuiWidgetSnapshot> widgets;
    std::vector<ImGuiHandle> windows;
    std::vector<ImGuiHandle> rootWidgets;
    std::vector<ImGuiShapeSnapshot> shapes;
};

/** 渲染线程提交给核心模型的交互类型。 */
enum class ImGuiInteractionType {
    ButtonClick,
    CheckChanged,
    SelectionChanged,
    TableCellSelected,
    SliderChanged,
    InputChanged,
    ColorChanged,
    WindowCloseRequested,
    WindowGeometryChanged,
    SurfaceGeometryChanged,
    SurfaceCloseRequested
};

/** 渲染线程提交的一次用户交互；text 在提交调用返回前有效即可。 */
struct ImGuiInteraction {
    ImGuiInteractionType type = ImGuiInteractionType::ButtonClick;
    ImGuiHandle handle = 0;
    int index = -1;
    int row = -1;
    int column = -1;
    int integerValue = 0;
    bool boolValue = false;
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    std::uint32_t color = 0;
    std::string text;
};

/** 脚本事件类型。数值会由 C ABI 转为稳定枚举，不直接对外暴露本枚举。 */
enum class ImGuiEventType {
    None,
    Click,
    Check,
    Select,
    TableSelect,
    Slider,
    WindowClose,
    Post,
    FrameworkClosed
};

/** Lua/JS/Go 事件泵消费的结构化事件。 */
struct ImGuiEvent {
    ImGuiEventType type = ImGuiEventType::None;
    ImGuiHandle handle = 0;
    int index = -1;
    int row = -1;
    int column = -1;
    int integerValue = 0;
    bool boolValue = false;
    std::string text;
};

/** 查询设备与构建环境是否支持当前 Dear ImGui 渲染后端。 */
bool imguiIsSupported();

/** 创建或更新 ImGui Surface；本函数只启动宿主，不阻塞脚本任务。 */
bool imguiShow(const ImGuiSurfaceConfig& config);

/** 关闭 ImGui Surface、唤醒等待者并保留错误信息。可重复调用。 */
void imguiClose();

/**
 * Android Surface 或 EGL 渲染线程异步启动失败时终止当前框架。
 *
 * show() 只能确认 Service 命令已经送达，真正的 Surface/EGL 创建发生在其他线程；该
 * 入口把异步错误送回等待中的脚本，避免 show(true) 永久阻塞。正常关闭不得调用它。
 */
void imguiNotifyRendererFailure(const std::string& error);

/** 脚本任务结束时彻底清空句柄、图片、事件和渲染状态。 */
void imguiReset();

/** 设置主题：1 浅色、2 经典、其他值为深色。 */
bool imguiSetColorTheme(int style);

/** 创建窗口和布局容器。 */
ImGuiHandle imguiCreateWindow(
        const std::string& title,
        float x,
        float y,
        float width,
        float height,
        bool showClose
);
bool imguiDestroyWindow(ImGuiHandle handle);
ImGuiHandle imguiCreateVerticalLayout(ImGuiHandle parent, float width, float height);
ImGuiHandle imguiCreateHorizontalLayout(ImGuiHandle parent, float width, float height);
ImGuiHandle imguiCreateTreeLayout(ImGuiHandle parent, const std::string& title, float width);
ImGuiHandle imguiCreateTabBar(ImGuiHandle parent, const std::string& title);
ImGuiHandle imguiAddTabItem(ImGuiHandle tabBar, const std::string& title);
bool imguiSameLine(ImGuiHandle handle, float spacing);
bool imguiSetLayoutBorderVisible(ImGuiHandle handle, bool visible);

/** 创建基础控件；parent=0 的按钮使用 x/y 绝对坐标放在全屏画布。 */
ImGuiHandle imguiCreateButton(
        ImGuiHandle parent,
        const std::string& text,
        float x,
        float y,
        float width,
        float height
);
ImGuiHandle imguiCreateLabel(ImGuiHandle parent, const std::string& text, bool singleLine);
ImGuiHandle imguiCreateCheckBox(ImGuiHandle parent, const std::string& label, bool checked);
ImGuiHandle imguiCreateSwitch(
        ImGuiHandle parent,
        const std::string& label,
        bool checked,
        float height
);
ImGuiHandle imguiCreateInputText(
        ImGuiHandle parent,
        const std::string& label,
        const std::string& value,
        int inputType,
        float width,
        float height
);
ImGuiHandle imguiCreateProgressBar(
        ImGuiHandle parent,
        float progress,
        float width,
        float height
);
ImGuiHandle imguiCreateSlider(
        ImGuiHandle parent,
        const std::string& label,
        int minimum,
        int maximum,
        int initialPosition,
        float width
);
ImGuiHandle imguiCreateColorPicker(
        ImGuiHandle parent,
        const std::string& title,
        std::uint32_t color,
        float width,
        float height
);

/** 创建选择控件与表格。 */
ImGuiHandle imguiCreateComboBox(
        ImGuiHandle parent,
        const std::vector<std::string>& items,
        float width
);
ImGuiHandle imguiCreateRadioGroup(ImGuiHandle parent, const std::string& label);
bool imguiAddOptionItem(ImGuiHandle handle, const std::string& text);
bool imguiAddRadioBox(ImGuiHandle handle, const std::string& text, bool wrapLine);
bool imguiGetItemText(ImGuiHandle handle, int index, std::string* text);
bool imguiRemoveItemAt(ImGuiHandle handle, int index);
bool imguiRemoveAllItems(ImGuiHandle handle);
int imguiGetSelectedItemIndex(ImGuiHandle handle);
bool imguiSetItemSelected(ImGuiHandle handle, int index);
int imguiGetItemCount(ImGuiHandle handle);
ImGuiHandle imguiCreateTable(
        ImGuiHandle parent,
        const std::string& title,
        int columns,
        bool showHeader,
        float width,
        float height
);
bool imguiSetTableHeader(ImGuiHandle handle, int column, const std::string& text);
int imguiInsertTableRow(ImGuiHandle handle, int after);
bool imguiGetTableItemText(ImGuiHandle handle, int row, int column, std::string* text);
bool imguiSetTableItemText(
        ImGuiHandle handle,
        int row,
        int column,
        const std::string& text
);
bool imguiDeleteTableRow(ImGuiHandle handle, int row);
bool imguiClearTable(ImGuiHandle handle);

/** 读取或修改控件状态。 */
bool imguiSetChecked(ImGuiHandle handle, bool checked);
bool imguiIsChecked(ImGuiHandle handle, bool* checked);
bool imguiGetInputText(ImGuiHandle handle, std::string* text);
bool imguiSetInputText(ImGuiHandle handle, const std::string& text);
bool imguiSetInputType(ImGuiHandle handle, int inputType);
bool imguiSetProgress(ImGuiHandle handle, float progress);
bool imguiGetProgress(ImGuiHandle handle, float* progress);
bool imguiSetSlider(ImGuiHandle handle, int position);
bool imguiGetSlider(ImGuiHandle handle, int* position);
bool imguiSetWidgetSize(ImGuiHandle handle, float width, float height);
bool imguiSetWidgetVisible(ImGuiHandle handle, bool visible);
bool imguiIsWidgetVisible(ImGuiHandle handle, bool* visible);
bool imguiSetWidgetStyle(ImGuiHandle handle, int style, float first, float second);
bool imguiSetWidgetColor(ImGuiHandle handle, int type, std::uint32_t color);

/** 创建或替换图片控件，路径支持普通脚本目录和当前 ALPKG 资源。 */
ImGuiHandle imguiCreateImage(
        ImGuiHandle parent,
        const std::string& path,
        float width,
        float height
);
bool imguiSetImage(ImGuiHandle handle, const std::string& path);
bool imguiSetImageRgba(
        ImGuiHandle handle,
        const unsigned char* rgba,
        int width,
        int height
);

/** 设置与读取窗口几何和标志。 */
bool imguiSetWindowPos(ImGuiHandle handle, float x, float y);
bool imguiSetWindowSize(ImGuiHandle handle, float width, float height);
bool imguiGetWindowGeometry(
        ImGuiHandle handle,
        float* x,
        float* y,
        float* width,
        float* height
);
bool imguiSetWindowFlags(ImGuiHandle handle, int flags);

/** 创建绘图图形。 */
ImGuiHandle imguiCreateRectangle(
        float x,
        float y,
        float x2,
        float y2,
        std::uint32_t color,
        bool filled,
        float rounding
);
ImGuiHandle imguiCreateCircle(
        float x,
        float y,
        float radius,
        std::uint32_t color,
        bool filled,
        int segments
);
ImGuiHandle imguiCreatePolygon(
        const std::vector<std::pair<float, float>>& points,
        std::uint32_t color,
        bool closed,
        bool filled,
        float thickness
);
ImGuiHandle imguiCreateLine(
        float x1,
        float y1,
        float x2,
        float y2,
        std::uint32_t color,
        float thickness
);
ImGuiHandle imguiCreateBitmapShape(
        float x,
        float y,
        float width,
        float height,
        const std::string& path
);
ImGuiHandle imguiCreateBitmapShapeRgba(
        float x,
        float y,
        float width,
        float height,
        const unsigned char* rgba,
        int imageWidth,
        int imageHeight
);
ImGuiHandle imguiCreateShapeText(
        float x,
        float y,
        float width,
        float height,
        const std::string& text,
        std::uint32_t textColor,
        std::uint32_t backgroundColor,
        bool hasBackground,
        float fontScale
);
int imguiSetShapePosition(ImGuiHandle handle, float x, float y);
int imguiSetShapeVisibility(ImGuiHandle handle, bool visible);
bool imguiIsShapeVisible(ImGuiHandle handle, bool* visible);
bool imguiSetShapeText(ImGuiHandle handle, const std::string& text);
bool imguiSetShapeTextColor(ImGuiHandle handle, std::uint32_t color);
bool imguiSetShapeTextBackground(
        ImGuiHandle handle,
        std::uint32_t color,
        bool hasBackground
);
bool imguiSetShapeTextFontScale(ImGuiHandle handle, float scale);
bool imguiSetBitmapShape(ImGuiHandle handle, const std::string& path);
bool imguiSetBitmapShapeRgba(
        ImGuiHandle handle,
        const unsigned char* rgba,
        int width,
        int height
);
bool imguiSetShapeThickness(ImGuiHandle handle, float thickness);
int imguiRemoveShape(ImGuiHandle handle);

/** 查询句柄，不区分窗口、控件和图形。 */
bool imguiIsValidHandle(ImGuiHandle handle);

/** 将语言绑定的 post 标识投递给同一个事件泵。 */
bool imguiPost(long long postId);

/** 解决窗口关闭回调：allowClose=false 取消关闭，true 销毁窗口。 */
bool imguiResolveWindowClose(ImGuiHandle handle, bool allowClose);

/**
 * 等待下一条脚本事件。
 *
 * 返回 true 表示取得事件；超时返回 false 且错误为空；停止、参数或生命周期错误返回 false
 * 且 imguiLastError 非空。等待期间不访问语言虚拟机。
 */
bool imguiWaitEvent(
        int timeoutMs,
        ShouldStopCallback shouldStop,
        void* stopContext,
        ImGuiEvent* event
);

/** 等待整个 ImGui 框架关闭；正常关闭返回 true，脚本停止或参数错误返回 false。 */
bool imguiWaitClosed(ShouldStopCallback shouldStop, void* stopContext);

/** 渲染线程取得当前不可变模型；没有修改时重复调用不会复制控件或图片。 */
std::shared_ptr<const ImGuiRenderSnapshot> imguiAcquireRenderSnapshot();

/** 渲染线程批量提交当前帧交互，核心会更新状态并投递结构化事件。 */
void imguiApplyInteractions(const std::vector<ImGuiInteraction>& interactions);

/** 返回当前线程最近一次 ImGui 核心 API 错误。 */
std::string imguiLastError();

} // namespace xiaoyv::api
