/**
 * 文件用途：声明点阵字库文档，统一维护格式、点阵唯一性、事务编辑、外部合并和原子保存。
 */
#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace xiaoyv::tools {

struct FontDictionaryRecord {
    QString label;
    int width = 0;
    int height = 0;
    QByteArray hex;

    QString pixelBody() const;
    QString serialized() const;
    QString pixelKey() const;
    bool hasSamePixels(const FontDictionaryRecord& other) const;
    bool operator==(const FontDictionaryRecord& other) const = default;
};

class FontDictionaryDocument final : public QAbstractListModel {
    Q_OBJECT

public:
    enum class DuplicateDecision {
        Skip,
        Replace,
        Cancel,
    };

    enum class WriteResult {
        Added,
        Replaced,
        Skipped,
        Cancelled,
        Invalid,
    };

    using ConflictResolver = std::function<DuplicateDecision(
            const FontDictionaryRecord& existing,
            const FontDictionaryRecord& incoming,
            int existingRow)>;

    explicit FontDictionaryDocument(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

    const QVector<FontDictionaryRecord>& records() const;
    QString filePath() const;
    bool isModified() const;
    const QStringList& loadWarnings() const;

    static bool parseRecord(
            const QString& text,
            FontDictionaryRecord* record,
            QString* error = nullptr);
    static int findSamePixels(
            const QVector<FontDictionaryRecord>& records,
            const FontDictionaryRecord& incoming,
            int excludedRow = -1);
    static WriteResult addTo(
            QVector<FontDictionaryRecord>* records,
            const FontDictionaryRecord& incoming,
            DuplicateDecision decision,
            int* affectedRow = nullptr);
    static WriteResult replaceIn(
            QVector<FontDictionaryRecord>* records,
            int selectedRow,
            const FontDictionaryRecord& incoming,
            DuplicateDecision decision,
            int* affectedRow = nullptr);

    bool load(const QString& path, QString* error = nullptr);
    bool save(const QString& path, ConflictResolver resolver, QString* error = nullptr);
    void revert();

    WriteResult add(
            const FontDictionaryRecord& record,
            DuplicateDecision decision,
            int* affectedRow = nullptr);
    WriteResult replace(
            int selectedRow,
            const FontDictionaryRecord& record,
            DuplicateDecision decision,
            int* affectedRow = nullptr);
    bool remove(int row);
    /** 批量操作完成后一次性提交，调用方取消时不调用本函数即可保持原状态。 */
    bool commitTransaction(QVector<FontDictionaryRecord> records, QString* error = nullptr);

signals:
    void modifiedChanged(bool modified);

private:
    /** 整体替换字库记录并正确通知所有绑定视图。 */
    void replaceRecords(QVector<FontDictionaryRecord> records);
    void setModified(bool modified);
    void replaceState(
            QVector<FontDictionaryRecord> records,
            const QString& path,
            QStringList warnings,
            bool modified);

    QVector<FontDictionaryRecord> records_;
    QVector<FontDictionaryRecord> baselineRecords_;
    QString filePath_;
    QStringList loadWarnings_;
    bool modified_ = false;
};

} // namespace xiaoyv::tools
