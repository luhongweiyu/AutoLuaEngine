/**
 * 文件用途：声明 Dear ImGui 稳定 C ABI，供 Lua、后续 JS/Go 和外部插件统一调用。
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long long EngineImGuiHandle;
typedef int (*engine_imgui_interrupt_callback)(void* userData);

/** show/showWindow 的语言无关 Surface 配置。字符串只在调用期间读取。 */
typedef struct EngineImGuiSurfaceConfig {
    int windowed;
    int touchable;
    int x;
    int y;
    int width;
    int height;
    int hasTitle;
    const char* title;
    uint32_t titleColor;
    uint32_t titleBackgroundColor;
    int hasClose;
    uint32_t closeColor;
    int hasResize;
    uint32_t resizeColor;
    int hasToggle;
    uint32_t toggleColor;
    float titleFontSize;
    const char* fontPath;
    float fontSize;
} EngineImGuiSurfaceConfig;

typedef struct EngineImGuiGeometry {
    float x;
    float y;
    float width;
    float height;
} EngineImGuiGeometry;

typedef struct EngineImGuiPointF {
    float x;
    float y;
} EngineImGuiPointF;

enum EngineImGuiEventType {
    ENGINE_IMGUI_EVENT_NONE = 0,
    ENGINE_IMGUI_EVENT_CLICK = 1,
    ENGINE_IMGUI_EVENT_CHECK = 2,
    ENGINE_IMGUI_EVENT_SELECT = 3,
    ENGINE_IMGUI_EVENT_TABLE_SELECT = 4,
    ENGINE_IMGUI_EVENT_SLIDER = 5,
    ENGINE_IMGUI_EVENT_WINDOW_CLOSE = 6,
    ENGINE_IMGUI_EVENT_POST = 7,
    ENGINE_IMGUI_EVENT_FRAMEWORK_CLOSED = 8
};

/**
 * 渲染事件的稳定 C 表示。
 *
 * text 指针由当前调用线程持有，只读且无需释放；下一次 engine_imguiWaitEvent 调用会覆盖。
 */
typedef struct EngineImGuiEvent {
    int type;
    EngineImGuiHandle handle;
    int index;
    int row;
    int column;
    int integerValue;
    int boolValue;
    const char* text;
} EngineImGuiEvent;

/**
 * ImGui 子函数表。
 *
 * 回调函数对象属于具体语言运行时，不能跨 ABI 传递；各绑定通过 waitEvent 消费统一事件，
 * 再调用自己的函数对象。其余控件、绘图、图片、窗口和状态逻辑全部位于 libengine.so。
 */
typedef struct EngineImGuiApi {
    int abiVersion;
    int (*isSupport)();
    int (*show)(const EngineImGuiSurfaceConfig* config);
    void (*close)();
    void (*reset)();
    int (*setColorTheme)(int style);
    EngineImGuiHandle (*createWindow)(const char*, float, float, float, float, int);
    int (*destroyWindow)(EngineImGuiHandle);
    EngineImGuiHandle (*createVerticalLayout)(EngineImGuiHandle, float, float);
    EngineImGuiHandle (*createHorticalLayout)(EngineImGuiHandle, float, float);
    EngineImGuiHandle (*createTreeBoxLayout)(EngineImGuiHandle, const char*, float);
    EngineImGuiHandle (*createTabBar)(EngineImGuiHandle, const char*);
    EngineImGuiHandle (*addTabBarItem)(EngineImGuiHandle, const char*);
    int (*sameLine)(EngineImGuiHandle, float);
    int (*setLayoutBorderVisible)(EngineImGuiHandle, int);
    EngineImGuiHandle (*createButton)(EngineImGuiHandle, const char*, float, float, float, float);
    EngineImGuiHandle (*createLabel)(EngineImGuiHandle, const char*, int);
    EngineImGuiHandle (*createCheckBox)(EngineImGuiHandle, const char*, int);
    EngineImGuiHandle (*createSwitch)(EngineImGuiHandle, const char*, int, float);
    EngineImGuiHandle (*createInputText)(EngineImGuiHandle, const char*, const char*, int, float, float);
    EngineImGuiHandle (*createProgressBar)(EngineImGuiHandle, float, float, float);
    EngineImGuiHandle (*createSlider)(EngineImGuiHandle, const char*, int, int, int, float);
    EngineImGuiHandle (*createColorPicker)(EngineImGuiHandle, const char*, uint32_t, float, float);
    EngineImGuiHandle (*createComboBox)(EngineImGuiHandle, const char* const*, int, float);
    EngineImGuiHandle (*createRadioGroup)(EngineImGuiHandle, const char*);
    int (*addOptionItem)(EngineImGuiHandle, const char*);
    int (*addRadioBox)(EngineImGuiHandle, const char*, int);
    const char* (*getItemText)(EngineImGuiHandle, int);
    int (*removeItemAt)(EngineImGuiHandle, int);
    int (*removeAllItems)(EngineImGuiHandle);
    int (*getSelectedItemIndex)(EngineImGuiHandle);
    int (*setItemSelected)(EngineImGuiHandle, int);
    int (*getItemCount)(EngineImGuiHandle);
    EngineImGuiHandle (*createTableView)(EngineImGuiHandle, const char*, int, int, float, float);
    int (*setTableHeaderItem)(EngineImGuiHandle, int, const char*);
    int (*insertTableRow)(EngineImGuiHandle, int);
    const char* (*getTableItemText)(EngineImGuiHandle, int, int);
    int (*setTableItemText)(EngineImGuiHandle, int, int, const char*);
    int (*deleteTableRow)(EngineImGuiHandle, int);
    int (*clearTable)(EngineImGuiHandle);
    int (*setChecked)(EngineImGuiHandle, int);
    int (*isChecked)(EngineImGuiHandle, int*);
    const char* (*getInputText)(EngineImGuiHandle);
    int (*setInputText)(EngineImGuiHandle, const char*);
    int (*setInputType)(EngineImGuiHandle, int);
    int (*setProgressBarPos)(EngineImGuiHandle, float);
    int (*getProgressBarPos)(EngineImGuiHandle, float*);
    int (*setSlider)(EngineImGuiHandle, int);
    int (*getSliderPos)(EngineImGuiHandle, int*);
    int (*setWidgetSize)(EngineImGuiHandle, float, float);
    int (*setWidgetVisible)(EngineImGuiHandle, int);
    int (*isWidgetVisible)(EngineImGuiHandle, int*);
    int (*setWidgetStyle)(EngineImGuiHandle, int, float, float);
    int (*setWidgetColor)(EngineImGuiHandle, int, uint32_t);
    EngineImGuiHandle (*createImage)(EngineImGuiHandle, const char*, float, float);
    int (*setImage)(EngineImGuiHandle, const char*);
    int (*setImageRgba)(EngineImGuiHandle, const unsigned char*, int, int);
    int (*setWindowPos)(EngineImGuiHandle, float, float);
    int (*setWindowSize)(EngineImGuiHandle, float, float);
    int (*getWindowPos)(EngineImGuiHandle, EngineImGuiGeometry*);
    int (*setWindowFlags)(EngineImGuiHandle, int);
    EngineImGuiHandle (*createRectangle)(float, float, float, float, uint32_t, int, float);
    EngineImGuiHandle (*createCircle)(float, float, float, uint32_t, int, int);
    EngineImGuiHandle (*createPolygon)(const EngineImGuiPointF*, int, uint32_t, int, int, float);
    EngineImGuiHandle (*createLine)(float, float, float, float, uint32_t, float);
    EngineImGuiHandle (*createBitmapShape)(float, float, float, float, const char*);
    EngineImGuiHandle (*createBitmapShapeRgba)(float, float, float, float, const unsigned char*, int, int);
    EngineImGuiHandle (*createShapeText)(float, float, float, float, const char*, uint32_t, uint32_t, int, float);
    int (*setShapePosition)(EngineImGuiHandle, float, float);
    int (*setShapeVisibility)(EngineImGuiHandle, int);
    int (*isShapeVisibility)(EngineImGuiHandle, int*);
    int (*setShapeTextString)(EngineImGuiHandle, const char*);
    int (*setShapeTextColor)(EngineImGuiHandle, uint32_t);
    int (*setShapeTextBackground)(EngineImGuiHandle, uint32_t, int);
    int (*setShapeTextFontScale)(EngineImGuiHandle, float);
    int (*setBitmapShape)(EngineImGuiHandle, const char*);
    int (*setBitmapShapeRgba)(EngineImGuiHandle, const unsigned char*, int, int);
    int (*setShapeThickness)(EngineImGuiHandle, float);
    int (*removeShape)(EngineImGuiHandle);
    int (*isValidHandle)(EngineImGuiHandle);
    int (*post)(long long postId);
    int (*resolveWindowClose)(EngineImGuiHandle, int allowClose);
    int (*waitEvent)(EngineImGuiEvent*, int, engine_imgui_interrupt_callback, void*);
    int (*waitClosed)(engine_imgui_interrupt_callback, void*);
    const char* (*lastError)();
} EngineImGuiApi;

const EngineImGuiApi* engine_getImGuiApi();
int engine_imguiIsSupport();
int engine_imguiShow(const EngineImGuiSurfaceConfig* config);
void engine_imguiClose();
void engine_imguiReset();
int engine_imguiSetColorTheme(int style);
EngineImGuiHandle engine_imguiCreateWindow(const char*, float, float, float, float, int);
int engine_imguiDestroyWindow(EngineImGuiHandle);
EngineImGuiHandle engine_imguiCreateVerticalLayout(EngineImGuiHandle, float, float);
EngineImGuiHandle engine_imguiCreateHorticalLayout(EngineImGuiHandle, float, float);
EngineImGuiHandle engine_imguiCreateTreeBoxLayout(EngineImGuiHandle, const char*, float);
EngineImGuiHandle engine_imguiCreateTabBar(EngineImGuiHandle, const char*);
EngineImGuiHandle engine_imguiAddTabBarItem(EngineImGuiHandle, const char*);
int engine_imguiSameLine(EngineImGuiHandle, float);
int engine_imguiSetLayoutBorderVisible(EngineImGuiHandle, int);
EngineImGuiHandle engine_imguiCreateButton(EngineImGuiHandle, const char*, float, float, float, float);
EngineImGuiHandle engine_imguiCreateLabel(EngineImGuiHandle, const char*, int);
EngineImGuiHandle engine_imguiCreateCheckBox(EngineImGuiHandle, const char*, int);
EngineImGuiHandle engine_imguiCreateSwitch(EngineImGuiHandle, const char*, int, float);
EngineImGuiHandle engine_imguiCreateInputText(EngineImGuiHandle, const char*, const char*, int, float, float);
EngineImGuiHandle engine_imguiCreateProgressBar(EngineImGuiHandle, float, float, float);
EngineImGuiHandle engine_imguiCreateSlider(EngineImGuiHandle, const char*, int, int, int, float);
EngineImGuiHandle engine_imguiCreateColorPicker(EngineImGuiHandle, const char*, uint32_t, float, float);
EngineImGuiHandle engine_imguiCreateComboBox(EngineImGuiHandle, const char* const*, int, float);
EngineImGuiHandle engine_imguiCreateRadioGroup(EngineImGuiHandle, const char*);
int engine_imguiAddOptionItem(EngineImGuiHandle, const char*);
int engine_imguiAddRadioBox(EngineImGuiHandle, const char*, int);
const char* engine_imguiGetItemText(EngineImGuiHandle, int);
int engine_imguiRemoveItemAt(EngineImGuiHandle, int);
int engine_imguiRemoveAllItems(EngineImGuiHandle);
int engine_imguiGetSelectedItemIndex(EngineImGuiHandle);
int engine_imguiSetItemSelected(EngineImGuiHandle, int);
int engine_imguiGetItemCount(EngineImGuiHandle);
EngineImGuiHandle engine_imguiCreateTableView(EngineImGuiHandle, const char*, int, int, float, float);
int engine_imguiSetTableHeaderItem(EngineImGuiHandle, int, const char*);
int engine_imguiInsertTableRow(EngineImGuiHandle, int);
const char* engine_imguiGetTableItemText(EngineImGuiHandle, int, int);
int engine_imguiSetTableItemText(EngineImGuiHandle, int, int, const char*);
int engine_imguiDeleteTableRow(EngineImGuiHandle, int);
int engine_imguiClearTable(EngineImGuiHandle);
int engine_imguiSetChecked(EngineImGuiHandle, int);
int engine_imguiIsChecked(EngineImGuiHandle, int*);
const char* engine_imguiGetInputText(EngineImGuiHandle);
int engine_imguiSetInputText(EngineImGuiHandle, const char*);
int engine_imguiSetInputType(EngineImGuiHandle, int);
int engine_imguiSetProgressBarPos(EngineImGuiHandle, float);
int engine_imguiGetProgressBarPos(EngineImGuiHandle, float*);
int engine_imguiSetSlider(EngineImGuiHandle, int);
int engine_imguiGetSliderPos(EngineImGuiHandle, int*);
int engine_imguiSetWidgetSize(EngineImGuiHandle, float, float);
int engine_imguiSetWidgetVisible(EngineImGuiHandle, int);
int engine_imguiIsWidgetVisible(EngineImGuiHandle, int*);
int engine_imguiSetWidgetStyle(EngineImGuiHandle, int, float, float);
int engine_imguiSetWidgetColor(EngineImGuiHandle, int, uint32_t);
EngineImGuiHandle engine_imguiCreateImage(EngineImGuiHandle, const char*, float, float);
int engine_imguiSetImage(EngineImGuiHandle, const char*);
int engine_imguiSetImageRgba(EngineImGuiHandle, const unsigned char*, int, int);
int engine_imguiSetWindowPos(EngineImGuiHandle, float, float);
int engine_imguiSetWindowSize(EngineImGuiHandle, float, float);
int engine_imguiGetWindowPos(EngineImGuiHandle, EngineImGuiGeometry*);
int engine_imguiSetWindowFlags(EngineImGuiHandle, int);
EngineImGuiHandle engine_imguiCreateRectangle(float, float, float, float, uint32_t, int, float);
EngineImGuiHandle engine_imguiCreateCircle(float, float, float, uint32_t, int, int);
EngineImGuiHandle engine_imguiCreatePolygon(const EngineImGuiPointF*, int, uint32_t, int, int, float);
EngineImGuiHandle engine_imguiCreateLine(float, float, float, float, uint32_t, float);
EngineImGuiHandle engine_imguiCreateBitmapShape(float, float, float, float, const char*);
EngineImGuiHandle engine_imguiCreateBitmapShapeRgba(float, float, float, float, const unsigned char*, int, int);
EngineImGuiHandle engine_imguiCreateShapeText(float, float, float, float, const char*, uint32_t, uint32_t, int, float);
int engine_imguiSetShapePosition(EngineImGuiHandle, float, float);
int engine_imguiSetShapeVisibility(EngineImGuiHandle, int);
int engine_imguiIsShapeVisibility(EngineImGuiHandle, int*);
int engine_imguiSetShapeTextString(EngineImGuiHandle, const char*);
int engine_imguiSetShapeTextColor(EngineImGuiHandle, uint32_t);
int engine_imguiSetShapeTextBackground(EngineImGuiHandle, uint32_t, int);
int engine_imguiSetShapeTextFontScale(EngineImGuiHandle, float);
int engine_imguiSetBitmapShape(EngineImGuiHandle, const char*);
int engine_imguiSetBitmapShapeRgba(EngineImGuiHandle, const unsigned char*, int, int);
int engine_imguiSetShapeThickness(EngineImGuiHandle, float);
int engine_imguiRemoveShape(EngineImGuiHandle);
int engine_imguiIsValidHandle(EngineImGuiHandle);
int engine_imguiPost(long long postId);
int engine_imguiResolveWindowClose(EngineImGuiHandle, int allowClose);
int engine_imguiWaitEvent(EngineImGuiEvent*, int, engine_imgui_interrupt_callback, void*);
int engine_imguiWaitClosed(engine_imgui_interrupt_callback, void*);
const char* engine_imguiLastError();

#ifdef __cplusplus
}
#endif
