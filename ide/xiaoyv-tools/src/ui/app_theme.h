/**
 * 文件用途：声明抓图取色器皮肤令牌、全局样式和持久化接口。
 */
#pragma once

#include <QList>
#include <QPalette>
#include <QString>

class QApplication;
class QWidget;

namespace xiaoyv::tools {

enum class AppTheme {
    Dark,
    Light,
    Midnight,
    Carbon,
    SoftLight,
    HighContrast,
};

struct ThemePalette {
    AppTheme id;
    QString key;
    QString displayName;
    bool dark;
    QString window;
    QString panel;
    QString alternate;
    QString text;
    QString muted;
    QString border;
    QString input;
    QString hover;
    QString selected;
    QString selectedText;
    QString disabled;
    QString disabledInput;
    QString titleBar;
    QString success;
    QString danger;
};

const ThemePalette& themePalette(AppTheme theme);
QList<AppTheme> allThemes();
QString themeDisplayName(AppTheme theme);
bool isDarkTheme(AppTheme theme);
AppTheme loadTheme();
void saveTheme(AppTheme theme);
void applyApplicationTheme(QApplication* application, AppTheme theme);
void applyWidgetTheme(QWidget* root, AppTheme theme);
/** Windows 下同步原生标题栏明暗；其他平台为空操作。 */
void applyNativeTitleBar(QWidget* window, bool dark);

} // namespace xiaoyv::tools
