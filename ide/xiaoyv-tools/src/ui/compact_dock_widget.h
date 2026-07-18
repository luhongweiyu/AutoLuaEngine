/**
 * 文件用途：声明紧凑停靠窗；嵌入时仅保留操作柄，悬浮时使用匹配主题的标题栏。
 */
#pragma once

#include <QDockWidget>

class QShowEvent;

namespace xiaoyv::tools {

class CompactDockWidget final : public QDockWidget {
    Q_OBJECT

public:
    explicit CompactDockWidget(const QString& title, QWidget* parent = nullptr);

    void setDarkChrome(bool dark);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    class Grip;
    class FloatingTitleBar;

    void applyChrome(bool floating);
    void positionGrip();

    QWidget* hiddenTitleBar_ = nullptr;
    Grip* grip_ = nullptr;
    FloatingTitleBar* floatingTitleBar_ = nullptr;
    bool dark_ = true;
};

} // namespace xiaoyv::tools
