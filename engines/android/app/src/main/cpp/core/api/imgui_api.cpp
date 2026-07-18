/**
 * 文件用途：实现语言无关的 ImGui 控件模型、图片所有权、事件队列和脚本生命周期。
 */
#include "imgui_api.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <unordered_set>

#include "image_api.h"
#include "../../engine/json_value.h"
#include "../../platform/android_bridge.h"
#include "imgui.h"

namespace xiaoyv::api {
namespace {

// 该值是高频事件的软上限；按钮、关闭等离散事件不能为了硬性限长而被静默丢弃。
constexpr std::size_t kMaxQueuedEvents = 512;
constexpr int kWaitSliceMs = 20;

/**
 * ImGui 进程级模型。
 *
 * 引擎当前只允许一个顶层脚本，所以模型按进程保存即可。所有入口仍使用同一把互斥锁，
 * 让 Lua 的真实子线程、渲染线程和后续 JS/Go 调用同时进入时不会产生悬空句柄。
 */
struct ImGuiState {
    std::mutex mutex;
    std::condition_variable condition;
    ImGuiHandle nextHandle = 1;
    std::uint64_t nextImageRevision = 1;
    std::uint64_t revision = 1;
    std::uint64_t surfaceGeneration = 0;
    int colorTheme = 0;
    bool displayed = false;
    bool closed = true;
    std::string lifecycleError;
    ImGuiSurfaceConfig surface;
    std::unordered_map<ImGuiHandle, ImGuiWidgetSnapshot> widgets;
    std::vector<ImGuiHandle> windows;
    std::vector<ImGuiHandle> rootWidgets;
    std::vector<ImGuiShapeSnapshot> shapes;
    std::deque<ImGuiEvent> events;
    std::shared_ptr<const ImGuiRenderSnapshot> cachedSnapshot;
};

ImGuiState gState;
thread_local std::string gLastError;

bool setError(const std::string& error) {
    gLastError = error;
    return false;
}

void clearError() {
    gLastError.clear();
}

bool finite(float value) {
    return std::isfinite(static_cast<double>(value));
}

bool validSize(float value) {
    return finite(value) && value >= -1.0F;
}

void markDirtyLocked() {
    ++gState.revision;
    if (gState.revision == 0) {
        gState.revision = 1;
    }
    gState.cachedSnapshot.reset();
}

ImGuiHandle allocateHandleLocked() {
    while (gState.nextHandle <= 0
            || gState.widgets.count(gState.nextHandle) != 0
            || std::any_of(
                    gState.shapes.begin(),
                    gState.shapes.end(),
                    [&](const ImGuiShapeSnapshot& shape) {
                        return shape.handle == gState.nextHandle;
                    }
            )) {
        ++gState.nextHandle;
        if (gState.nextHandle <= 0) {
            gState.nextHandle = 1;
        }
    }
    return gState.nextHandle++;
}

bool widgetCanOwnChildren(ImGuiWidgetKind kind) {
    return kind == ImGuiWidgetKind::Window
            || kind == ImGuiWidgetKind::VerticalLayout
            || kind == ImGuiWidgetKind::HorizontalLayout
            || kind == ImGuiWidgetKind::TreeLayout
            || kind == ImGuiWidgetKind::TabItem;
}

ImGuiWidgetSnapshot* findWidgetLocked(ImGuiHandle handle) {
    auto iterator = gState.widgets.find(handle);
    return iterator == gState.widgets.end() ? nullptr : &iterator->second;
}

const ImGuiWidgetSnapshot* findWidgetLockedConst(ImGuiHandle handle) {
    auto iterator = gState.widgets.find(handle);
    return iterator == gState.widgets.end() ? nullptr : &iterator->second;
}

ImGuiShapeSnapshot* findShapeLocked(ImGuiHandle handle) {
    auto iterator = std::find_if(
            gState.shapes.begin(),
            gState.shapes.end(),
            [handle](const ImGuiShapeSnapshot& shape) { return shape.handle == handle; }
    );
    return iterator == gState.shapes.end() ? nullptr : &*iterator;
}

bool validateParentLocked(ImGuiHandle parent, ImGuiWidgetSnapshot** parentWidget) {
    if (parent <= 0) {
        return setError("父容器句柄无效");
    }
    ImGuiWidgetSnapshot* widget = findWidgetLocked(parent);
    if (widget == nullptr || !widgetCanOwnChildren(widget->kind)) {
        return setError("父句柄不是可添加控件的窗口或布局");
    }
    if (parentWidget != nullptr) {
        *parentWidget = widget;
    }
    return true;
}

ImGuiHandle addWidgetLocked(ImGuiWidgetSnapshot widget, bool allowRoot) {
    if (widget.parent > 0) {
        ImGuiWidgetSnapshot* parent = nullptr;
        if (!validateParentLocked(widget.parent, &parent)) {
            return 0;
        }
        widget.handle = allocateHandleLocked();
        parent->children.push_back(widget.handle);
    } else if (allowRoot) {
        widget.handle = allocateHandleLocked();
        gState.rootWidgets.push_back(widget.handle);
    } else {
        setError("控件必须指定有效父容器");
        return 0;
    }

    ImGuiHandle handle = widget.handle;
    gState.widgets.emplace(handle, std::move(widget));
    markDirtyLocked();
    clearError();
    return handle;
}

void eraseFromVector(std::vector<ImGuiHandle>* handles, ImGuiHandle handle) {
    handles->erase(std::remove(handles->begin(), handles->end(), handle), handles->end());
}

void eraseWidgetTreeLocked(ImGuiHandle handle) {
    auto iterator = gState.widgets.find(handle);
    if (iterator == gState.widgets.end()) {
        return;
    }
    std::vector<ImGuiHandle> children = iterator->second.children;
    ImGuiHandle parent = iterator->second.parent;
    for (ImGuiHandle child : children) {
        eraseWidgetTreeLocked(child);
    }
    if (parent > 0) {
        ImGuiWidgetSnapshot* parentWidget = findWidgetLocked(parent);
        if (parentWidget != nullptr) {
            eraseFromVector(&parentWidget->children, handle);
        }
    }
    eraseFromVector(&gState.windows, handle);
    eraseFromVector(&gState.rootWidgets, handle);
    gState.widgets.erase(handle);
}

/**
 * 投递脚本事件。
 *
 * 同一滑块尚未消费的变化只保留最新值。队列达到软上限后继续优先淘汰滑块事件；若队列
 * 已全部是按钮、选择、关闭等离散操作，则允许短暂超过软上限，避免用户操作被静默丢失。
 */
void queueEventLocked(ImGuiEvent event) {
    if (event.type == ImGuiEventType::Slider) {
        for (auto iterator = gState.events.rbegin(); iterator != gState.events.rend(); ++iterator) {
            if (iterator->type == ImGuiEventType::Slider && iterator->handle == event.handle) {
                *iterator = std::move(event);
                gState.condition.notify_all();
                return;
            }
        }
    }

    if (gState.events.size() >= kMaxQueuedEvents) {
        auto disposable = std::find_if(
                gState.events.begin(),
                gState.events.end(),
                [](const ImGuiEvent& queued) {
                    return queued.type == ImGuiEventType::Slider;
                }
        );
        if (disposable != gState.events.end()) {
            gState.events.erase(disposable);
        } else if (event.type == ImGuiEventType::Slider) {
            // 全部离散事件都比新的高频滑块变化重要；本次滑块值仍会保留在控件模型中。
            return;
        }
    }
    gState.events.push_back(std::move(event));
    gState.condition.notify_all();
}

std::string surfaceConfigJson(const ImGuiSurfaceConfig& config) {
    std::map<std::string, JsonValue> fields;
    fields["windowed"] = JsonValue::makeBool(config.windowed);
    fields["touchable"] = JsonValue::makeBool(config.touchable);
    fields["x"] = JsonValue::makeNumber(config.x);
    fields["y"] = JsonValue::makeNumber(config.y);
    fields["width"] = JsonValue::makeNumber(config.width);
    fields["height"] = JsonValue::makeNumber(config.height);
    fields["hasTitle"] = JsonValue::makeBool(config.hasTitle);
    fields["title"] = JsonValue::makeString(config.title);
    fields["titleColor"] = JsonValue::makeNumber(config.titleColor);
    fields["titleBackgroundColor"] = JsonValue::makeNumber(config.titleBackgroundColor);
    fields["hasClose"] = JsonValue::makeBool(config.hasClose);
    fields["closeColor"] = JsonValue::makeNumber(config.closeColor);
    fields["hasResize"] = JsonValue::makeBool(config.hasResize);
    fields["resizeColor"] = JsonValue::makeNumber(config.resizeColor);
    fields["hasToggle"] = JsonValue::makeBool(config.hasToggle);
    fields["toggleColor"] = JsonValue::makeNumber(config.toggleColor);
    fields["titleFontSize"] = JsonValue::makeNumber(config.titleFontSize);
    fields["fontPath"] = JsonValue::makeString(config.fontPath);
    fields["fontSize"] = JsonValue::makeNumber(config.fontSize);
    return jsonValueToString(JsonValue::makeObject(std::move(fields)));
}

std::shared_ptr<const ImGuiImagePixels> makeImageFromRgba(
        const unsigned char* rgba,
        int width,
        int height,
        std::uint64_t revision
) {
    if (rgba == nullptr || width <= 0 || height <= 0) {
        setError("RGBA 图片点阵或尺寸无效");
        return nullptr;
    }
    const std::size_t imageWidth = static_cast<std::size_t>(width);
    const std::size_t imageHeight = static_cast<std::size_t>(height);
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    if (imageWidth > maximum / 4U
            || imageHeight > maximum / (imageWidth * 4U)) {
        setError("RGBA 图片尺寸过大");
        return nullptr;
    }
    const std::size_t byteCount = imageWidth * imageHeight * 4U;

    std::shared_ptr<std::vector<unsigned char>> bytes;
    try {
        bytes = std::make_shared<std::vector<unsigned char>>(rgba, rgba + byteCount);
    } catch (...) {
        setError("RGBA 图片内存分配失败");
        return nullptr;
    }
    auto image = std::make_shared<ImGuiImagePixels>();
    image->width = width;
    image->height = height;
    image->revision = revision;
    image->rgba = std::move(bytes);
    return image;
}

std::shared_ptr<const ImGuiImagePixels> loadImage(
        const std::string& path,
        std::uint64_t revision
) {
    if (path.empty()) {
        return nullptr;
    }
    脚本图片 decoded;
    if (!加载脚本图片(path.c_str(), &decoded)) {
        setError(取图片错误().empty() ? "加载 ImGui 图片失败" : 取图片错误());
        return nullptr;
    }
    return makeImageFromRgba(decoded.rgba.data(), decoded.width, decoded.height, revision);
}

bool widgetKindIsOneOf(
        const ImGuiWidgetSnapshot* widget,
        std::initializer_list<ImGuiWidgetKind> kinds
) {
    if (widget == nullptr) {
        return false;
    }
    return std::find(kinds.begin(), kinds.end(), widget->kind) != kinds.end();
}

} // namespace

bool imguiIsSupported() {
    bool supported = AndroidBridge::isScriptImGuiSupported();
    if (!supported) {
        return setError("当前设备不支持 OpenGL ES 3 ImGui 渲染");
    }
    clearError();
    return true;
}

bool imguiShow(const ImGuiSurfaceConfig& config) {
    if (!imguiIsSupported()) {
        return false;
    }
    if (config.windowed && (config.width <= 0 || config.height <= 0)) {
        return setError("独立 ImGui 窗口的宽度和高度必须大于 0");
    }
    if (!finite(config.fontSize) || config.fontSize <= 0.0F) {
        return setError("ImGui 内容字体大小必须大于 0");
    }
    if (config.windowed && config.hasTitle
            && (!finite(config.titleFontSize) || config.titleFontSize <= 0.0F)) {
        return setError("ImGui 标题字体大小必须大于 0");
    }

    {
        std::lock_guard<std::mutex> lock(gState.mutex);
        ++gState.surfaceGeneration;
        if (gState.surfaceGeneration == 0) {
            // 0 保留给“尚未显示”；极端溢出时回到 1，仍会让渲染器识别为新会话。
            gState.surfaceGeneration = 1;
        }
        gState.surface = config;
        gState.displayed = true;
        gState.closed = false;
        gState.lifecycleError.clear();

        // showWindow 以一个小型 Surface 作为画布，兼容规则要求第一个 ImGui 窗口默认
        // 填满这块画布。显示成功后脚本仍可通过 setWindowPos/setWindowSize 再次调整。
        if (config.windowed && !gState.windows.empty()) {
            ImGuiWidgetSnapshot* firstWindow = findWidgetLocked(gState.windows.front());
            if (firstWindow != nullptr && firstWindow->kind == ImGuiWidgetKind::Window) {
                firstWindow->x = 0.0F;
                firstWindow->y = 0.0F;
                firstWindow->width = static_cast<float>(config.width);
                firstWindow->height = static_cast<float>(config.height);
            }
        }
        markDirtyLocked();
    }
    if (!AndroidBridge::showScriptImGui(surfaceConfigJson(config))) {
        std::lock_guard<std::mutex> lock(gState.mutex);
        gState.displayed = false;
        gState.closed = true;
        markDirtyLocked();
        gState.condition.notify_all();
        return setError("Android ImGui Surface 创建失败，请检查悬浮窗权限");
    }

    clearError();
    return true;
}

void imguiClose() {
    bool notifyClosed = false;
    {
        std::lock_guard<std::mutex> lock(gState.mutex);
        if (!gState.closed || gState.displayed) {
            gState.displayed = false;
            gState.closed = true;
            ImGuiEvent closedEvent;
            closedEvent.type = ImGuiEventType::FrameworkClosed;
            queueEventLocked(std::move(closedEvent));
            markDirtyLocked();
            notifyClosed = true;
        }
    }
    if (notifyClosed) {
        AndroidBridge::closeScriptImGui();
        gState.condition.notify_all();
    }
    clearError();
}

void imguiNotifyRendererFailure(const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(gState.mutex);
        // 正常 close() 后 Surface 销毁属于预期生命周期，迟到的渲染错误不能覆盖结果。
        if (gState.closed && !gState.displayed) {
            return;
        }
        gState.lifecycleError = error.empty() ? "ImGui 渲染器启动失败" : error;
        gState.displayed = false;
        gState.closed = true;
        ImGuiEvent closedEvent;
        closedEvent.type = ImGuiEventType::FrameworkClosed;
        closedEvent.text = gState.lifecycleError;
        queueEventLocked(std::move(closedEvent));
        markDirtyLocked();
    }
    gState.condition.notify_all();
    AndroidBridge::closeScriptImGui();
}

void imguiReset() {
    imguiClose();
    {
        std::lock_guard<std::mutex> lock(gState.mutex);
        // 三个序号保持进程级单调递增。HTTP 强制关闭界面后脚本可能继续运行；如果重新
        // 从 1 分配句柄，Lua 绑定中尚未销毁的旧回调可能误绑定到新控件。Surface 序号
        // 同理不能复用，否则尚在退出的渲染线程会继承旧窗口的收起状态。
        gState.colorTheme = 0;
        gState.lifecycleError.clear();
        gState.surface = ImGuiSurfaceConfig{};
        gState.widgets.clear();
        gState.windows.clear();
        gState.rootWidgets.clear();
        gState.shapes.clear();
        gState.events.clear();
        markDirtyLocked();
    }
    gState.condition.notify_all();
    clearError();
}

bool imguiSetColorTheme(int style) {
    // 懒人精灵约定该方法的布尔返回值同时表示当前环境是否支持 ImGui。
    if (!imguiIsSupported()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    gState.colorTheme = style;
    markDirtyLocked();
    clearError();
    return true;
}

ImGuiHandle imguiCreateWindow(
        const std::string& title,
        float x,
        float y,
        float width,
        float height,
        bool showClose
) {
    if (!finite(x) || !finite(y) || !finite(width) || !finite(height)
            || width <= 0.0F || height <= 0.0F) {
        setError("窗口坐标或尺寸无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot window;
    window.handle = allocateHandleLocked();
    window.kind = ImGuiWidgetKind::Window;
    window.title = title;
    window.x = x;
    window.y = y;
    window.width = width;
    window.height = height;
    window.showClose = showClose;
    gState.windows.push_back(window.handle);
    ImGuiHandle handle = window.handle;
    gState.widgets.emplace(handle, std::move(window));
    markDirtyLocked();
    clearError();
    return handle;
}

bool imguiDestroyWindow(ImGuiHandle handle) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    const ImGuiWidgetSnapshot* window = findWidgetLockedConst(handle);
    if (window == nullptr || window->kind != ImGuiWidgetKind::Window) {
        return setError("窗口句柄无效");
    }
    eraseWidgetTreeLocked(handle);
    markDirtyLocked();
    clearError();
    return true;
}

ImGuiHandle imguiCreateVerticalLayout(ImGuiHandle parent, float width, float height) {
    if (!validSize(width) || !validSize(height)) {
        setError("垂直布局尺寸无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::VerticalLayout;
    widget.width = width;
    widget.height = height;
    return addWidgetLocked(std::move(widget), false);
}

ImGuiHandle imguiCreateHorizontalLayout(ImGuiHandle parent, float width, float height) {
    if (!validSize(width) || !validSize(height)) {
        setError("水平布局尺寸无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::HorizontalLayout;
    widget.width = width;
    widget.height = height;
    return addWidgetLocked(std::move(widget), false);
}

ImGuiHandle imguiCreateTreeLayout(ImGuiHandle parent, const std::string& title, float width) {
    if (!validSize(width)) {
        setError("树形布局宽度无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::TreeLayout;
    widget.title = title;
    widget.width = width;
    return addWidgetLocked(std::move(widget), false);
}

ImGuiHandle imguiCreateTabBar(ImGuiHandle parent, const std::string& title) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::TabBar;
    widget.title = title;
    return addWidgetLocked(std::move(widget), false);
}

ImGuiHandle imguiAddTabItem(ImGuiHandle tabBar, const std::string& title) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* parent = findWidgetLocked(tabBar);
    if (parent == nullptr || parent->kind != ImGuiWidgetKind::TabBar) {
        setError("标签栏句柄无效");
        return 0;
    }
    ImGuiWidgetSnapshot item;
    item.handle = allocateHandleLocked();
    item.parent = tabBar;
    item.kind = ImGuiWidgetKind::TabItem;
    item.title = title;
    parent->children.push_back(item.handle);
    ImGuiHandle handle = item.handle;
    gState.widgets.emplace(handle, std::move(item));
    markDirtyLocked();
    clearError();
    return handle;
}

bool imguiSameLine(ImGuiHandle handle, float spacing) {
    if (!finite(spacing) || spacing < -1.0F) {
        return setError("同行间距必须大于等于 -1");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr) {
        return setError("控件句柄无效");
    }
    widget->sameLine = true;
    widget->sameLineSpacing = spacing;
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetLayoutBorderVisible(ImGuiHandle handle, bool visible) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (!widgetKindIsOneOf(widget, {
            ImGuiWidgetKind::VerticalLayout,
            ImGuiWidgetKind::HorizontalLayout,
            ImGuiWidgetKind::TreeLayout
    })) {
        return setError("句柄不是布局容器");
    }
    widget->borderVisible = visible;
    markDirtyLocked();
    clearError();
    return true;
}

ImGuiHandle imguiCreateButton(
        ImGuiHandle parent,
        const std::string& text,
        float x,
        float y,
        float width,
        float height
) {
    if (!finite(x) || !finite(y) || !validSize(width) || !validSize(height)) {
        setError("按钮坐标或尺寸无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::Button;
    widget.text = text;
    widget.x = x;
    widget.y = y;
    widget.width = width;
    widget.height = height;
    return addWidgetLocked(std::move(widget), parent == 0);
}

ImGuiHandle imguiCreateLabel(ImGuiHandle parent, const std::string& text, bool singleLine) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::Label;
    widget.text = text;
    widget.singleLine = singleLine;
    return addWidgetLocked(std::move(widget), false);
}

ImGuiHandle imguiCreateCheckBox(ImGuiHandle parent, const std::string& label, bool checked) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::CheckBox;
    widget.text = label;
    widget.checked = checked;
    return addWidgetLocked(std::move(widget), false);
}

ImGuiHandle imguiCreateSwitch(
        ImGuiHandle parent,
        const std::string& label,
        bool checked,
        float height
) {
    if (!validSize(height)) {
        setError("开关高度无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::Switch;
    widget.text = label;
    widget.checked = checked;
    widget.height = height;
    return addWidgetLocked(std::move(widget), false);
}

ImGuiHandle imguiCreateInputText(
        ImGuiHandle parent,
        const std::string& label,
        const std::string& value,
        int inputType,
        float width,
        float height
) {
    if (inputType < 0 || inputType > 2 || !validSize(width) || !validSize(height)) {
        setError("输入框类型或尺寸无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::InputText;
    widget.title = label;
    widget.text = value;
    widget.inputType = inputType;
    widget.width = width;
    widget.height = height;
    return addWidgetLocked(std::move(widget), false);
}

ImGuiHandle imguiCreateProgressBar(
        ImGuiHandle parent,
        float progress,
        float width,
        float height
) {
    if (!finite(progress) || !validSize(width) || !validSize(height)) {
        setError("进度条参数无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::ProgressBar;
    widget.progress = std::clamp(progress, 0.0F, 1.0F);
    widget.width = width;
    widget.height = height;
    return addWidgetLocked(std::move(widget), false);
}

ImGuiHandle imguiCreateSlider(
        ImGuiHandle parent,
        const std::string& label,
        int minimum,
        int maximum,
        int initialPosition,
        float width
) {
    if (minimum >= maximum || initialPosition < minimum || initialPosition > maximum
            || !validSize(width)) {
        setError("滑动条范围、初始位置或宽度无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::Slider;
    widget.text = label;
    widget.minimum = minimum;
    widget.maximum = maximum;
    widget.integerValue = initialPosition;
    widget.width = width;
    return addWidgetLocked(std::move(widget), false);
}

ImGuiHandle imguiCreateColorPicker(
        ImGuiHandle parent,
        const std::string& title,
        std::uint32_t color,
        float width,
        float height
) {
    if (!validSize(width) || !validSize(height)) {
        setError("颜色选择器尺寸无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::ColorPicker;
    widget.title = title.empty() ? "Color" : title;
    widget.color = color;
    widget.width = width;
    widget.height = height;
    return addWidgetLocked(std::move(widget), false);
}

ImGuiHandle imguiCreateComboBox(
        ImGuiHandle parent,
        const std::vector<std::string>& items,
        float width
) {
    if (!validSize(width)) {
        setError("组合框宽度无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::ComboBox;
    widget.items = items;
    widget.selectedIndex = items.empty() ? -1 : 0;
    widget.width = width;
    return addWidgetLocked(std::move(widget), false);
}

ImGuiHandle imguiCreateRadioGroup(ImGuiHandle parent, const std::string& label) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::RadioGroup;
    widget.title = label;
    widget.selectedIndex = -1;
    return addWidgetLocked(std::move(widget), false);
}

bool imguiAddOptionItem(ImGuiHandle handle, const std::string& text) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::ComboBox) {
        return setError("组合框句柄无效");
    }
    widget->items.push_back(text);
    // 追加选项不改变当前选择；空组合框追加第一项后仍保持 -1，交给脚本显式选择。
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiAddRadioBox(ImGuiHandle handle, const std::string& text, bool wrapLine) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::RadioGroup) {
        return setError("单选组句柄无效");
    }
    widget->items.push_back(text);
    widget->radioWrapAfter.push_back(wrapLine ? 1U : 0U);
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiGetItemText(ImGuiHandle handle, int index, std::string* text) {
    if (text == nullptr) {
        return setError("选项文本输出地址为空");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    const ImGuiWidgetSnapshot* widget = findWidgetLockedConst(handle);
    if (!widgetKindIsOneOf(widget, {ImGuiWidgetKind::ComboBox, ImGuiWidgetKind::RadioGroup})) {
        return setError("句柄不是组合框或单选组");
    }
    if (index < 0 || index >= static_cast<int>(widget->items.size())) {
        return setError("选项索引超出范围");
    }
    *text = widget->items[static_cast<std::size_t>(index)];
    clearError();
    return true;
}

bool imguiRemoveItemAt(ImGuiHandle handle, int index) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (!widgetKindIsOneOf(widget, {ImGuiWidgetKind::ComboBox, ImGuiWidgetKind::RadioGroup})) {
        return setError("句柄不是组合框或单选组");
    }
    if (index < 0 || index >= static_cast<int>(widget->items.size())) {
        return setError("选项索引超出范围");
    }
    widget->items.erase(widget->items.begin() + index);
    if (index < static_cast<int>(widget->radioWrapAfter.size())) {
        widget->radioWrapAfter.erase(widget->radioWrapAfter.begin() + index);
    }
    if (widget->items.empty()) {
        widget->selectedIndex = -1;
    } else if (widget->selectedIndex > index) {
        --widget->selectedIndex;
    } else if (widget->selectedIndex == index) {
        widget->selectedIndex = std::min(index, static_cast<int>(widget->items.size()) - 1);
    }
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiRemoveAllItems(ImGuiHandle handle) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (!widgetKindIsOneOf(widget, {ImGuiWidgetKind::ComboBox, ImGuiWidgetKind::RadioGroup})) {
        return setError("句柄不是组合框或单选组");
    }
    widget->items.clear();
    widget->radioWrapAfter.clear();
    widget->selectedIndex = -1;
    markDirtyLocked();
    clearError();
    return true;
}

int imguiGetSelectedItemIndex(ImGuiHandle handle) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    const ImGuiWidgetSnapshot* widget = findWidgetLockedConst(handle);
    if (!widgetKindIsOneOf(widget, {
            ImGuiWidgetKind::ComboBox,
            ImGuiWidgetKind::RadioGroup,
            ImGuiWidgetKind::Table
    })) {
        setError("句柄不是可选择控件");
        return -2;
    }
    clearError();
    return widget->kind == ImGuiWidgetKind::Table ? widget->selectedRow : widget->selectedIndex;
}

bool imguiSetItemSelected(ImGuiHandle handle, int index) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr) {
        return setError("控件句柄无效");
    }

    ImGuiEvent event;
    event.handle = handle;
    event.index = index;
    if (widget->kind == ImGuiWidgetKind::Table) {
        if (index < 0 || index >= static_cast<int>(widget->rows.size())) {
            return setError("表格行索引超出范围");
        }
        const int selectedColumn = widget->columns > 0 ? 0 : -1;
        if (widget->selectedRow == index && widget->selectedColumn == selectedColumn) {
            clearError();
            return true;
        }
        widget->selectedRow = index;
        widget->selectedColumn = selectedColumn;
        event.type = ImGuiEventType::TableSelect;
        event.row = widget->selectedRow;
        event.column = widget->selectedColumn;
        if (event.column >= 0) {
            event.text = widget->rows[static_cast<std::size_t>(event.row)]
                    [static_cast<std::size_t>(event.column)];
        }
    } else if (widgetKindIsOneOf(widget, {
            ImGuiWidgetKind::ComboBox,
            ImGuiWidgetKind::RadioGroup
    })) {
        if (index < 0 || index >= static_cast<int>(widget->items.size())) {
            return setError("选项索引超出范围");
        }
        if (widget->selectedIndex == index) {
            clearError();
            return true;
        }
        widget->selectedIndex = index;
        event.type = ImGuiEventType::Select;
        event.text = widget->items[static_cast<std::size_t>(index)];
    } else {
        return setError("句柄不是可选择控件");
    }

    queueEventLocked(std::move(event));
    markDirtyLocked();
    clearError();
    return true;
}

int imguiGetItemCount(ImGuiHandle handle) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    const ImGuiWidgetSnapshot* widget = findWidgetLockedConst(handle);
    if (widget == nullptr) {
        setError("控件句柄无效");
        return -1;
    }
    if (widget->kind == ImGuiWidgetKind::Table) {
        clearError();
        return static_cast<int>(widget->rows.size());
    }
    if (widgetKindIsOneOf(widget, {ImGuiWidgetKind::ComboBox, ImGuiWidgetKind::RadioGroup})) {
        clearError();
        return static_cast<int>(widget->items.size());
    }
    setError("当前控件不包含可计数项目");
    return -1;
}

ImGuiHandle imguiCreateTable(
        ImGuiHandle parent,
        const std::string& title,
        int columns,
        bool showHeader,
        float width,
        float height
) {
    if (columns <= 0 || !validSize(width) || !validSize(height)) {
        setError("表格列数或尺寸无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::Table;
    widget.title = title;
    widget.columns = columns;
    widget.showHeader = showHeader;
    widget.headers.resize(static_cast<std::size_t>(columns));
    widget.width = width;
    widget.height = height;
    return addWidgetLocked(std::move(widget), false);
}

bool imguiSetTableHeader(ImGuiHandle handle, int column, const std::string& text) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Table) {
        return setError("表格句柄无效");
    }
    if (column < 0 || column >= widget->columns) {
        return setError("表格列索引超出范围");
    }
    widget->headers[static_cast<std::size_t>(column)] = text;
    markDirtyLocked();
    clearError();
    return true;
}

int imguiInsertTableRow(ImGuiHandle handle, int after) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Table) {
        setError("表格句柄无效");
        return -1;
    }

    int insertIndex;
    if (after == -1) {
        insertIndex = 0;
    } else if (after == -2) {
        insertIndex = static_cast<int>(widget->rows.size());
    } else if (after >= 0 && after < static_cast<int>(widget->rows.size())) {
        insertIndex = after + 1;
    } else {
        setError("表格插入位置无效");
        return -1;
    }

    widget->rows.insert(
            widget->rows.begin() + insertIndex,
            std::vector<std::string>(static_cast<std::size_t>(widget->columns))
    );
    if (widget->selectedRow >= insertIndex) {
        ++widget->selectedRow;
    }
    markDirtyLocked();
    clearError();
    return insertIndex;
}

bool imguiGetTableItemText(ImGuiHandle handle, int row, int column, std::string* text) {
    if (text == nullptr) {
        return setError("单元格文本输出地址为空");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    const ImGuiWidgetSnapshot* widget = findWidgetLockedConst(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Table) {
        return setError("表格句柄无效");
    }
    if (row < 0 || row >= static_cast<int>(widget->rows.size())
            || column < 0 || column >= widget->columns) {
        return setError("表格行或列索引超出范围");
    }
    *text = widget->rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
    clearError();
    return true;
}

bool imguiSetTableItemText(
        ImGuiHandle handle,
        int row,
        int column,
        const std::string& text
) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Table) {
        return setError("表格句柄无效");
    }
    if (row < 0 || row >= static_cast<int>(widget->rows.size())
            || column < 0 || column >= widget->columns) {
        return setError("表格行或列索引超出范围");
    }
    widget->rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = text;
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiDeleteTableRow(ImGuiHandle handle, int row) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Table) {
        return setError("表格句柄无效");
    }
    if (row < 0 || row >= static_cast<int>(widget->rows.size())) {
        return setError("表格行索引超出范围");
    }
    widget->rows.erase(widget->rows.begin() + row);
    if (widget->selectedRow == row) {
        widget->selectedRow = -1;
        widget->selectedColumn = -1;
    } else if (widget->selectedRow > row) {
        --widget->selectedRow;
    }
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiClearTable(ImGuiHandle handle) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Table) {
        return setError("表格句柄无效");
    }
    widget->rows.clear();
    widget->selectedRow = -1;
    widget->selectedColumn = -1;
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetChecked(ImGuiHandle handle, bool checked) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (!widgetKindIsOneOf(widget, {ImGuiWidgetKind::CheckBox, ImGuiWidgetKind::Switch})) {
        return setError("句柄不是复选框或开关");
    }
    if (widget->checked == checked) {
        clearError();
        return true;
    }
    widget->checked = checked;
    ImGuiEvent event;
    event.type = ImGuiEventType::Check;
    event.handle = handle;
    event.boolValue = checked;
    queueEventLocked(std::move(event));
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiIsChecked(ImGuiHandle handle, bool* checked) {
    if (checked == nullptr) {
        return setError("选中状态输出地址为空");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    const ImGuiWidgetSnapshot* widget = findWidgetLockedConst(handle);
    if (!widgetKindIsOneOf(widget, {ImGuiWidgetKind::CheckBox, ImGuiWidgetKind::Switch})) {
        return setError("句柄不是复选框或开关");
    }
    *checked = widget->checked;
    clearError();
    return true;
}

bool imguiGetInputText(ImGuiHandle handle, std::string* text) {
    if (text == nullptr) {
        return setError("输入文本输出地址为空");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    const ImGuiWidgetSnapshot* widget = findWidgetLockedConst(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::InputText) {
        return setError("输入框句柄无效");
    }
    *text = widget->text;
    clearError();
    return true;
}

bool imguiSetInputText(ImGuiHandle handle, const std::string& text) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::InputText) {
        return setError("输入框句柄无效");
    }
    widget->text = text;
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetInputType(ImGuiHandle handle, int inputType) {
    if (inputType < 0 || inputType > 2) {
        return setError("输入类型只能是 0、1 或 2");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::InputText) {
        return setError("输入框句柄无效");
    }
    if (widget->inputType != inputType) {
        widget->inputType = inputType;
        // 与懒人精灵保持一致：切换输入类型会清空旧内容，避免密码/多行内部编辑状态
        // 被错误继承到另一种输入控件。
        widget->text.clear();
    }
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetProgress(ImGuiHandle handle, float progress) {
    if (!finite(progress)) {
        return setError("进度值无效");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::ProgressBar) {
        return setError("进度条句柄无效");
    }
    widget->progress = std::clamp(progress, 0.0F, 1.0F);
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiGetProgress(ImGuiHandle handle, float* progress) {
    if (progress == nullptr) {
        return setError("进度值输出地址为空");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    const ImGuiWidgetSnapshot* widget = findWidgetLockedConst(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::ProgressBar) {
        return setError("进度条句柄无效");
    }
    *progress = widget->progress;
    clearError();
    return true;
}

bool imguiSetSlider(ImGuiHandle handle, int position) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Slider) {
        return setError("滑动条句柄无效");
    }
    if (position < widget->minimum || position > widget->maximum) {
        return setError("滑动条位置超出范围");
    }
    if (widget->integerValue == position) {
        clearError();
        return true;
    }
    widget->integerValue = position;
    ImGuiEvent event;
    event.type = ImGuiEventType::Slider;
    event.handle = handle;
    event.integerValue = position;
    queueEventLocked(std::move(event));
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiGetSlider(ImGuiHandle handle, int* position) {
    if (position == nullptr) {
        return setError("滑动条位置输出地址为空");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    const ImGuiWidgetSnapshot* widget = findWidgetLockedConst(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Slider) {
        return setError("滑动条句柄无效");
    }
    *position = widget->integerValue;
    clearError();
    return true;
}

bool imguiSetWidgetSize(ImGuiHandle handle, float width, float height) {
    if (!validSize(width) || !validSize(height)) {
        return setError("控件尺寸无效");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr) {
        return setError("控件句柄无效");
    }
    if (widget->kind == ImGuiWidgetKind::Window && (width <= 0.0F || height <= 0.0F)) {
        return setError("窗口宽度和高度必须大于 0");
    }
    widget->width = width;
    widget->height = height;
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetWidgetVisible(ImGuiHandle handle, bool visible) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr) {
        return setError("控件句柄无效");
    }
    widget->visible = visible;
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiIsWidgetVisible(ImGuiHandle handle, bool* visible) {
    if (visible == nullptr) {
        return setError("可见状态输出地址为空");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    const ImGuiWidgetSnapshot* widget = findWidgetLockedConst(handle);
    if (widget == nullptr) {
        return setError("控件句柄无效");
    }
    *visible = widget->visible;
    clearError();
    return true;
}

bool imguiSetWidgetStyle(ImGuiHandle handle, int style, float first, float second) {
    if (style < 0 || style >= ImGuiStyleVar_COUNT || !finite(first) || !finite(second)) {
        return setError("控件样式索引或属性值无效");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr) {
        return setError("控件句柄无效");
    }
    widget->styleValues[style] = {first, second};
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetWidgetColor(ImGuiHandle handle, int type, std::uint32_t color) {
    if (type < 0 || type >= ImGuiCol_COUNT) {
        return setError("控件颜色类型无效");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr) {
        return setError("控件句柄无效");
    }
    widget->styleColors[type] = color;
    markDirtyLocked();
    clearError();
    return true;
}

ImGuiHandle imguiCreateImage(
        ImGuiHandle parent,
        const std::string& path,
        float width,
        float height
) {
    if (!validSize(width) || !validSize(height)) {
        setError("图片控件尺寸无效");
        return 0;
    }

    std::uint64_t imageRevision;
    {
        std::lock_guard<std::mutex> lock(gState.mutex);
        imageRevision = gState.nextImageRevision++;
    }
    std::shared_ptr<const ImGuiImagePixels> image = loadImage(path, imageRevision);
    if (!path.empty() && image == nullptr) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot widget;
    widget.parent = parent;
    widget.kind = ImGuiWidgetKind::Image;
    widget.width = width;
    widget.height = height;
    widget.image = std::move(image);
    return addWidgetLocked(std::move(widget), false);
}

bool imguiSetImage(ImGuiHandle handle, const std::string& path) {
    std::uint64_t imageRevision;
    {
        std::lock_guard<std::mutex> lock(gState.mutex);
        imageRevision = gState.nextImageRevision++;
    }
    std::shared_ptr<const ImGuiImagePixels> image = loadImage(path, imageRevision);
    if (!path.empty() && image == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Image) {
        return setError("图片控件句柄无效");
    }
    widget->image = std::move(image);
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetImageRgba(
        ImGuiHandle handle,
        const unsigned char* rgba,
        int width,
        int height
) {
    std::uint64_t imageRevision;
    {
        std::lock_guard<std::mutex> lock(gState.mutex);
        imageRevision = gState.nextImageRevision++;
    }
    std::shared_ptr<const ImGuiImagePixels> image = makeImageFromRgba(
            rgba,
            width,
            height,
            imageRevision
    );
    if (image == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Image) {
        return setError("图片控件句柄无效");
    }
    widget->image = std::move(image);
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetWindowPos(ImGuiHandle handle, float x, float y) {
    if (!finite(x) || !finite(y)) {
        return setError("窗口坐标无效");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Window) {
        return setError("窗口句柄无效");
    }
    widget->x = x;
    widget->y = y;
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetWindowSize(ImGuiHandle handle, float width, float height) {
    if (!finite(width) || !finite(height) || width <= 0.0F || height <= 0.0F) {
        return setError("窗口宽度和高度必须大于 0");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Window) {
        return setError("窗口句柄无效");
    }
    widget->width = width;
    widget->height = height;
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiGetWindowGeometry(
        ImGuiHandle handle,
        float* x,
        float* y,
        float* width,
        float* height
) {
    if (x == nullptr || y == nullptr || width == nullptr || height == nullptr) {
        return setError("窗口几何输出地址为空");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    const ImGuiWidgetSnapshot* widget = findWidgetLockedConst(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Window) {
        return setError("窗口句柄无效");
    }
    *x = widget->x;
    *y = widget->y;
    *width = widget->width;
    *height = widget->height;
    clearError();
    return true;
}

bool imguiSetWindowFlags(ImGuiHandle handle, int flags) {
    constexpr int kPublicWindowFlags = (1 << 19) - 1;
    if (flags < 0 || (flags & ~kPublicWindowFlags) != 0) {
        return setError("窗口标志包含 Dear ImGui 内部或未知位");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* widget = findWidgetLocked(handle);
    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Window) {
        return setError("窗口句柄无效");
    }
    widget->windowFlags = flags;
    markDirtyLocked();
    clearError();
    return true;
}

ImGuiHandle imguiCreateRectangle(
        float x,
        float y,
        float x2,
        float y2,
        std::uint32_t color,
        bool filled,
        float rounding
) {
    if (!finite(x) || !finite(y) || !finite(x2) || !finite(y2)
            || x2 < x || y2 < y || !finite(rounding) || rounding < 0.0F) {
        setError("矩形参数无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot shape;
    shape.handle = allocateHandleLocked();
    shape.kind = ImGuiShapeKind::Rectangle;
    shape.x = x;
    shape.y = y;
    shape.x2 = x2;
    shape.y2 = y2;
    shape.color = color;
    shape.filled = filled;
    shape.rounding = rounding;
    ImGuiHandle handle = shape.handle;
    gState.shapes.push_back(std::move(shape));
    markDirtyLocked();
    clearError();
    return handle;
}

ImGuiHandle imguiCreateCircle(
        float x,
        float y,
        float radius,
        std::uint32_t color,
        bool filled,
        int segments
) {
    if (!finite(x) || !finite(y) || !finite(radius) || radius <= 0.0F || segments < 3) {
        setError("圆形坐标、半径或分段数无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot shape;
    shape.handle = allocateHandleLocked();
    shape.kind = ImGuiShapeKind::Circle;
    shape.x = x;
    shape.y = y;
    shape.radius = radius;
    shape.color = color;
    shape.filled = filled;
    shape.segments = segments;
    ImGuiHandle handle = shape.handle;
    gState.shapes.push_back(std::move(shape));
    markDirtyLocked();
    clearError();
    return handle;
}

ImGuiHandle imguiCreatePolygon(
        const std::vector<std::pair<float, float>>& points,
        std::uint32_t color,
        bool closed,
        bool filled,
        float thickness
) {
    if (points.size() < (filled ? 3U : 2U) || !finite(thickness) || thickness <= 0.0F) {
        setError("多边形顶点数量或线宽无效");
        return 0;
    }
    for (const auto& point : points) {
        if (!finite(point.first) || !finite(point.second)) {
            setError("多边形包含无效坐标");
            return 0;
        }
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot shape;
    shape.handle = allocateHandleLocked();
    shape.kind = ImGuiShapeKind::Polygon;
    shape.points = points;
    shape.x = points.front().first;
    shape.y = points.front().second;
    shape.color = color;
    shape.closed = closed;
    shape.filled = filled;
    shape.thickness = thickness;
    ImGuiHandle handle = shape.handle;
    gState.shapes.push_back(std::move(shape));
    markDirtyLocked();
    clearError();
    return handle;
}

ImGuiHandle imguiCreateLine(
        float x1,
        float y1,
        float x2,
        float y2,
        std::uint32_t color,
        float thickness
) {
    if (!finite(x1) || !finite(y1) || !finite(x2) || !finite(y2)
            || !finite(thickness) || thickness <= 0.0F) {
        setError("线段参数无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot shape;
    shape.handle = allocateHandleLocked();
    shape.kind = ImGuiShapeKind::Line;
    shape.x = x1;
    shape.y = y1;
    shape.x2 = x2;
    shape.y2 = y2;
    shape.color = color;
    shape.thickness = thickness;
    ImGuiHandle handle = shape.handle;
    gState.shapes.push_back(std::move(shape));
    markDirtyLocked();
    clearError();
    return handle;
}

ImGuiHandle imguiCreateBitmapShape(
        float x,
        float y,
        float width,
        float height,
        const std::string& path
) {
    if (!finite(x) || !finite(y) || !finite(width) || !finite(height)
            || width <= 0.0F || height <= 0.0F || path.empty()) {
        setError("位图图形坐标、尺寸或路径无效");
        return 0;
    }
    std::uint64_t imageRevision;
    {
        std::lock_guard<std::mutex> lock(gState.mutex);
        imageRevision = gState.nextImageRevision++;
    }
    std::shared_ptr<const ImGuiImagePixels> image = loadImage(path, imageRevision);
    if (image == nullptr) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot shape;
    shape.handle = allocateHandleLocked();
    shape.kind = ImGuiShapeKind::Bitmap;
    shape.x = x;
    shape.y = y;
    shape.width = width;
    shape.height = height;
    shape.image = std::move(image);
    ImGuiHandle handle = shape.handle;
    gState.shapes.push_back(std::move(shape));
    markDirtyLocked();
    clearError();
    return handle;
}

ImGuiHandle imguiCreateBitmapShapeRgba(
        float x,
        float y,
        float width,
        float height,
        const unsigned char* rgba,
        int imageWidth,
        int imageHeight
) {
    if (!finite(x) || !finite(y) || !finite(width) || !finite(height)
            || width <= 0.0F || height <= 0.0F) {
        setError("位图图形坐标或显示尺寸无效");
        return 0;
    }
    std::uint64_t imageRevision;
    {
        std::lock_guard<std::mutex> lock(gState.mutex);
        imageRevision = gState.nextImageRevision++;
    }
    std::shared_ptr<const ImGuiImagePixels> image = makeImageFromRgba(
            rgba,
            imageWidth,
            imageHeight,
            imageRevision
    );
    if (image == nullptr) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot shape;
    shape.handle = allocateHandleLocked();
    shape.kind = ImGuiShapeKind::Bitmap;
    shape.x = x;
    shape.y = y;
    shape.width = width;
    shape.height = height;
    shape.image = std::move(image);
    ImGuiHandle handle = shape.handle;
    gState.shapes.push_back(std::move(shape));
    markDirtyLocked();
    clearError();
    return handle;
}

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
) {
    if (!finite(x) || !finite(y) || !finite(width) || !finite(height)
            || width <= 0.0F || height <= 0.0F
            || !finite(fontScale) || fontScale <= 0.0F) {
        setError("文本图形参数无效");
        return 0;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot shape;
    shape.handle = allocateHandleLocked();
    shape.kind = ImGuiShapeKind::Text;
    shape.x = x;
    shape.y = y;
    shape.width = width;
    shape.height = height;
    shape.text = text;
    shape.textColor = textColor;
    shape.backgroundColor = backgroundColor;
    shape.hasBackground = hasBackground;
    shape.fontScale = fontScale;
    ImGuiHandle handle = shape.handle;
    gState.shapes.push_back(std::move(shape));
    markDirtyLocked();
    clearError();
    return handle;
}

int imguiSetShapePosition(ImGuiHandle handle, float x, float y) {
    if (!finite(x) || !finite(y)) {
        setError("图形坐标无效");
        return -1;
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot* shape = findShapeLocked(handle);
    if (shape == nullptr) {
        setError("图形句柄无效");
        return -1;
    }

    const float offsetX = x - shape->x;
    const float offsetY = y - shape->y;
    if (shape->kind == ImGuiShapeKind::Rectangle || shape->kind == ImGuiShapeKind::Line) {
        shape->x2 += offsetX;
        shape->y2 += offsetY;
    } else if (shape->kind == ImGuiShapeKind::Polygon) {
        for (auto& point : shape->points) {
            point.first += offsetX;
            point.second += offsetY;
        }
    }
    shape->x = x;
    shape->y = y;
    markDirtyLocked();
    clearError();
    return 0;
}

int imguiSetShapeVisibility(ImGuiHandle handle, bool visible) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot* shape = findShapeLocked(handle);
    if (shape == nullptr) {
        setError("图形句柄无效");
        return -1;
    }
    shape->visible = visible;
    markDirtyLocked();
    clearError();
    return 0;
}

bool imguiIsShapeVisible(ImGuiHandle handle, bool* visible) {
    if (visible == nullptr) {
        return setError("图形可见状态输出地址为空");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot* shape = findShapeLocked(handle);
    if (shape == nullptr) {
        return setError("图形句柄无效");
    }
    *visible = shape->visible;
    clearError();
    return true;
}

bool imguiSetShapeText(ImGuiHandle handle, const std::string& text) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot* shape = findShapeLocked(handle);
    if (shape == nullptr || shape->kind != ImGuiShapeKind::Text) {
        return setError("文本图形句柄无效");
    }
    shape->text = text;
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetShapeTextColor(ImGuiHandle handle, std::uint32_t color) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot* shape = findShapeLocked(handle);
    if (shape == nullptr || shape->kind != ImGuiShapeKind::Text) {
        return setError("文本图形句柄无效");
    }
    shape->textColor = color;
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetShapeTextBackground(
        ImGuiHandle handle,
        std::uint32_t color,
        bool hasBackground
) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot* shape = findShapeLocked(handle);
    if (shape == nullptr || shape->kind != ImGuiShapeKind::Text) {
        return setError("文本图形句柄无效");
    }
    shape->backgroundColor = color;
    shape->hasBackground = hasBackground;
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetShapeTextFontScale(ImGuiHandle handle, float scale) {
    if (!finite(scale) || scale <= 0.0F) {
        return setError("文本缩放比例必须大于 0");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot* shape = findShapeLocked(handle);
    if (shape == nullptr || shape->kind != ImGuiShapeKind::Text) {
        return setError("文本图形句柄无效");
    }
    shape->fontScale = scale;
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetBitmapShape(ImGuiHandle handle, const std::string& path) {
    if (path.empty()) {
        return setError("位图路径不能为空");
    }
    std::uint64_t imageRevision;
    {
        std::lock_guard<std::mutex> lock(gState.mutex);
        imageRevision = gState.nextImageRevision++;
    }
    std::shared_ptr<const ImGuiImagePixels> image = loadImage(path, imageRevision);
    if (image == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot* shape = findShapeLocked(handle);
    if (shape == nullptr || shape->kind != ImGuiShapeKind::Bitmap) {
        return setError("位图图形句柄无效");
    }
    shape->image = std::move(image);
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetBitmapShapeRgba(
        ImGuiHandle handle,
        const unsigned char* rgba,
        int width,
        int height
) {
    std::uint64_t imageRevision;
    {
        std::lock_guard<std::mutex> lock(gState.mutex);
        imageRevision = gState.nextImageRevision++;
    }
    std::shared_ptr<const ImGuiImagePixels> image = makeImageFromRgba(
            rgba,
            width,
            height,
            imageRevision
    );
    if (image == nullptr) {
        return false;
    }

    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot* shape = findShapeLocked(handle);
    if (shape == nullptr || shape->kind != ImGuiShapeKind::Bitmap) {
        return setError("位图图形句柄无效");
    }
    shape->image = std::move(image);
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiSetShapeThickness(ImGuiHandle handle, float thickness) {
    if (!finite(thickness) || thickness <= 0.0F) {
        return setError("图形线宽必须大于 0");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiShapeSnapshot* shape = findShapeLocked(handle);
    if (shape == nullptr) {
        return setError("图形句柄无效");
    }
    if (shape->kind == ImGuiShapeKind::Bitmap || shape->kind == ImGuiShapeKind::Text) {
        return setError("位图和文本图形不支持设置线宽");
    }
    shape->thickness = thickness;
    markDirtyLocked();
    clearError();
    return true;
}

int imguiRemoveShape(ImGuiHandle handle) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    auto iterator = std::find_if(
            gState.shapes.begin(),
            gState.shapes.end(),
            [handle](const ImGuiShapeSnapshot& shape) { return shape.handle == handle; }
    );
    if (iterator == gState.shapes.end()) {
        setError("图形句柄无效或已经删除");
        return -1;
    }
    gState.shapes.erase(iterator);
    markDirtyLocked();
    clearError();
    return 0;
}

bool imguiIsValidHandle(ImGuiHandle handle) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    bool valid = gState.widgets.count(handle) != 0 || findShapeLocked(handle) != nullptr;
    clearError();
    return valid;
}

bool imguiPost(long long postId) {
    if (postId <= 0) {
        return setError("post 回调标识无效");
    }
    std::lock_guard<std::mutex> lock(gState.mutex);
    if (gState.closed) {
        return setError("ImGui 框架未显示或已经关闭");
    }
    ImGuiEvent event;
    event.type = ImGuiEventType::Post;
    event.handle = postId;
    queueEventLocked(std::move(event));
    clearError();
    return true;
}

bool imguiResolveWindowClose(ImGuiHandle handle, bool allowClose) {
    std::lock_guard<std::mutex> lock(gState.mutex);
    ImGuiWidgetSnapshot* window = findWidgetLocked(handle);
    if (window == nullptr || window->kind != ImGuiWidgetKind::Window) {
        return setError("窗口句柄无效");
    }
    if (allowClose) {
        eraseWidgetTreeLocked(handle);
    } else {
        window->closePending = false;
    }
    markDirtyLocked();
    clearError();
    return true;
}

bool imguiWaitEvent(
        int timeoutMs,
        ShouldStopCallback shouldStop,
        void* stopContext,
        ImGuiEvent* event
) {
    if (event == nullptr) {
        return setError("ImGui 事件输出地址为空");
    }
    if (timeoutMs < -1) {
        return setError("ImGui 事件等待时间必须大于等于 -1");
    }

    const auto deadline = timeoutMs < 0
            ? std::chrono::steady_clock::time_point::max()
            : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::mutex> lock(gState.mutex);
    while (true) {
        if (!gState.events.empty()) {
            *event = std::move(gState.events.front());
            gState.events.pop_front();
            clearError();
            return true;
        }

        lock.unlock();
        const bool stopped = shouldStop != nullptr && shouldStop(stopContext);
        lock.lock();
        if (stopped) {
            return setError("脚本已停止");
        }

        if (timeoutMs >= 0 && std::chrono::steady_clock::now() >= deadline) {
            clearError();
            return false;
        }

        std::chrono::milliseconds slice(kWaitSliceMs);
        if (timeoutMs >= 0) {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now()
            );
            if (remaining.count() <= 0) {
                continue;
            }
            slice = std::min(slice, remaining);
        }
        gState.condition.wait_for(lock, slice);
    }
}

bool imguiWaitClosed(ShouldStopCallback shouldStop, void* stopContext) {
    std::unique_lock<std::mutex> lock(gState.mutex);
    while (!gState.closed) {
        lock.unlock();
        const bool stopped = shouldStop != nullptr && shouldStop(stopContext);
        lock.lock();
        if (stopped) {
            return setError("脚本已停止");
        }
        gState.condition.wait_for(lock, std::chrono::milliseconds(kWaitSliceMs));
    }
    if (!gState.lifecycleError.empty()) {
        return setError(gState.lifecycleError);
    }
    clearError();
    return true;
}

std::shared_ptr<const ImGuiRenderSnapshot> imguiAcquireRenderSnapshot() {
    std::lock_guard<std::mutex> lock(gState.mutex);
    if (gState.cachedSnapshot != nullptr
            && gState.cachedSnapshot->revision == gState.revision) {
        return gState.cachedSnapshot;
    }

    auto snapshot = std::make_shared<ImGuiRenderSnapshot>();
    snapshot->revision = gState.revision;
    snapshot->surfaceGeneration = gState.surfaceGeneration;
    snapshot->displayed = gState.displayed;
    snapshot->colorTheme = gState.colorTheme;
    snapshot->surface = gState.surface;
    snapshot->widgets = gState.widgets;
    snapshot->windows = gState.windows;
    snapshot->rootWidgets = gState.rootWidgets;
    snapshot->shapes = gState.shapes;
    gState.cachedSnapshot = snapshot;
    return snapshot;
}

void imguiApplyInteractions(const std::vector<ImGuiInteraction>& interactions) {
    if (interactions.empty()) {
        return;
    }

    bool closeSurface = false;
    bool updateSurfaceGeometry = false;
    ImGuiSurfaceConfig surfaceConfig;
    {
        std::lock_guard<std::mutex> lock(gState.mutex);
        bool modelChanged = false;
        for (const ImGuiInteraction& interaction : interactions) {
            ImGuiWidgetSnapshot* widget = findWidgetLocked(interaction.handle);
            switch (interaction.type) {
                case ImGuiInteractionType::ButtonClick: {
                    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Button) break;
                    ImGuiEvent event;
                    event.type = ImGuiEventType::Click;
                    event.handle = interaction.handle;
                    queueEventLocked(std::move(event));
                    break;
                }
                case ImGuiInteractionType::CheckChanged: {
                    if (!widgetKindIsOneOf(widget, {
                            ImGuiWidgetKind::CheckBox,
                            ImGuiWidgetKind::Switch
                    })) break;
                    widget->checked = interaction.boolValue;
                    ImGuiEvent event;
                    event.type = ImGuiEventType::Check;
                    event.handle = interaction.handle;
                    event.boolValue = interaction.boolValue;
                    queueEventLocked(std::move(event));
                    modelChanged = true;
                    break;
                }
                case ImGuiInteractionType::SelectionChanged: {
                    if (!widgetKindIsOneOf(widget, {
                            ImGuiWidgetKind::ComboBox,
                            ImGuiWidgetKind::RadioGroup
                    })) break;
                    if (interaction.index < 0
                            || interaction.index >= static_cast<int>(widget->items.size())) break;
                    widget->selectedIndex = interaction.index;
                    ImGuiEvent event;
                    event.type = ImGuiEventType::Select;
                    event.handle = interaction.handle;
                    event.index = interaction.index;
                    event.text = widget->items[static_cast<std::size_t>(interaction.index)];
                    queueEventLocked(std::move(event));
                    modelChanged = true;
                    break;
                }
                case ImGuiInteractionType::TableCellSelected: {
                    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Table) break;
                    if (interaction.row < 0
                            || interaction.row >= static_cast<int>(widget->rows.size())
                            || interaction.column < 0
                            || interaction.column >= widget->columns) break;
                    widget->selectedRow = interaction.row;
                    widget->selectedColumn = interaction.column;
                    ImGuiEvent event;
                    event.type = ImGuiEventType::TableSelect;
                    event.handle = interaction.handle;
                    event.index = interaction.row;
                    event.row = interaction.row;
                    event.column = interaction.column;
                    event.text = widget->rows[static_cast<std::size_t>(interaction.row)]
                            [static_cast<std::size_t>(interaction.column)];
                    queueEventLocked(std::move(event));
                    modelChanged = true;
                    break;
                }
                case ImGuiInteractionType::SliderChanged: {
                    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Slider) break;
                    widget->integerValue = std::clamp(
                            interaction.integerValue,
                            widget->minimum,
                            widget->maximum
                    );
                    ImGuiEvent event;
                    event.type = ImGuiEventType::Slider;
                    event.handle = interaction.handle;
                    event.integerValue = widget->integerValue;
                    queueEventLocked(std::move(event));
                    modelChanged = true;
                    break;
                }
                case ImGuiInteractionType::InputChanged:
                    if (widget != nullptr && widget->kind == ImGuiWidgetKind::InputText) {
                        widget->text = interaction.text;
                        modelChanged = true;
                    }
                    break;
                case ImGuiInteractionType::ColorChanged:
                    if (widget != nullptr && widget->kind == ImGuiWidgetKind::ColorPicker) {
                        widget->color = interaction.color;
                        modelChanged = true;
                    }
                    break;
                case ImGuiInteractionType::WindowCloseRequested: {
                    if (widget == nullptr || widget->kind != ImGuiWidgetKind::Window
                            || widget->closePending) break;
                    widget->closePending = true;
                    ImGuiEvent event;
                    event.type = ImGuiEventType::WindowClose;
                    event.handle = interaction.handle;
                    queueEventLocked(std::move(event));
                    modelChanged = true;
                    break;
                }
                case ImGuiInteractionType::WindowGeometryChanged:
                    if (widget != nullptr && widget->kind == ImGuiWidgetKind::Window) {
                        widget->x = interaction.x;
                        widget->y = interaction.y;
                        widget->width = interaction.width;
                        widget->height = interaction.height;
                        modelChanged = true;
                    }
                    break;
                case ImGuiInteractionType::SurfaceGeometryChanged:
                    if (gState.surface.windowed) {
                        gState.surface.x = static_cast<int>(std::round(interaction.x));
                        gState.surface.y = static_cast<int>(std::round(interaction.y));
                        gState.surface.width = std::max(
                                1,
                                static_cast<int>(std::round(interaction.width))
                        );
                        gState.surface.height = std::max(
                                1,
                                static_cast<int>(std::round(interaction.height))
                        );
                        surfaceConfig = gState.surface;
                        updateSurfaceGeometry = true;
                        modelChanged = true;
                    }
                    break;
                case ImGuiInteractionType::SurfaceCloseRequested:
                    closeSurface = true;
                    break;
            }
        }
        if (modelChanged) {
            markDirtyLocked();
        }
    }

    // WindowManager 调用不能持有模型锁；Java 侧同步触发 surfaceChanged 时会再次进入 native。
    if (updateSurfaceGeometry) {
        AndroidBridge::updateScriptImGui(surfaceConfigJson(surfaceConfig));
    }
    if (closeSurface) {
        imguiClose();
    }
}

std::string imguiLastError() {
    return gLastError;
}

} // namespace xiaoyv::api
