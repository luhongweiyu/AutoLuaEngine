/**
 * 文件用途：声明带颜色块和十六进制输入框的颜色控件，供二值化目标色与偏色复用。
 */
#pragma once

#include <QColor>
#include <QWidget>

class QLineEdit;
class QToolButton;

namespace xiaoyv::tools {

class ColorInput final : public QWidget {
    Q_OBJECT

public:
    explicit ColorInput(const QColor& initial, QWidget* parent = nullptr);

    QColor color() const;
    void setColor(const QColor& color);

signals:
    void colorChanged(const QColor& color);
    void inputRejected(const QString& message);

private:
    void updateSwatch();
    void commitText();

    QToolButton* swatch_ = nullptr;
    QLineEdit* edit_ = nullptr;
    QColor color_;
};

} // namespace xiaoyv::tools
