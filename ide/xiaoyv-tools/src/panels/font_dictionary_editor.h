/**
 * 文件用途：声明点阵字库编辑器，独占字库文件、冲突事务、列表和保存交互。
 */
#pragma once

#include "model/font_dictionary_document.h"

#include <QWidget>

#include <functional>
#include <optional>

class QLineEdit;
class QListView;

namespace xiaoyv::tools {

class FontDictionaryEditor final : public QWidget {
    Q_OBJECT

public:
    using RecordProvider = std::function<FontDictionaryRecord(QString* error)>;
    using BatchProvider = std::function<QVector<FontDictionaryRecord>(QString* error)>;

    explicit FontDictionaryEditor(QWidget* parent = nullptr);

    void setRecordProvider(RecordProvider provider);
    void setBatchProvider(BatchProvider provider);
    /** 打开其他字库或退出程序前调用；返回 false 表示用户取消当前动作。 */
    bool confirmDiscardOrSaveChanges();

signals:
    void recordSelected(const FontDictionaryRecord& record);
    void statusMessage(const QString& message);

private:
    void addCurrentRecord();
    void addBatchRecords();
    void replaceSelectedRecord();
    void deleteSelectedRecord();
    void openDictionary();
    bool saveDictionary();
    FontDictionaryDocument::DuplicateDecision askDuplicateDecision(
            const FontDictionaryRecord& existing,
            const FontDictionaryRecord& incoming,
            bool batch,
            std::optional<FontDictionaryDocument::DuplicateDecision>* applyToAll = nullptr) const;
    void selectRow(int row);

    FontDictionaryDocument dictionary_;
    RecordProvider recordProvider_;
    BatchProvider batchProvider_;
    QLineEdit* pathEdit_ = nullptr;
    QListView* list_ = nullptr;
};

} // namespace xiaoyv::tools
