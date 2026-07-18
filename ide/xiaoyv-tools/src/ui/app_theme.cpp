/**
 * 文件用途：实现六套中性皮肤，并从同一组令牌生成 QPalette 和紧凑 QSS。
 */
#include "ui/app_theme.h"

#include <QApplication>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <dwmapi.h>
#include <windows.h>
#endif

namespace xiaoyv::tools {
namespace {

const QList<ThemePalette> kThemes = {
        {AppTheme::Dark, "dark", QString::fromUtf8("中性暗色"), true,
         "#181818", "#1F1F1F", "#252526", "#D4D4D4", "#9D9D9D", "#3C3C3C",
         "#202020", "#2A2D2E", "#094771", "#FFFFFF", "#6D6D6D", "#242424",
         "#202020", "#4EC9B0", "#F14C4C"},
        {AppTheme::Light, "light", QString::fromUtf8("清爽亮色"), false,
         "#F3F3F3", "#FFFFFF", "#F7F7F7", "#202124", "#666A70", "#D0D0D0",
         "#FFFFFF", "#E8E8E8", "#CDE8FF", "#111111", "#A0A0A0", "#EEEEEE",
         "#E7E7E7", "#16825D", "#C42B1C"},
        {AppTheme::Midnight, "midnight", QString::fromUtf8("深夜蓝灰"), true,
         "#16191F", "#1D222B", "#252C37", "#D8DEE9", "#8F9BAD", "#3A4555",
         "#202630", "#2B3441", "#285F87", "#FFFFFF", "#697586", "#242B35",
         "#202630", "#60C6A8", "#FF6B6B"},
        {AppTheme::Carbon, "carbon", QString::fromUtf8("碳黑暖灰"), true,
         "#1B1A19", "#23211F", "#2B2926", "#E3DED8", "#A59E96", "#48433E",
         "#262320", "#332F2B", "#6D5030", "#FFFFFF", "#746C64", "#292622",
         "#24211E", "#70C1A4", "#F07167"},
        {AppTheme::SoftLight, "soft-light", QString::fromUtf8("柔和浅色"), false,
         "#EEF1F2", "#FAFBFB", "#F3F5F5", "#243034", "#69777B", "#CAD1D3",
         "#FFFFFF", "#E2E8EA", "#BFDCE5", "#172126", "#9AA4A7", "#E7EBEC",
         "#E5EAEB", "#197A66", "#B83A3A"},
        {AppTheme::HighContrast, "high-contrast", QString::fromUtf8("高对比"), true,
         "#000000", "#080808", "#111111", "#FFFFFF", "#CCCCCC", "#FFFFFF",
         "#000000", "#222222", "#004C99", "#FFFFFF", "#888888", "#111111",
         "#000000", "#00FFB3", "#FF4D4D"},
};

QString themeStyleSheet(const ThemePalette& p) {
    return QStringLiteral(R"(
QWidget { color:%1; background:%2; selection-background-color:%3; selection-color:%4; }
QMainWindow, QDialog { background:%5; }
QMainWindow::separator { background:%6; width:3px; height:3px; }
QMainWindow::separator:hover { background:%3; }
QMenuBar, QMenu, QToolBar, QStatusBar { background:%5; border-color:%6; }
QMenuBar::item, QMenu::item { background:transparent; }
QMenuBar::item:selected, QMenu::item:selected { background:%7; }
QToolBar { spacing:1px; padding:1px; border-bottom:1px solid %6; }
QToolButton, QStatusBar QLabel { background:transparent; }
QToolButton { border:0; padding:0; }
QToolButton:hover { background:%7; }
QToolButton:checked { background:%3; color:%4; }
QPushButton { background:%8; border:1px solid %6; padding:1px 5px; min-height:18px; }
QPushButton:hover { background:%7; }
QPushButton:pressed { background:%3; color:%4; }
QLineEdit, QPlainTextEdit, QListWidget, QTableView, QTableWidget, QComboBox, QSpinBox {
    background:%9; border:1px solid %6; padding:2px; color:%1;
}
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QSlider:disabled {
    color:%10; background:%11; border-color:%10;
}
QHeaderView::section { background:%8; border:0; border-right:1px solid %6; border-bottom:1px solid %6; padding:2px 4px; }
QTableView::item, QTableWidget::item { padding:1px 3px; }
QTabBar::tab { background:%8; border:1px solid %6; height:16px; padding:2px 7px; }
QTabBar::close-button { width:12px; height:12px; }
QTabBar::tab:selected { background:%3; color:%4; font-weight:600; }
QDockWidget { border:1px solid %6; }
QScrollBar:vertical { background:%5; width:11px; }
QScrollBar:horizontal { background:%5; height:11px; }
QScrollBar::handle { background:%6; min-width:24px; min-height:24px; }
QScrollBar::add-line, QScrollBar::sub-line { width:0; height:0; }
QToolTip { background:%9; color:%1; border:1px solid %6; padding:3px; }
)")
            .arg(p.text, p.panel, p.selected, p.selectedText, p.window, p.border,
                 p.hover, p.alternate, p.input, p.disabled, p.disabledInput);
}

} // namespace

const ThemePalette& themePalette(AppTheme theme) {
    for (const ThemePalette& palette : kThemes) if (palette.id == theme) return palette;
    return kThemes.front();
}

QList<AppTheme> allThemes() {
    QList<AppTheme> result;
    for (const ThemePalette& palette : kThemes) result.push_back(palette.id);
    return result;
}

QString themeDisplayName(AppTheme theme) {
    return themePalette(theme).displayName;
}

bool isDarkTheme(AppTheme theme) {
    return themePalette(theme).dark;
}

AppTheme loadTheme() {
    const QString key = QSettings().value(QStringLiteral("appearance/theme"), QStringLiteral("dark")).toString();
    for (const ThemePalette& palette : kThemes) if (palette.key == key) return palette.id;
    return AppTheme::Dark;
}

void saveTheme(AppTheme theme) {
    QSettings().setValue(QStringLiteral("appearance/theme"), themePalette(theme).key);
}

void applyApplicationTheme(QApplication* application, AppTheme theme) {
    if (application == nullptr) return;
    const ThemePalette& p = themePalette(theme);
    application->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette palette;
    palette.setColor(QPalette::Window, p.window);
    palette.setColor(QPalette::WindowText, p.text);
    palette.setColor(QPalette::Base, p.input);
    palette.setColor(QPalette::AlternateBase, p.alternate);
    palette.setColor(QPalette::Text, p.text);
    palette.setColor(QPalette::Button, p.alternate);
    palette.setColor(QPalette::ButtonText, p.text);
    palette.setColor(QPalette::Highlight, p.selected);
    palette.setColor(QPalette::HighlightedText, p.selectedText);
    palette.setColor(QPalette::Disabled, QPalette::Text, p.disabled);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, p.disabled);
    application->setPalette(palette);
    application->setStyleSheet(themeStyleSheet(p));
}

void applyWidgetTheme(QWidget* root, AppTheme theme) {
    if (root == nullptr) return;
    root->setProperty("xiaoyvTheme", themePalette(theme).key);
    root->style()->unpolish(root);
    root->style()->polish(root);
    root->update();
}

void applyNativeTitleBar(QWidget* window, bool dark) {
#ifdef Q_OS_WIN
    if (window == nullptr) return;
    const BOOL enabled = dark ? TRUE : FALSE;
    const HWND handle = reinterpret_cast<HWND>(window->winId());
    constexpr DWORD kImmersiveDarkMode = 20;
    if (FAILED(DwmSetWindowAttribute(
            handle, kImmersiveDarkMode, &enabled, sizeof(enabled)))) {
        constexpr DWORD kLegacyImmersiveDarkMode = 19;
        DwmSetWindowAttribute(handle, kLegacyImmersiveDarkMode, &enabled, sizeof(enabled));
    }
#else
    Q_UNUSED(window)
    Q_UNUSED(dark)
#endif
}

} // namespace xiaoyv::tools
