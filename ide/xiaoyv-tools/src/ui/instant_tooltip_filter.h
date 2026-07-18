/**
 * 文件用途：让纯图标工具按钮在鼠标进入时立即显示中文说明。
 */
#pragma once

#include <QEvent>
#include <QObject>
#include <QToolTip>
#include <QWidget>

namespace xiaoyv::tools {

class InstantTooltipFilter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::Enter) {
            auto* widget = qobject_cast<QWidget*>(watched);
            if (widget != nullptr && !widget->toolTip().isEmpty()) {
                QToolTip::showText(widget->mapToGlobal(QPoint(widget->width() / 2, widget->height())),
                                   widget->toolTip(), widget);
            }
        } else if (event->type() == QEvent::Leave) {
            QToolTip::hideText();
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace xiaoyv::tools
