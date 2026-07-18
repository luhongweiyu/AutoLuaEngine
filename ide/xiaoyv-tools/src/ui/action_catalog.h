/**
 * 文件用途：定义菜单和主工具栏动作的唯一目录，文字、顺序、图标和分组只维护一份。
 */
#pragma once

#include "ui/tool_icons.h"

#include <array>

namespace xiaoyv::tools {

enum class MenuId {
    File,
    Edit,
    Image,
    Device,
    Tools,
};

enum class ActionId {
    Open,
    Save,
    SaveAs,
    Exit,
    Undo,
    Redo,
    DeviceScreenshot,
    FloatingCapture,
    WindowCapture,
    Projection,
    RotateLeft,
    RotateRight,
    FlipHorizontal,
    FlipVertical,
    Crop,
    ZoomIn,
    ZoomOut,
    Fit,
    ActualSize,
    CheckConnection,
    ConnectionSettings,
    ReloadFormats,
    OpenFormatsDirectory,
    Count,
};

struct ActionSpec {
    ActionId id;
    MenuId menu;
    const char* text;
    const char* objectName;
    const char* shortcut;
    ToolIcon icon;
    bool hasIcon;
    bool toolbar;
    int group;
};

inline constexpr std::array<ActionSpec, static_cast<int>(ActionId::Count)> kActionCatalog = {{
        {ActionId::Open, MenuId::File, "打开图片", "openAction", "Ctrl+O", ToolIcon::Open, true, true, 0},
        {ActionId::Save, MenuId::File, "保存", "saveAction", "Ctrl+S", ToolIcon::Save, true, true, 0},
        {ActionId::SaveAs, MenuId::File, "另存为", "saveAsAction", "Ctrl+Shift+S", ToolIcon::Save, false, false, 0},
        {ActionId::Exit, MenuId::File, "退出", "exitAction", "Alt+F4", ToolIcon::Stop, false, false, 1},
        {ActionId::Undo, MenuId::Edit, "撤销", "undoAction", "Ctrl+Z", ToolIcon::Undo, true, true, 1},
        {ActionId::Redo, MenuId::Edit, "重做", "redoAction", "Ctrl+Y", ToolIcon::Redo, true, true, 1},
        {ActionId::DeviceScreenshot, MenuId::Device, "设备截图", "deviceScreenshotAction", "", ToolIcon::DeviceScreenshot, true, true, 2},
        {ActionId::FloatingCapture, MenuId::Device, "浮动抓图", "floatingCaptureAction", "", ToolIcon::FloatingScreenshot, true, true, 2},
        {ActionId::WindowCapture, MenuId::Device, "窗口截图", "windowCaptureAction", "", ToolIcon::FloatingScreenshot, false, true, 2},
        {ActionId::Projection, MenuId::Device, "投影当前图片", "projectionAction", "", ToolIcon::Projection, true, true, 2},
        {ActionId::RotateLeft, MenuId::Image, "向左旋转", "rotateLeftAction", "", ToolIcon::RotateLeft, true, true, 3},
        {ActionId::RotateRight, MenuId::Image, "向右旋转", "rotateRightAction", "", ToolIcon::RotateRight, true, true, 3},
        {ActionId::FlipHorizontal, MenuId::Image, "水平翻转", "flipHorizontalAction", "", ToolIcon::FlipHorizontal, true, true, 3},
        {ActionId::FlipVertical, MenuId::Image, "上下翻转", "flipVerticalAction", "", ToolIcon::FlipVertical, true, true, 3},
        {ActionId::Crop, MenuId::Image, "按框选范围裁剪", "cropAction", "", ToolIcon::Crop, true, true, 3},
        {ActionId::ZoomIn, MenuId::Image, "放大", "zoomInAction", "Ctrl++", ToolIcon::ZoomIn, true, true, 4},
        {ActionId::ZoomOut, MenuId::Image, "缩小", "zoomOutAction", "Ctrl+-", ToolIcon::ZoomOut, true, true, 4},
        {ActionId::Fit, MenuId::Image, "适应窗口", "fitAction", "Ctrl+0", ToolIcon::Fit, true, true, 4},
        {ActionId::ActualSize, MenuId::Image, "实际大小 1:1", "actualAction", "Ctrl+1", ToolIcon::ActualSize, true, true, 4},
        {ActionId::CheckConnection, MenuId::Device, "刷新连接状态", "checkConnectionAction", "", ToolIcon::Settings, false, false, 5},
        {ActionId::ConnectionSettings, MenuId::Device, "连接设置", "connectionSettingsAction", "", ToolIcon::Settings, true, true, 5},
        {ActionId::ReloadFormats, MenuId::Tools, "重新加载生成格式", "reloadFormatsAction", "", ToolIcon::Settings, false, false, 0},
        {ActionId::OpenFormatsDirectory, MenuId::Tools, "打开格式目录", "openFormatsDirectoryAction", "", ToolIcon::Open, false, false, 0},
}};

inline int actionIndex(ActionId id) {
    return static_cast<int>(id);
}

} // namespace xiaoyv::tools
