/**
 * 文件用途：实现字库增删替换、批量冲突事务、外部合并和原子保存交互。
 */
#include "panels/font_dictionary_editor.h"

#include <QAbstractItemView>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace xiaoyv::tools {

FontDictionaryEditor::FontDictionaryEditor(QWidget* parent)
        : QWidget(parent), dictionary_(this) {
    pathEdit_ = new QLineEdit(this);
    pathEdit_->setReadOnly(true);
    pathEdit_->setPlaceholderText(QString::fromUtf8("未打开字库文件"));
    auto* openButton = new QToolButton(this);
    openButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    openButton->setToolTip(QString::fromUtf8("打开点阵字库"));
    openButton->setAutoRaise(true);
    auto* pathLayout = new QHBoxLayout();
    pathLayout->setContentsMargins(0, 0, 0, 0);
    pathLayout->setSpacing(4);
    pathLayout->addWidget(pathEdit_, 1);
    pathLayout->addWidget(openButton);

    list_ = new QListView(this);
    list_->setObjectName(QStringLiteral("fontDictionaryList"));
    list_->setModel(&dictionary_);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setMinimumHeight(120);
    auto* addButton = new QPushButton(QString::fromUtf8("添加当前"), this);
    auto* addAllButton = new QPushButton(QString::fromUtf8("批量加入"), this);
    auto* replaceButton = new QPushButton(QString::fromUtf8("替换选中"), this);
    auto* deleteButton = new QPushButton(QString::fromUtf8("删除"), this);
    auto* saveButton = new QPushButton(QString::fromUtf8("保存字库"), this);
    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(4);
    actions->addWidget(addButton);
    actions->addWidget(addAllButton);
    actions->addWidget(replaceButton);
    actions->addWidget(deleteButton);
    actions->addStretch();
    actions->addWidget(saveButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    layout->addLayout(pathLayout);
    layout->addWidget(list_);
    layout->addLayout(actions);

    connect(openButton, &QToolButton::clicked, this, &FontDictionaryEditor::openDictionary);
    connect(saveButton, &QPushButton::clicked, this, &FontDictionaryEditor::saveDictionary);
    connect(addButton, &QPushButton::clicked, this, &FontDictionaryEditor::addCurrentRecord);
    connect(addAllButton, &QPushButton::clicked, this, &FontDictionaryEditor::addBatchRecords);
    connect(replaceButton, &QPushButton::clicked, this, &FontDictionaryEditor::replaceSelectedRecord);
    connect(deleteButton, &QPushButton::clicked, this, &FontDictionaryEditor::deleteSelectedRecord);
    connect(list_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                if (current.isValid()) emit recordSelected(dictionary_.records()[current.row()]);
            });
    connect(&dictionary_, &FontDictionaryDocument::modifiedChanged, this, [this](bool modified) {
        pathEdit_->setText(dictionary_.filePath()
                + (modified ? QStringLiteral(" *") : QString{}));
    });
}

void FontDictionaryEditor::setRecordProvider(RecordProvider provider) {
    recordProvider_ = std::move(provider);
}

void FontDictionaryEditor::setBatchProvider(BatchProvider provider) {
    batchProvider_ = std::move(provider);
}

bool FontDictionaryEditor::confirmDiscardOrSaveChanges() {
    if (!dictionary_.isModified()) return true;
    QMessageBox box(QMessageBox::Question,
                    QString::fromUtf8("字库尚未保存"),
                    QString::fromUtf8("当前点阵字库有未保存修改。"),
                    QMessageBox::NoButton,
                    this);
    QPushButton* save = box.addButton(QString::fromUtf8("保存"), QMessageBox::AcceptRole);
    QPushButton* discard = box.addButton(
            QString::fromUtf8("放弃修改"), QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(QString::fromUtf8("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(save);
    box.exec();
    if (box.clickedButton() == cancel) return false;
    if (box.clickedButton() == save) return saveDictionary();
    if (box.clickedButton() == discard) dictionary_.revert();
    return true;
}

void FontDictionaryEditor::addCurrentRecord() {
    if (!recordProvider_) {
        emit statusMessage(QString::fromUtf8("当前点阵提供器未配置"));
        return;
    }
    QString error;
    const FontDictionaryRecord incoming = recordProvider_(&error);
    if (!error.isEmpty()) {
        emit statusMessage(error);
        return;
    }
    const int duplicate = FontDictionaryDocument::findSamePixels(dictionary_.records(), incoming);
    FontDictionaryDocument::DuplicateDecision decision =
            FontDictionaryDocument::DuplicateDecision::Skip;
    if (duplicate >= 0 && dictionary_.records()[duplicate].label != incoming.label) {
        decision = askDuplicateDecision(dictionary_.records()[duplicate], incoming, false);
    }
    int affected = -1;
    const auto result = dictionary_.add(incoming, decision, &affected);
    if (result == FontDictionaryDocument::WriteResult::Cancelled) return;
    selectRow(affected);
    emit statusMessage(result == FontDictionaryDocument::WriteResult::Added
            ? QString::fromUtf8("点阵已加入字库")
            : result == FontDictionaryDocument::WriteResult::Replaced
                    ? QString::fromUtf8("冲突点阵已替换")
                    : QString::fromUtf8("相同文字和点阵已存在，已跳过"));
}

void FontDictionaryEditor::addBatchRecords() {
    if (!batchProvider_) {
        emit statusMessage(QString::fromUtf8("批量点阵提供器未配置"));
        return;
    }
    QString error;
    const QVector<FontDictionaryRecord> incomingRecords = batchProvider_(&error);
    if (!error.isEmpty()) {
        emit statusMessage(error);
        return;
    }
    QVector<FontDictionaryRecord> transaction = dictionary_.records();
    std::optional<FontDictionaryDocument::DuplicateDecision> applyToAll;
    int added = 0;
    int replaced = 0;
    int skipped = 0;
    for (const FontDictionaryRecord& incoming : incomingRecords) {
        const int duplicate = FontDictionaryDocument::findSamePixels(transaction, incoming);
        FontDictionaryDocument::DuplicateDecision decision =
                FontDictionaryDocument::DuplicateDecision::Skip;
        if (duplicate >= 0 && transaction[duplicate].label != incoming.label) {
            decision = applyToAll.has_value()
                    ? *applyToAll
                    : askDuplicateDecision(transaction[duplicate], incoming, true, &applyToAll);
            if (decision == FontDictionaryDocument::DuplicateDecision::Cancel) {
                emit statusMessage(QString::fromUtf8("已取消批量加入，字库未发生变化"));
                return;
            }
        }
        const auto result = FontDictionaryDocument::addTo(&transaction, incoming, decision);
        if (result == FontDictionaryDocument::WriteResult::Added) ++added;
        else if (result == FontDictionaryDocument::WriteResult::Replaced) ++replaced;
        else ++skipped;
    }
    const int selectedBefore = list_->currentIndex().row();
    if (!dictionary_.commitTransaction(std::move(transaction), &error)) {
        emit statusMessage(error);
        return;
    }
    selectRow(dictionary_.rowCount() == 0
            ? -1
            : std::clamp(selectedBefore, 0, dictionary_.rowCount() - 1));
    emit statusMessage(QString::fromUtf8("批量完成：新增 %1，替换 %2，跳过 %3")
            .arg(added).arg(replaced).arg(skipped));
}

void FontDictionaryEditor::replaceSelectedRecord() {
    const int selected = list_->currentIndex().row();
    if (selected < 0 || selected >= dictionary_.rowCount()) {
        emit statusMessage(QString::fromUtf8("请先选择一条字库记录"));
        return;
    }
    if (!recordProvider_) {
        emit statusMessage(QString::fromUtf8("当前点阵提供器未配置"));
        return;
    }
    QString error;
    const FontDictionaryRecord incoming = recordProvider_(&error);
    if (!error.isEmpty()) {
        emit statusMessage(error);
        return;
    }
    const int duplicate = FontDictionaryDocument::findSamePixels(
            dictionary_.records(), incoming, selected);
    FontDictionaryDocument::DuplicateDecision decision =
            FontDictionaryDocument::DuplicateDecision::Skip;
    if (duplicate >= 0 && dictionary_.records()[duplicate].label != incoming.label) {
        decision = askDuplicateDecision(dictionary_.records()[duplicate], incoming, false);
    }
    int affected = -1;
    const auto result = dictionary_.replace(selected, incoming, decision, &affected);
    if (result == FontDictionaryDocument::WriteResult::Cancelled) return;
    selectRow(affected >= 0 ? affected : selected);
    emit statusMessage(result == FontDictionaryDocument::WriteResult::Replaced
            ? QString::fromUtf8("已替换选中字库记录")
            : QString::fromUtf8("目标点阵已存在，未修改字库"));
}

void FontDictionaryEditor::deleteSelectedRecord() {
    const int selected = list_->currentIndex().row();
    if (!dictionary_.remove(selected)) return;
    selectRow(std::min(selected, dictionary_.rowCount() - 1));
    emit statusMessage(QString::fromUtf8("字库记录已删除，保存后写入磁盘"));
}

void FontDictionaryEditor::openDictionary() {
    const QString path = QFileDialog::getOpenFileName(
            this, QString::fromUtf8("打开点阵字库"), dictionary_.filePath(),
            QString::fromUtf8("文本字库 (*.txt *.dict);;所有文件 (*)"));
    if (path.isEmpty() || !confirmDiscardOrSaveChanges()) return;
    QString error;
    if (!dictionary_.load(path, &error)) {
        QMessageBox::warning(this, QString::fromUtf8("打开字库失败"), error);
        return;
    }
    pathEdit_->setText(path);
    selectRow(0);
    const QString warning = dictionary_.loadWarnings().isEmpty()
            ? QString{}
            : QString::fromUtf8("；另有 %1 行未识别但会原样保留")
                      .arg(dictionary_.loadWarnings().size());
    emit statusMessage(QString::fromUtf8("已打开字库：%1 条记录%2")
            .arg(dictionary_.rowCount()).arg(warning));
}

bool FontDictionaryEditor::saveDictionary() {
    int selectedBefore = list_->currentIndex().row();
    QString path = dictionary_.filePath();
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
                this, QString::fromUtf8("保存点阵字库"), {},
                QString::fromUtf8("文本字库 (*.txt);;所有文件 (*)"));
        if (path.isEmpty()) return false;
    }
    QString error;
    const bool saved = dictionary_.save(
            path,
            [this](const FontDictionaryRecord& existing,
                   const FontDictionaryRecord& incoming,
                   int) {
                return askDuplicateDecision(existing, incoming, false);
            },
            &error);
    if (!saved) {
        if (error != QString::fromUtf8("已取消保存字库")) {
            QMessageBox::warning(this, QString::fromUtf8("保存字库失败"), error);
        }
        return false;
    }
    pathEdit_->setText(path);
    selectRow(dictionary_.rowCount() == 0
            ? -1
            : std::clamp(selectedBefore, 0, dictionary_.rowCount() - 1));
    emit statusMessage(QString::fromUtf8("字库已原子保存：") + path);
    return true;
}

FontDictionaryDocument::DuplicateDecision FontDictionaryEditor::askDuplicateDecision(
        const FontDictionaryRecord& existing,
        const FontDictionaryRecord& incoming,
        bool batch,
        std::optional<FontDictionaryDocument::DuplicateDecision>* applyToAll) const {
    QMessageBox box(QMessageBox::Question,
                    QString::fromUtf8("点阵已存在"),
                    QString::fromUtf8("相同点阵当前对应“%1”，本次准备写入“%2”。")
                            .arg(existing.label, incoming.label),
                    QMessageBox::NoButton,
                    const_cast<FontDictionaryEditor*>(this));
    QPushButton* skip = box.addButton(QString::fromUtf8("跳过"), QMessageBox::RejectRole);
    QPushButton* replace = box.addButton(QString::fromUtf8("替换"), QMessageBox::AcceptRole);
    box.addButton(
            batch ? QString::fromUtf8("取消本批") : QString::fromUtf8("取消"),
            QMessageBox::DestructiveRole);
    QPushButton* skipAll = nullptr;
    QPushButton* replaceAll = nullptr;
    if (batch) {
        skipAll = box.addButton(QString::fromUtf8("全部跳过"), QMessageBox::ActionRole);
        replaceAll = box.addButton(QString::fromUtf8("全部替换"), QMessageBox::ActionRole);
    }
    box.setDefaultButton(skip);
    box.exec();
    if (box.clickedButton() == replace) return FontDictionaryDocument::DuplicateDecision::Replace;
    if (box.clickedButton() == skip) return FontDictionaryDocument::DuplicateDecision::Skip;
    if (box.clickedButton() == skipAll) {
        if (applyToAll != nullptr) *applyToAll = FontDictionaryDocument::DuplicateDecision::Skip;
        return FontDictionaryDocument::DuplicateDecision::Skip;
    }
    if (box.clickedButton() == replaceAll) {
        if (applyToAll != nullptr) *applyToAll = FontDictionaryDocument::DuplicateDecision::Replace;
        return FontDictionaryDocument::DuplicateDecision::Replace;
    }
    return FontDictionaryDocument::DuplicateDecision::Cancel;
}

void FontDictionaryEditor::selectRow(int row) {
    if (row < 0 || row >= dictionary_.rowCount()) {
        list_->clearSelection();
        list_->setCurrentIndex({});
        return;
    }
    list_->setCurrentIndex(dictionary_.index(row, 0));
}

} // namespace xiaoyv::tools
