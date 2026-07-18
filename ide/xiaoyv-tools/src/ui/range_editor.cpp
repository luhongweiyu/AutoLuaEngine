/**
 * 文件用途：实现不依赖图片尺寸的手动范围输入、框选开关和恢复全图操作。
 */
#include "ui/range_editor.h"

#include "core/selection_range.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>

namespace xiaoyv::tools {

RangeEditor::RangeEditor(QWidget* parent)
        : QWidget(parent) {
    selectButton_ = new QPushButton(QString::fromUtf8("框选"), this);
    selectButton_->setCheckable(true);
    rangeEdit_ = new QLineEdit(QString::fromUtf8("全图"), this);
    rangeEdit_->setPlaceholderText(QStringLiteral("left,top,right,bottom"));
    clearButton_ = new QToolButton(this);
    clearButton_->setText(QStringLiteral("×"));
    clearButton_->setToolTip(QString::fromUtf8("清除范围，恢复全图"));
    clearButton_->setAutoRaise(true);
    clearButton_->setFixedSize(18, 18);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(selectButton_);
    layout->addWidget(rangeEdit_, 1);
    layout->addWidget(clearButton_);

    connect(selectButton_, &QPushButton::toggled, this, &RangeEditor::selectionModeRequested);
    connect(rangeEdit_, &QLineEdit::editingFinished, this, &RangeEditor::commitText);
    connect(clearButton_, &QToolButton::clicked, this, [this] {
        if (range_.isNull()) return;
        range_ = {};
        rangeEdit_->setText(formatSelectionRange(range_));
        emit rangeEdited(range_);
    });
}

void RangeEditor::setRange(const QRect& range) {
    range_ = range;
    rangeEdit_->setText(formatSelectionRange(range_));
}

void RangeEditor::setSelectionMode(bool enabled) {
    const QSignalBlocker blocker(selectButton_);
    selectButton_->setChecked(enabled);
}

void RangeEditor::commitText() {
    QRect parsed;
    QString error;
    if (!parseSelectionRange(rangeEdit_->text(), &parsed, &error)) {
        rangeEdit_->setText(formatSelectionRange(range_));
        emit inputRejected(error);
        return;
    }
    if (parsed == range_) return;
    range_ = parsed;
    rangeEdit_->setText(formatSelectionRange(range_));
    emit rangeEdited(range_);
}

} // namespace xiaoyv::tools
