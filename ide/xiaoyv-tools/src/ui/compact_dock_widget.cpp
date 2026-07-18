/**
 * 文件用途：实现不占标题高度的嵌入停靠窗和可原生拖回主窗口的悬浮标题栏。
 */
#include "ui/compact_dock_widget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QShowEvent>
#include <QTimer>
#include <QToolButton>

namespace xiaoyv::tools {

class CompactDockWidget::Grip final : public QToolButton {
public:
    explicit Grip(CompactDockWidget* dock)
            : QToolButton(dock), dock_(dock) {
        setObjectName(QStringLiteral("dockOperationGrip"));
        setText(QStringLiteral("⋮"));
        setToolTip(QString::fromUtf8("窗口操作"));
        setFixedSize(14, 24);
        setAutoRaise(true);
        connect(this, &QToolButton::clicked, this, [this] {
            QMenu menu;
            QAction* floating = menu.addAction(QString::fromUtf8("浮动"));
            QAction* close = menu.addAction(QString::fromUtf8("关闭"));
            QAction* selected = menu.exec(mapToGlobal(QPoint(0, height())));
            if (selected == floating) dock_->setFloating(true);
            else if (selected == close) dock_->close();
        });
    }

private:
    CompactDockWidget* dock_ = nullptr;
};

class CompactDockWidget::FloatingTitleBar final : public QWidget {
public:
    explicit FloatingTitleBar(CompactDockWidget* dock)
            : QWidget(dock), dock_(dock) {
        label_ = new QLabel(dock->windowTitle(), this);
        auto* close = new QToolButton(this);
        close->setText(QStringLiteral("×"));
        close->setAutoRaise(true);
        close->setFixedSize(24, 22);
        connect(close, &QToolButton::clicked, dock, &QWidget::close);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(7, 2, 2, 2);
        layout->addWidget(label_, 1);
        layout->addWidget(close);
        setFixedHeight(27);
    }

    void setDark(bool dark) {
        setStyleSheet(dark
                ? QStringLiteral("background:#252526;border-bottom:1px solid #3C3C3C;")
                : QStringLiteral("background:#E7E7E7;border-bottom:1px solid #C8C8C8;"));
    }

protected:
    void mousePressEvent(QMouseEvent* event) override { event->ignore(); }
    void mouseMoveEvent(QMouseEvent* event) override { event->ignore(); }
    void mouseDoubleClickEvent(QMouseEvent* event) override { event->ignore(); }

private:
    CompactDockWidget* dock_ = nullptr;
    QLabel* label_ = nullptr;
};

CompactDockWidget::CompactDockWidget(const QString& title, QWidget* parent)
        : QDockWidget(title, parent) {
    setObjectName(title);
    setFeatures(QDockWidget::DockWidgetClosable
                | QDockWidget::DockWidgetMovable
                | QDockWidget::DockWidgetFloatable);
    hiddenTitleBar_ = new QWidget(this);
    hiddenTitleBar_->setObjectName(QStringLiteral("hiddenDockTitleBar"));
    hiddenTitleBar_->setFixedHeight(0);
    grip_ = new Grip(this);
    floatingTitleBar_ = new FloatingTitleBar(this);
    floatingTitleBar_->setObjectName(QStringLiteral("floatingDockTitleBar"));
    connect(this, &QDockWidget::topLevelChanged, this, &CompactDockWidget::applyChrome);
    applyChrome(false);
}

void CompactDockWidget::setDarkChrome(bool dark) {
    dark_ = dark;
    floatingTitleBar_->setDark(dark_);
    grip_->setStyleSheet(dark_
            ? QStringLiteral("QToolButton{background:rgba(45,45,48,190);color:#EEE;}")
            : QStringLiteral("QToolButton{background:rgba(235,235,235,210);color:#222;}"));
}

void CompactDockWidget::resizeEvent(QResizeEvent* event) {
    QDockWidget::resizeEvent(event);
    positionGrip();
}

void CompactDockWidget::showEvent(QShowEvent* event) {
    QDockWidget::showEvent(event);
    // 恢复启动布局时最终宽度可能晚于首次 resizeEvent；进入事件循环后按最终几何重新定位操作柄。
    QTimer::singleShot(0, this, [this] {
        if (!isFloating()) positionGrip();
    });
}

void CompactDockWidget::applyChrome(bool floating) {
    setTitleBarWidget(floating ? static_cast<QWidget*>(floatingTitleBar_) : hiddenTitleBar_);
    // QDockWidget 不一定会自动隐藏上一次的自定义标题栏；显式控制可见性，避免悬浮标题
    // 在重新嵌入后残留在内容区，表现为按钮后方多出一行“生成代码 ×”。
    floatingTitleBar_->setVisible(floating);
    hiddenTitleBar_->setVisible(!floating);
    grip_->setVisible(!floating);
    if (!floating) positionGrip();
}

void CompactDockWidget::positionGrip() {
    grip_->move(std::max(0, width() - grip_->width() - 2), 2);
    grip_->raise();
}

} // namespace xiaoyv::tools
