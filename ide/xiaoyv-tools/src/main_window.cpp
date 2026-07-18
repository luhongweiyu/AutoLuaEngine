/**
 * 文件用途：实现应用外壳；图片、字库和设备规则分别委托给对应模块。
 */
#include "main_window.h"

#include "capture/desktop_capture.h"
#include "dialogs/connection_dialog.h"
#include "model/color_point_model.h"
#include "model/image_document.h"
#include "panels/analysis_panel.h"
#include "panels/color_panel.h"
#include "panels/font_panel.h"
#include "panels/script_editor_panel.h"
#include "ui/compact_dock_widget.h"
#include "ui/instant_tooltip_filter.h"
#include "ui/tool_icons.h"
#include "workspace/image_workspace.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDockWidget>
#include <QFontMetrics>
#include <QLabel>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>

namespace xiaoyv::tools {
namespace {

QWidget* scrollable(QWidget* content) {
    auto* area = new QScrollArea();
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(content);
    return area;
}

CompactDockWidget* makeDock(
        QMainWindow* owner,
        const QString& title,
        const QString& objectName,
        QWidget* content,
        Qt::DockWidgetAreas areas) {
    auto* dock = new CompactDockWidget(title, owner);
    dock->setObjectName(objectName);
    dock->setAllowedAreas(areas);
    dock->setWidget(content);
    return dock;
}

QMenu* menuFor(MenuId id, QMenuBar* bar) {
    switch (id) {
        case MenuId::File: return bar->findChild<QMenu*>(QStringLiteral("fileMenu"));
        case MenuId::Edit: return bar->findChild<QMenu*>(QStringLiteral("editMenu"));
        case MenuId::Image: return bar->findChild<QMenu*>(QStringLiteral("imageMenu"));
        case MenuId::Device: return bar->findChild<QMenu*>(QStringLiteral("deviceMenu"));
        case MenuId::Tools: return bar->findChild<QMenu*>(QStringLiteral("toolsMenu"));
    }
    return nullptr;
}

} // namespace

MainWindow::MainWindow(const QStringList& arguments, QWidget* parent)
        : QMainWindow(parent), deviceClient_(this), generator_(this), theme_(loadTheme()) {
    setWindowTitle(QString::fromUtf8("小鱼抓图取色器"));
    setObjectName(QStringLiteral("xiaoyvToolsMainWindow"));
    setDockNestingEnabled(true);
    resize(1320, 820);

    deviceClient_.applyCommandLine(arguments);
    createActions();
    createMenus();
    createToolBar();
    createWorkspace();
    createPanels();
    createStatusBarWidgets();
    connectActions();
    connectWorkspaceSignals();
    connectDeviceSignals();
    setTheme(theme_);
    restoreWindowState();
    // 固定右下角归右侧停靠区；旧版本保存的布局也不能让日志重新横跨右侧工具列。
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    updateInspectorState();
    updateConnectionStatus();
    updateImageStatus();
    updateActionStates();
    QTimer::singleShot(0, &deviceClient_, &DeviceClient::checkConnection);

    if (!generator_.loadErrors().isEmpty()) {
        QTimer::singleShot(0, this, [this] {
            resultOutput_->appendPlainText(
                    QString::fromUtf8("[格式加载失败] ")
                    + generator_.loadErrors().join(QString::fromUtf8("；")));
            showStatus(QString::fromUtf8("部分生成格式加载失败，请查看日志"));
        });
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!fontPanel_->confirmDiscardOrSaveChanges()
            || !workspace_->confirmApplicationExit()) {
        event->ignore();
        return;
    }
    saveWindowState();
    event->accept();
}

void MainWindow::createActions() {
    for (const ActionSpec& spec : kActionCatalog) {
        auto* created = new QAction(QString::fromUtf8(spec.text), this);
        created->setObjectName(QString::fromUtf8(spec.objectName));
        if (spec.shortcut[0] != '\0') {
            created->setShortcut(QKeySequence(QString::fromLatin1(spec.shortcut)));
        }
        actions_[actionIndex(spec.id)] = created;
    }
}

void MainWindow::createMenus() {
    auto addMenu = [this](const QString& title, const QString& name) {
        QMenu* menu = menuBar()->addMenu(title);
        menu->setObjectName(name);
        return menu;
    };
    addMenu(QString::fromUtf8("文件"), QStringLiteral("fileMenu"));
    addMenu(QString::fromUtf8("编辑"), QStringLiteral("editMenu"));
    addMenu(QString::fromUtf8("图像"), QStringLiteral("imageMenu"));
    addMenu(QString::fromUtf8("设备"), QStringLiteral("deviceMenu"));
    QMenu* viewMenu = addMenu(QString::fromUtf8("视图"), QStringLiteral("viewMenu"));
    addMenu(QString::fromUtf8("工具"), QStringLiteral("toolsMenu"));

    for (MenuId menuId : {MenuId::File, MenuId::Edit, MenuId::Image,
                          MenuId::Device, MenuId::Tools}) {
        QMenu* menu = menuFor(menuId, menuBar());
        int previousGroup = -1;
        for (const ActionSpec& spec : kActionCatalog) {
            if (spec.menu != menuId) continue;
            if (previousGroup >= 0 && previousGroup != spec.group) menu->addSeparator();
            menu->addAction(action(spec.id));
            previousGroup = spec.group;
        }
    }

    QMenu* appearance = viewMenu->addMenu(QString::fromUtf8("外观"));
    themeGroup_ = new QActionGroup(this);
    themeGroup_->setExclusive(true);
    for (AppTheme theme : allThemes()) {
        QAction* item = appearance->addAction(themeDisplayName(theme));
        item->setCheckable(true);
        item->setData(static_cast<int>(theme));
        themeGroup_->addAction(item);
        connect(item, &QAction::triggered, this, [this, theme] { setTheme(theme); });
    }
}

void MainWindow::createToolBar() {
    auto* toolbar = addToolBar(QString::fromUtf8("主工具栏"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(22, 22));
    auto* tooltipFilter = new InstantTooltipFilter(toolbar);

    int previousGroup = -1;
    for (const ActionSpec& spec : kActionCatalog) {
        if (!spec.toolbar) continue;
        if (previousGroup >= 0 && previousGroup != spec.group) toolbar->addSeparator();
        if (spec.id == ActionId::WindowCapture) {
            windowCaptureButton_ = new WindowCaptureButton(toolbar);
            windowCaptureButton_->setObjectName(QStringLiteral("windowCaptureButton"));
            windowCaptureButton_->installEventFilter(tooltipFilter);
            toolbar->addWidget(windowCaptureButton_);
        } else {
            toolbar->addAction(action(spec.id));
        }
        previousGroup = spec.group;
    }
    for (QToolButton* button : toolbar->findChildren<QToolButton*>()) {
        button->installEventFilter(tooltipFilter);
    }
}

void MainWindow::createWorkspace() {
    workspace_ = new ImageWorkspace(this);
    setCentralWidget(workspace_);
}

void MainWindow::createPanels() {
    QMenu* viewMenu = menuBar()->findChild<QMenu*>(QStringLiteral("viewMenu"));
    colorPanel_ = new ColorPanel(&generator_, this);
    analysisPanel_ = new AnalysisPanel(this);
    fontPanel_ = new FontPanel(this);
    inspectorTabs_ = new QTabWidget(this);
    inspectorTabs_->setObjectName(QStringLiteral("inspectorTabs"));
    inspectorTabs_->setDocumentMode(true);
    inspectorTabs_->addTab(scrollable(colorPanel_), QString::fromUtf8("取色"));
    analysisTabIndex_ = inspectorTabs_->addTab(
            scrollable(analysisPanel_), QString::fromUtf8("二值化"));
    fontTabIndex_ = inspectorTabs_->addTab(
            scrollable(fontPanel_), QString::fromUtf8("字库"));
    auto* inspectorTabBar = inspectorTabs_->tabBar();
    inspectorTabBar->setExpanding(false);
    inspectorTabBar->setUsesScrollButtons(false);
    // 使用紧凑固定宽度，右侧停靠窗口缩窄时标签栏不再反向撑大窗口。
    const QFontMetrics tabMetrics(inspectorTabBar->font());
    const int tabWidth = tabMetrics.horizontalAdvance(QString::fromUtf8("二值化")) + 16;
    inspectorTabBar->setStyleSheet(QStringLiteral(
            "QTabBar::tab { min-width:%1px; max-width:%1px; }").arg(tabWidth));

    inspectorDock_ = makeDock(
            this, QString::fromUtf8("图像工具"), QStringLiteral("inspectorDock"),
            inspectorTabs_, Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    inspectorDock_->setMinimumWidth(280);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock_);
    inspectorDock_->toggleViewAction()->setText(QString::fromUtf8("图像工具"));
    viewMenu->addAction(inspectorDock_->toggleViewAction());

    scriptEditor_ = new ScriptEditorPanel(this);
    auto* codeDock = makeDock(
            this, QString::fromUtf8("生成代码"), QStringLiteral("codeDock"),
            scriptEditor_, Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    codeDock->setMinimumHeight(145);
    addDockWidget(Qt::RightDockWidgetArea, codeDock);
    splitDockWidget(inspectorDock_, codeDock, Qt::Vertical);
    resizeDocks({inspectorDock_, codeDock}, {650, 210}, Qt::Vertical);
    codeDock->toggleViewAction()->setText(QString::fromUtf8("生成代码"));
    viewMenu->addAction(codeDock->toggleViewAction());

    resultOutput_ = new QPlainTextEdit(this);
    resultOutput_->setReadOnly(true);
    resultOutput_->setPlaceholderText(QString::fromUtf8("设备测试结果和日志"));
    auto* resultDock = makeDock(
            this, QString::fromUtf8("测试结果与日志"), QStringLiteral("resultDock"),
            resultOutput_, Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    resultDock->setMinimumHeight(56);
    addDockWidget(Qt::BottomDockWidgetArea, resultDock);
    resizeDocks({resultDock}, {64}, Qt::Vertical);
    resultDock->toggleViewAction()->setText(QString::fromUtf8("测试结果与日志"));
    viewMenu->addAction(resultDock->toggleViewAction());

    connect(colorPanel_, &ColorPanel::selectionRangeEdited,
            workspace_, &ImageWorkspace::applySelectionRange);
    connect(fontPanel_, &FontPanel::selectionRangeEdited,
            workspace_, &ImageWorkspace::applySelectionRange);
    connect(colorPanel_, &ColorPanel::selectionModeRequested,
            workspace_, &ImageWorkspace::setSelectionMode);
    connect(fontPanel_, &FontPanel::selectionModeRequested,
            workspace_, &ImageWorkspace::setSelectionMode);
    connect(colorPanel_, &ColorPanel::statusMessage, this, &MainWindow::showStatus);
    connect(analysisPanel_, &AnalysisPanel::statusMessage, this, &MainWindow::showStatus);
    connect(fontPanel_, &FontPanel::statusMessage, this, &MainWindow::showStatus);
    connect(colorPanel_, &ColorPanel::codeGenerated, this, &MainWindow::showCode);
    connect(colorPanel_, &ColorPanel::runRequested, this,
            [this](const QString& language, const QString& code) {
                showCode(language, code);
                deviceClient_.runScript(language, code);
            });
    connect(scriptEditor_, &ScriptEditorPanel::runRequested,
            &deviceClient_, &DeviceClient::runScript);
    connect(scriptEditor_, &ScriptEditorPanel::stopRequested,
            &deviceClient_, &DeviceClient::stopScript);
    connect(scriptEditor_, &ScriptEditorPanel::statusMessage,
            this, &MainWindow::showStatus);
    connect(inspectorTabs_, &QTabWidget::currentChanged,
            this, &MainWindow::updateInspectorState);
    connect(inspectorDock_, &QDockWidget::visibilityChanged,
            this, &MainWindow::updateInspectorState);
}

void MainWindow::createStatusBarWidgets() {
    connectionStatus_ = new QLabel(deviceClient_.connectionMessage(), this);
    imageStatus_ = new QLabel(QString::fromUtf8("未打开图片"), this);
    cursorStatus_ = new QLabel(QString::fromUtf8("坐标 --,--  ------"), this);
    zoomStatus_ = new QLabel(QString::fromUtf8("缩放 --"), this);
    connectionStatus_->setObjectName(QStringLiteral("connectionStatus"));
    imageStatus_->setObjectName(QStringLiteral("imageStatus"));
    cursorStatus_->setObjectName(QStringLiteral("cursorStatus"));
    zoomStatus_->setObjectName(QStringLiteral("zoomStatus"));
    statusBar()->setSizeGripEnabled(true);
    statusBar()->addWidget(connectionStatus_);
    statusBar()->addPermanentWidget(cursorStatus_);
    statusBar()->addPermanentWidget(zoomStatus_);
    statusBar()->addPermanentWidget(imageStatus_);
}

void MainWindow::connectWorkspaceSignals() {
    connect(workspace_, &ImageWorkspace::currentDocumentChanged, this,
            [this](ImageDocument* document) {
                colorPanel_->setDocument(document);
                analysisPanel_->setDocument(document);
                fontPanel_->setDocument(document);
                updateInspectorState();
            });
    connect(workspace_, &ImageWorkspace::selectionRangeChanged, this,
            [this](const QRect& range) {
                colorPanel_->setSelectionRange(range);
                fontPanel_->setSelectionRange(range);
            });
    connect(workspace_, &ImageWorkspace::selectionModeChanged, this,
            [this](bool enabled) {
                colorPanel_->setSelectionMode(enabled);
                fontPanel_->setSelectionMode(enabled);
            });
    connect(workspace_, &ImageWorkspace::cursorChanged, this,
            [this](bool valid, const QPoint& point, const QColor& color) {
                cursorStatus_->setText(valid
                        ? QString::fromUtf8("坐标 %1,%2  %3")
                                  .arg(point.x()).arg(point.y())
                                  .arg(colorToHex(color))
                        : QString::fromUtf8("坐标 --,--  ------"));
            });
    connect(workspace_, &ImageWorkspace::zoomChanged, this,
            [this](bool valid, double zoom) {
                zoomStatus_->setText(valid
                        ? QString::fromUtf8("缩放 %1%").arg(qRound(zoom * 100.0))
                        : QString::fromUtf8("缩放 --"));
            });
    connect(workspace_, &ImageWorkspace::stateChanged, this, [this] {
        updateImageStatus();
        updateActionStates();
    });
    connect(workspace_, &ImageWorkspace::statusMessage,
            this, &MainWindow::showStatus);
}

void MainWindow::connectDeviceSignals() {
    connect(&deviceClient_, &DeviceClient::connectionStateChanged,
            this, [this](DeviceClient::ConnectionState, const QString&) {
                updateConnectionStatus();
            });
    connect(&deviceClient_, &DeviceClient::screenshotReceived, this,
            [this](const QImage& image) {
                workspace_->addTemporaryImage(image);
            });
    connect(&deviceClient_, &DeviceClient::projectionOpened, this, [this] {
        showStatus(QString::fromUtf8("图片已在设备上打开，按设备返回键退出"));
    });
    connect(&deviceClient_, &DeviceClient::scriptStarted, this, [this] {
        resultOutput_->clear();
        resultOutput_->appendPlainText(QString::fromUtf8("[脚本开始运行]"));
    });
    connect(&deviceClient_, &DeviceClient::scriptLogsReceived, this,
            [this](const QStringList& logs) {
                for (const QString& log : logs) resultOutput_->appendPlainText(log);
            });
    connect(&deviceClient_, &DeviceClient::scriptFinished, this,
            [this](const QString& summary, const QStringList&) {
                resultOutput_->appendPlainText(QString::fromUtf8("[运行结果] ") + summary);
                showStatus(QString::fromUtf8("设备测试脚本执行完成"));
            });
    connect(&deviceClient_, &DeviceClient::stateChanged,
            this, &MainWindow::updateActionStates);
    connect(&deviceClient_, &DeviceClient::requestFailed, this,
            [this](const QString& operation, const QString& message) {
                resultOutput_->appendPlainText(QStringLiteral("[%1失败] %2").arg(operation, message));
                showStatus(operation + QString::fromUtf8("失败：") + message);
            });
}

void MainWindow::connectActions() {
    connect(action(ActionId::Open), &QAction::triggered,
            workspace_, &ImageWorkspace::openImages);
    connect(action(ActionId::Save), &QAction::triggered,
            workspace_, &ImageWorkspace::saveCurrent);
    connect(action(ActionId::SaveAs), &QAction::triggered,
            workspace_, &ImageWorkspace::saveCurrentAs);
    connect(action(ActionId::Exit), &QAction::triggered, this, &QWidget::close);
    connect(action(ActionId::Undo), &QAction::triggered,
            workspace_, &ImageWorkspace::undo);
    connect(action(ActionId::Redo), &QAction::triggered,
            workspace_, &ImageWorkspace::redo);
    connect(action(ActionId::RotateLeft), &QAction::triggered,
            workspace_, &ImageWorkspace::rotateLeft);
    connect(action(ActionId::RotateRight), &QAction::triggered,
            workspace_, &ImageWorkspace::rotateRight);
    connect(action(ActionId::FlipHorizontal), &QAction::triggered,
            workspace_, &ImageWorkspace::flipHorizontal);
    connect(action(ActionId::FlipVertical), &QAction::triggered,
            workspace_, &ImageWorkspace::flipVertical);
    connect(action(ActionId::Crop), &QAction::triggered,
            workspace_, &ImageWorkspace::cropSelection);
    connect(action(ActionId::ZoomIn), &QAction::triggered,
            workspace_, &ImageWorkspace::zoomIn);
    connect(action(ActionId::ZoomOut), &QAction::triggered,
            workspace_, &ImageWorkspace::zoomOut);
    connect(action(ActionId::Fit), &QAction::triggered,
            workspace_, &ImageWorkspace::fitToViewport);
    connect(action(ActionId::ActualSize), &QAction::triggered,
            workspace_, &ImageWorkspace::actualSize);
    connect(action(ActionId::DeviceScreenshot), &QAction::triggered,
            &deviceClient_, &DeviceClient::requestScreenshot);
    connect(action(ActionId::FloatingCapture), &QAction::triggered,
            this, &MainWindow::showFloatingCapture);
    connect(action(ActionId::WindowCapture), &QAction::triggered,
            windowCaptureButton_, &WindowCaptureButton::triggerCapture);
    connect(action(ActionId::Projection), &QAction::triggered, this, [this] {
        deviceClient_.projectImage(workspace_->currentDisplayedImage());
    });
    connect(action(ActionId::CheckConnection), &QAction::triggered,
            &deviceClient_, &DeviceClient::checkConnection);
    connect(action(ActionId::ConnectionSettings), &QAction::triggered,
            this, &MainWindow::showConnectionDialog);
    connect(action(ActionId::ReloadFormats), &QAction::triggered,
            colorPanel_, &ColorPanel::reloadFormats);
    connect(action(ActionId::OpenFormatsDirectory), &QAction::triggered, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(generator_.formatsDirectory()));
    });
    connect(windowCaptureButton_, &WindowCaptureButton::imageCaptured, this,
            [this](const QImage& image) {
                workspace_->addTemporaryImage(image);
            });
    connect(windowCaptureButton_, &WindowCaptureButton::captureFailed,
            this, &MainWindow::showStatus);
    connect(windowCaptureButton_, &WindowCaptureButton::statusMessage,
            this, &MainWindow::showStatus);
}

QAction* MainWindow::action(ActionId id) const {
    return actions_[actionIndex(id)];
}

void MainWindow::updateActionStates() {
    ImageDocument* document = workspace_->currentDocument();
    const bool hasImage = document != nullptr;
    action(ActionId::Save)->setEnabled(hasImage);
    action(ActionId::SaveAs)->setEnabled(hasImage);
    action(ActionId::Undo)->setEnabled(hasImage && document->canUndo());
    action(ActionId::Redo)->setEnabled(hasImage && document->canRedo());
    for (ActionId id : {ActionId::RotateLeft, ActionId::RotateRight,
                        ActionId::FlipHorizontal, ActionId::FlipVertical,
                        ActionId::ZoomIn, ActionId::ZoomOut,
                        ActionId::Fit, ActionId::ActualSize}) {
        action(id)->setEnabled(hasImage);
    }
    action(ActionId::Crop)->setEnabled(hasImage && !document->selection().isNull());
    action(ActionId::Projection)->setEnabled(
            hasImage && !deviceClient_.isBusy(DeviceClient::Operation::Projection));
    action(ActionId::DeviceScreenshot)->setEnabled(
            !deviceClient_.isBusy(DeviceClient::Operation::Screenshot));
    action(ActionId::CheckConnection)->setEnabled(
            !deviceClient_.isBusy(DeviceClient::Operation::Connection));
    scriptEditor_->setExecutionState(
            deviceClient_.isScriptRunning(),
            deviceClient_.isBusy(DeviceClient::Operation::Script),
            deviceClient_.isBusy(DeviceClient::Operation::ScriptStop));
}

void MainWindow::updateConnectionStatus() {
    if (connectionStatus_ == nullptr) return;
    const auto state = deviceClient_.connectionState();
    QString color = themePalette(theme_).muted;
    if (state == DeviceClient::ConnectionState::Connected) {
        color = themePalette(theme_).success;
    } else if (state == DeviceClient::ConnectionState::Disconnected) {
        color = themePalette(theme_).danger;
    }
    connectionStatus_->setText(deviceClient_.connectionMessage());
    connectionStatus_->setStyleSheet(QStringLiteral("color:%1").arg(color));
}

void MainWindow::updateImageStatus() {
    ImageDocument* document = workspace_->currentDocument();
    imageStatus_->setText(document == nullptr
            ? QString::fromUtf8("未打开图片")
            : QStringLiteral("%1 x %2")
                      .arg(document->image().width())
                      .arg(document->image().height()));
}

void MainWindow::updateInspectorState() {
    const bool inspectorVisible = inspectorDock_ != nullptr && inspectorDock_->isVisible();
    const int currentTab = inspectorTabs_ == nullptr ? -1 : inspectorTabs_->currentIndex();
    analysisPanel_->setPreviewActive(
            inspectorVisible && currentTab == analysisTabIndex_);
    workspace_->setConfirmedSelectionVisible(
            workspace_->currentCanvas() != nullptr
            && inspectorVisible
            && currentTab == fontTabIndex_);
}

void MainWindow::showFloatingCapture() {
    if (floatingCapture_ == nullptr) {
        floatingCapture_ = new FloatingCaptureWindow();
        connect(floatingCapture_, &FloatingCaptureWindow::imageCaptured, this,
                [this](const QImage& image) {
                    workspace_->addTemporaryImage(image);
                });
        connect(floatingCapture_, &FloatingCaptureWindow::captureFailed,
                this, &MainWindow::showStatus);
        connect(floatingCapture_, &QObject::destroyed,
                this, [this] { floatingCapture_ = nullptr; });
        floatingCapture_->move(frameGeometry().topLeft() + QPoint(80, 80));
    }
    floatingCapture_->show();
    floatingCapture_->raise();
    floatingCapture_->activateWindow();
}

void MainWindow::showConnectionDialog() {
    ConnectionDialog dialog(deviceClient_.settings(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    deviceClient_.setSettings(dialog.value());
    deviceClient_.saveSettings();
    deviceClient_.checkConnection();
}

void MainWindow::showCode(const QString& language, const QString& code) {
    scriptEditor_->setCode(language, code);
}

void MainWindow::showStatus(const QString& message) {
    statusBar()->showMessage(message, 6000);
}

void MainWindow::setTheme(AppTheme theme) {
    theme_ = theme;
    saveTheme(theme_);
    applyApplicationTheme(qApp, theme_);
    applyWidgetTheme(this, theme_);
    applyNativeTitleBar(this, isDarkTheme(theme_));
    for (CompactDockWidget* dock : findChildren<CompactDockWidget*>()) {
        dock->setDarkChrome(isDarkTheme(theme_));
    }
    for (QAction* item : themeGroup_->actions()) {
        item->setChecked(static_cast<AppTheme>(item->data().toInt()) == theme_);
    }
    updateConnectionStatus();
    refreshIcons();
}

void MainWindow::refreshIcons() {
    for (const ActionSpec& spec : kActionCatalog) {
        if (spec.hasIcon) action(spec.id)->setIcon(makeToolIcon(spec.icon, isDarkTheme(theme_)));
    }
    if (windowCaptureButton_ != nullptr) {
        windowCaptureButton_->setDarkTheme(isDarkTheme(theme_));
    }
}

void MainWindow::restoreWindowState() {
    QSettings settings;
    const QByteArray geometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    const QByteArray state = settings.value(QStringLiteral("window/state")).toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    if (!state.isEmpty()) restoreState(state, 2);
}

void MainWindow::saveWindowState() const {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState(2));
}

} // namespace xiaoyv::tools
