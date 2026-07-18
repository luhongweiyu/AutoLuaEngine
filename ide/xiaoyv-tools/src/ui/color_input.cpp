/**
 * 文件用途：实现颜色块选择和 RRGGBB 文本输入的双向同步。
 */
#include "ui/color_input.h"

#include "model/color_point_model.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>

namespace xiaoyv::tools {

ColorInput::ColorInput(const QColor& initial, QWidget* parent)
        : QWidget(parent), color_(initial) {
    swatch_ = new QToolButton(this);
    swatch_->setFixedSize(24, 22);
    swatch_->setToolTip(QString::fromUtf8("选择颜色"));
    edit_ = new QLineEdit(colorToHex(color_), this);
    edit_->setMaxLength(8);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(swatch_);
    layout->addWidget(edit_, 1);
    updateSwatch();

    connect(swatch_, &QToolButton::clicked, this, [this] {
        const QColor selected = QColorDialog::getColor(color_, this, QString::fromUtf8("选择颜色"));
        if (selected.isValid()) setColor(selected);
    });
    connect(edit_, &QLineEdit::editingFinished, this, &ColorInput::commitText);
}

QColor ColorInput::color() const {
    return color_;
}

void ColorInput::setColor(const QColor& color) {
    if (!color.isValid() || color_ == color) return;
    color_ = color;
    edit_->setText(colorToHex(color_));
    updateSwatch();
    emit colorChanged(color_);
}

void ColorInput::updateSwatch() {
    swatch_->setStyleSheet(QStringLiteral("QToolButton{background:#%1;border:1px solid #777;}")
            .arg(colorToHex(color_)));
}

void ColorInput::commitText() {
    QColor parsed;
    if (!parseRgbHex(edit_->text(), &parsed)) {
        edit_->setText(colorToHex(color_));
        emit inputRejected(QString::fromUtf8("颜色格式应为 RRGGBB"));
        return;
    }
    setColor(parsed);
}

} // namespace xiaoyv::tools
