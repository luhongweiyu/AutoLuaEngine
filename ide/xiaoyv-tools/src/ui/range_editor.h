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
    void setInputMinimumDigits(int count);

signals:
    void rangeEdited(const QRect& range);
    void selectionModeRequested(bool enabled);
    void inputRejected(const QString& message);

private:
    void commitText();
    void updateEndpointOrderWarning(const QString& text);

    QPushButton* selectButton_ = nullptr;
    QLineEdit* rangeEdit_ = nullptr;
    QToolButton* clearButton_ = nullptr;
    QRect range_{0, 0, 1, 1};
};

} // namespace xiaoyv::tools
