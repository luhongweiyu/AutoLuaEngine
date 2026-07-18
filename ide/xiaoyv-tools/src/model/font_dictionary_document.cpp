/**
 * 文件用途：实现点阵字库的不变量校验、磁盘外部修改合并和 QSaveFile 原子保存。
 */
#include "model/font_dictionary_document.h"

#include "core/image_processing.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <optional>

namespace xiaoyv::tools {
namespace {

struct FileLine {
    QString raw;
    std::optional<FontDictionaryRecord> record;
};

struct ParsedFile {
    QVector<FileLine> lines;
    QVector<FontDictionaryRecord> records;
    QStringList warnings;
};

bool validateUniquePixels(const QVector<FontDictionaryRecord>& records, QString* error) {
    QHash<QString, int> rows;
    for (int row = 0; row < records.size(); ++row) {
        const QString key = records[row].pixelKey();
        if (rows.contains(key)) {
            if (error != nullptr) {
                *error = QString::fromUtf8("字库第 %1 行和第 %2 行使用了相同点阵")
                        .arg(rows.value(key) + 1)
                        .arg(row + 1);
            }
            return false;
        }
        rows.insert(key, row);
    }
    return true;
}

bool readDictionaryFile(const QString& path, ParsedFile* result, QString* error) {
    if (result == nullptr) {
        if (error != nullptr) *error = QString::fromUtf8("字库读取结果对象为空");
        return false;
    }
    result->lines.clear();
    result->records.clear();
    result->warnings.clear();

    QFile file(path);
    if (!file.exists()) {
        if (error != nullptr) error->clear();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error != nullptr) *error = file.errorString();
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    int lineNumber = 0;
    while (!stream.atEnd()) {
        ++lineNumber;
        const QString raw = stream.readLine();
        FontDictionaryRecord record;
        QString parseError;
        if (FontDictionaryDocument::parseRecord(raw, &record, &parseError)) {
            result->lines.push_back({raw, record});
            result->records.push_back(std::move(record));
            continue;
        }
        result->lines.push_back({raw, std::nullopt});
        if (!raw.trimmed().isEmpty() && raw.contains(QLatin1Char('$'))) {
            result->warnings.push_back(QString::fromUtf8("第 %1 行未识别，保存时原样保留：%2")
                    .arg(lineNumber)
                    .arg(parseError));
        }
    }
    if (!validateUniquePixels(result->records, error)) return false;
    if (error != nullptr) error->clear();
    return true;
}

int findLineByPixelKey(const QVector<FileLine>& lines, const QString& key) {
    for (int index = 0; index < lines.size(); ++index) {
        if (lines[index].record.has_value()
                && lines[index].record->pixelKey() == key) {
            return index;
        }
    }
    return -1;
}

int findRecordByPixelKey(const QVector<FontDictionaryRecord>& records, const QString& key) {
    for (int index = 0; index < records.size(); ++index) {
        if (records[index].pixelKey() == key) return index;
    }
    return -1;
}

QVector<FontDictionaryRecord> recordsFromLines(const QVector<FileLine>& lines) {
    QVector<FontDictionaryRecord> result;
    for (const FileLine& line : lines) {
        if (line.record.has_value()) result.push_back(*line.record);
    }
    return result;
}

} // namespace

QString FontDictionaryRecord::pixelBody() const {
    return QStringLiteral("%1$%2$%3")
            .arg(width)
            .arg(height)
            .arg(QString::fromLatin1(hex));
}

QString FontDictionaryRecord::serialized() const {
    return label + QLatin1Char('$') + pixelBody();
}

QString FontDictionaryRecord::pixelKey() const {
    return pixelBody();
}

bool FontDictionaryRecord::hasSamePixels(const FontDictionaryRecord& other) const {
    return width == other.width && height == other.height && hex == other.hex;
}

FontDictionaryDocument::FontDictionaryDocument(QObject* parent)
        : QAbstractListModel(parent) {}

int FontDictionaryDocument::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : records_.size();
}

QVariant FontDictionaryDocument::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= records_.size()) return {};
    const FontDictionaryRecord& record = records_[index.row()];
    if (role == Qt::DisplayRole) {
        const QString preview = QString::fromLatin1(record.hex.left(18));
        return QStringLiteral("%1    %2 x %3    %4%5")
                .arg(record.label)
                .arg(record.width)
                .arg(record.height)
                .arg(preview)
                .arg(record.hex.size() > 18 ? QStringLiteral("...") : QString{});
    }
    if (role == Qt::ToolTipRole) return record.serialized();
    return {};
}

const QVector<FontDictionaryRecord>& FontDictionaryDocument::records() const {
    return records_;
}

QString FontDictionaryDocument::filePath() const {
    return filePath_;
}

bool FontDictionaryDocument::isModified() const {
    return modified_;
}

const QStringList& FontDictionaryDocument::loadWarnings() const {
    return loadWarnings_;
}

bool FontDictionaryDocument::parseRecord(
        const QString& text,
        FontDictionaryRecord* record,
        QString* error) {
    if (record == nullptr) {
        if (error != nullptr) *error = QString::fromUtf8("字库记录输出对象为空");
        return false;
    }
    int width = 0;
    int height = 0;
    QString label;
    std::vector<std::uint8_t> mask;
    if (!decodeFontPixel(text, &width, &height, &mask, &label, error) || label.isEmpty()) {
        if (error != nullptr && error->isEmpty()) *error = QString::fromUtf8("字库记录缺少文字");
        return false;
    }
    if (label.contains(QLatin1Char('$'))) {
        if (error != nullptr) *error = QString::fromUtf8("文字不能包含 $ 字符");
        return false;
    }
    FontDictionaryRecord parsed;
    parsed.label = label;
    parsed.width = width;
    parsed.height = height;
    parsed.hex = encodeFontPixel(width, height, mask).section(QLatin1Char('$'), 2).toLatin1();
    *record = std::move(parsed);
    if (error != nullptr) error->clear();
    return true;
}

int FontDictionaryDocument::findSamePixels(
        const QVector<FontDictionaryRecord>& records,
        const FontDictionaryRecord& incoming,
        int excludedRow) {
    for (int row = 0; row < records.size(); ++row) {
        if (row != excludedRow && records[row].hasSamePixels(incoming)) return row;
    }
    return -1;
}

FontDictionaryDocument::WriteResult FontDictionaryDocument::addTo(
        QVector<FontDictionaryRecord>* records,
        const FontDictionaryRecord& incoming,
        DuplicateDecision decision,
        int* affectedRow) {
    if (records == nullptr || incoming.label.trimmed().isEmpty()
            || incoming.width <= 0 || incoming.height <= 0 || incoming.hex.isEmpty()) {
        return WriteResult::Invalid;
    }
    const int duplicateRow = findSamePixels(*records, incoming);
    if (duplicateRow < 0) {
        records->push_back(incoming);
        if (affectedRow != nullptr) *affectedRow = records->size() - 1;
        return WriteResult::Added;
    }
    if ((*records)[duplicateRow].label == incoming.label || decision == DuplicateDecision::Skip) {
        if (affectedRow != nullptr) *affectedRow = duplicateRow;
        return WriteResult::Skipped;
    }
    if (decision == DuplicateDecision::Cancel) return WriteResult::Cancelled;
    (*records)[duplicateRow] = incoming;
    if (affectedRow != nullptr) *affectedRow = duplicateRow;
    return WriteResult::Replaced;
}

FontDictionaryDocument::WriteResult FontDictionaryDocument::replaceIn(
        QVector<FontDictionaryRecord>* records,
        int selectedRow,
        const FontDictionaryRecord& incoming,
        DuplicateDecision decision,
        int* affectedRow) {
    if (records == nullptr || selectedRow < 0 || selectedRow >= records->size()) {
        return WriteResult::Invalid;
    }
    const int duplicateRow = findSamePixels(*records, incoming, selectedRow);
    if (duplicateRow >= 0) {
        if ((*records)[duplicateRow].label == incoming.label || decision == DuplicateDecision::Skip) {
            if (affectedRow != nullptr) *affectedRow = duplicateRow;
            return WriteResult::Skipped;
        }
        if (decision == DuplicateDecision::Cancel) return WriteResult::Cancelled;

        // 用户选择替换冲突点阵时，删除冲突记录，再把当前选中行改为新记录，保持点阵唯一。
        records->removeAt(duplicateRow);
        if (duplicateRow < selectedRow) --selectedRow;
    }
    (*records)[selectedRow] = incoming;
    if (affectedRow != nullptr) *affectedRow = selectedRow;
    return WriteResult::Replaced;
}

bool FontDictionaryDocument::load(const QString& path, QString* error) {
    ParsedFile parsed;
    if (!readDictionaryFile(path, &parsed, error)) return false;
    replaceState(std::move(parsed.records), path, std::move(parsed.warnings), false);
    baselineRecords_ = records_;
    return true;
}

bool FontDictionaryDocument::save(
        const QString& path,
        ConflictResolver resolver,
        QString* error) {
    if (path.trimmed().isEmpty()) {
        if (error != nullptr) *error = QString::fromUtf8("字库保存路径为空");
        return false;
    }
    if (!validateUniquePixels(records_, error)) return false;

    ParsedFile disk;
    if (!readDictionaryFile(path, &disk, error)) return false;

    // 先应用本地删除。磁盘对应记录已被外部改名时保留该外部修改，避免静默删除他人内容。
    for (const FontDictionaryRecord& baseline : baselineRecords_) {
        if (findRecordByPixelKey(records_, baseline.pixelKey()) >= 0) continue;
        const int line = findLineByPixelKey(disk.lines, baseline.pixelKey());
        if (line >= 0 && disk.lines[line].record->label == baseline.label) {
            disk.lines.removeAt(line);
        }
    }

    for (const FontDictionaryRecord& local : records_) {
        const QString key = local.pixelKey();
        const int line = findLineByPixelKey(disk.lines, key);
        if (line < 0) {
            disk.lines.push_back({local.serialized(), local});
            continue;
        }
        const FontDictionaryRecord existing = *disk.lines[line].record;
        if (existing.label == local.label) continue;

        const int baselineRow = findRecordByPixelKey(baselineRecords_, key);
        const bool diskStillBaseline = baselineRow >= 0
                && baselineRecords_[baselineRow].label == existing.label;
        DuplicateDecision decision = DuplicateDecision::Replace;
        if (!diskStillBaseline) {
            decision = resolver
                    ? resolver(existing, local, line)
                    : DuplicateDecision::Cancel;
        }
        if (decision == DuplicateDecision::Cancel) {
            if (error != nullptr) *error = QString::fromUtf8("已取消保存字库");
            return false;
        }
        if (decision == DuplicateDecision::Skip) continue;
        disk.lines[line].raw = local.serialized();
        disk.lines[line].record = local;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error != nullptr) *error = file.errorString();
        return false;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    for (const FileLine& line : disk.lines) {
        stream << (line.record.has_value() ? line.record->serialized() : line.raw) << '\n';
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok || !file.commit()) {
        if (error != nullptr) *error = file.errorString();
        return false;
    }

    QVector<FontDictionaryRecord> merged = recordsFromLines(disk.lines);
    replaceState(std::move(merged), path, std::move(disk.warnings), false);
    baselineRecords_ = records_;
    if (error != nullptr) error->clear();
    return true;
}

void FontDictionaryDocument::revert() {
    if (!modified_) return;
    replaceRecords(baselineRecords_);
    setModified(false);
}

FontDictionaryDocument::WriteResult FontDictionaryDocument::add(
        const FontDictionaryRecord& record,
        DuplicateDecision decision,
        int* affectedRow) {
    QVector<FontDictionaryRecord> updated = records_;
    const WriteResult result = addTo(&updated, record, decision, affectedRow);
    if (result == WriteResult::Added || result == WriteResult::Replaced) {
        replaceRecords(std::move(updated));
        setModified(true);
    }
    return result;
}

FontDictionaryDocument::WriteResult FontDictionaryDocument::replace(
        int selectedRow,
        const FontDictionaryRecord& record,
        DuplicateDecision decision,
        int* affectedRow) {
    QVector<FontDictionaryRecord> updated = records_;
    const WriteResult result = replaceIn(&updated, selectedRow, record, decision, affectedRow);
    if (result == WriteResult::Replaced) {
        replaceRecords(std::move(updated));
        setModified(true);
    }
    return result;
}

bool FontDictionaryDocument::remove(int row) {
    if (row < 0 || row >= records_.size()) return false;
    beginRemoveRows({}, row, row);
    records_.removeAt(row);
    endRemoveRows();
    setModified(true);
    return true;
}

bool FontDictionaryDocument::commitTransaction(
        QVector<FontDictionaryRecord> records,
        QString* error) {
    if (!validateUniquePixels(records, error)) return false;
    if (records == records_) return true;
    replaceRecords(std::move(records));
    setModified(true);
    if (error != nullptr) error->clear();
    return true;
}

void FontDictionaryDocument::replaceRecords(QVector<FontDictionaryRecord> records) {
    beginResetModel();
    records_ = std::move(records);
    endResetModel();
}

void FontDictionaryDocument::setModified(bool modified) {
    if (modified_ == modified) return;
    modified_ = modified;
    emit modifiedChanged(modified_);
}

void FontDictionaryDocument::replaceState(
        QVector<FontDictionaryRecord> records,
        const QString& path,
        QStringList warnings,
        bool modified) {
    replaceRecords(std::move(records));
    filePath_ = path;
    loadWarnings_ = std::move(warnings);
    setModified(modified);
}

} // namespace xiaoyv::tools
