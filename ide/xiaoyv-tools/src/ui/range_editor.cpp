/**
 * 文件用途：实现不依赖图片尺寸的手动范围输入、框选开关和恢复全图操作。
 */
#include "ui/range_editor.h"

#include "core/selection_range.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>

namespace xiaoyv::tools {

namespace {

bool hasReversedEndpoints(const QString& text) {
    const QStringList parts = text.trimmed().split(QLatin1Char(','));
    if (parts.size() != 4) return false;

    int values[4]{};
    for (int index = 0; index < 4; ++index) {
        bool ok = false;
        values[index] = parts[index].trimmed().toInt(&ok);
        if (!ok) return false;
    }
    return values[0] > values[2] || values[1] > values[3];
}

} // namespace

RangeEditor::RangeEditor(QWidget* parent)
        : QWidget(parent) {
    selectButton_ = new QPushButton(QString::fromUtf8("框选 A/S/Ctrl R"), this);
    selectButton_->setCheckable(true);
    selectButton_->setToolTip(QString::fromUtf8("框选：A 设置左上角，S 设置右下角，Ctrl+R 切换框选模式"));
    rangeEdit_ = new QLineEdit(formatSelectionRange(range_), this);
    rangeEdit_->setPlaceholderText(QString::fromUtf8("左,上,右,下"));
    rangeEdit_->setToolTip(QString::fromUtf8(
            "快捷键：A 设置框选左上角；S 设置框选右下角"));
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
    connect(rangeEdit_, &QLineEdit::textChanged, this, &RangeEditor::updateEndpointOrderWarning);
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

void RangeEditor::setInputMinimumDigits(int count) {
    if (count <= 0) return;
    const QString sample(count, QLatin1Char('0'));
    rangeEdit_->setMinimumWidth(rangeEdit_->fontMetrics().horizontalAdvance(sample) + 18);
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

void RangeEditor::updateEndpointOrderWarning(const QString& text) {
    const bool reversed = hasReversedEndpoints(text);
    if (rangeEdit_->property("reversedEndpoints").toBool() == reversed) return;
    rangeEdit_->setProperty("reversedEndpoints", reversed);
    rangeEdit_->style()->unpolish(rangeEdit_);
    rangeEdit_->style()->polish(rangeEdit_);
    rangeEdit_->update();
}

} // namespace xiaoyv::tools
