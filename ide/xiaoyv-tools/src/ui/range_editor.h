/**
 * 文件用途：声明取色与点阵字库共用的紧凑范围编辑控件。
 */
#pragma once

#include <QRect>
#include <QWidget>

class QLineEdit;
class QPushButton;
class QToolButton;

namespace xiaoyv::tools {

class RangeEditor final : public QWidget {
    Q_OBJECT

public:
    explicit RangeEditor(QWidget* parent = nullptr);

    void setRange(const QRect& range);
    void setSelectionMode(bool enabled);

signals:
    void rangeEdited(const QRect& range);
    void selectionModeRequested(bool enabled);
    void inputRejected(const QString& message);

private:
    void commitText();

    QPushButton* selectButton_ = nullptr;
    QLineEdit* rangeEdit_ = nullptr;
    QToolButton* clearButton_ = nullptr;
    QRect range_;
};

} // namespace xiaoyv::tools
