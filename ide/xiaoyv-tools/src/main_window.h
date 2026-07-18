/**
 * 文件用途：声明应用外壳，只装配菜单、工作区、工具面板、设备服务和应用级状态。
 */
#pragma once

#include <QMainWindow>

#include <array>

#include "device/device_client.h"
#include "generator/generator_engine.h"
#include "ui/action_catalog.h"
#include "ui/app_theme.h"

class QAction;
class QActionGroup;
class QLabel;
class QPlainTextEdit;
class QTabWidget;

namespace xiaoyv::tools {

class AnalysisPanel;
class ColorPanel;
class CompactDockWidget;
class FloatingCaptureWindow;
class FontPanel;
class ImageWorkspace;
class ScriptEditorPanel;
class WindowCaptureButton;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QStringList& arguments = {}, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createWorkspace();
    void createPanels();
    void createStatusBarWidgets();
    void connectWorkspaceSignals();
    void connectDeviceSignals();
    void connectActions();

    QAction* action(ActionId id) const;
    void updateActionStates();
    void updateConnectionStatus();
    void updateImageStatus();
    void updateInspectorState();
    void showFloatingCapture();
    void showConnectionDialog();
    void showCode(const QString& language, const QString& code);
    void showStatus(const QString& message);
    void setTheme(AppTheme theme);
    void refreshIcons();
    void restoreWindowState();
    void saveWindowState() const;

    std::array<QAction*, static_cast<int>(ActionId::Count)> actions_{};
    QActionGroup* themeGroup_ = nullptr;
    DeviceClient deviceClient_;
    GeneratorEngine generator_;
    AppTheme theme_ = AppTheme::Dark;
    ImageWorkspace* workspace_ = nullptr;
    QTabWidget* inspectorTabs_ = nullptr;
    CompactDockWidget* inspectorDock_ = nullptr;
    int analysisTabIndex_ = -1;
    int fontTabIndex_ = -1;
    ColorPanel* colorPanel_ = nullptr;
    AnalysisPanel* analysisPanel_ = nullptr;
    FontPanel* fontPanel_ = nullptr;
    ScriptEditorPanel* scriptEditor_ = nullptr;
    QPlainTextEdit* resultOutput_ = nullptr;
    QLabel* connectionStatus_ = nullptr;
    QLabel* imageStatus_ = nullptr;
    QLabel* cursorStatus_ = nullptr;
    QLabel* zoomStatus_ = nullptr;
    WindowCaptureButton* windowCaptureButton_ = nullptr;
    FloatingCaptureWindow* floatingCapture_ = nullptr;
};

} // namespace xiaoyv::tools
