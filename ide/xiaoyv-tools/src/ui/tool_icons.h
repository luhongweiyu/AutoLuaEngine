/**
 * 文件用途：声明主工具栏的高 DPI 线性图标；亮暗主题分别使用清晰的前景色。
 */
#pragma once

#include <QIcon>

namespace xiaoyv::tools {

enum class ToolIcon {
    Open,
    Save,
    Undo,
    Redo,
    DeviceScreenshot,
    FloatingScreenshot,
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
    Settings,
    Stop,
};

/** 与 Android 端共用造型和主色的小鱼应用 Logo。 */
QIcon makeApplicationLogoIcon();
QIcon makeToolIcon(ToolIcon icon, bool darkTheme);

} // namespace xiaoyv::tools
