/**
 * 文件用途：声明点阵提取、固定网格预览、字库事务编辑和当前图片测试面板。
 */
#pragma once

#include <QMetaObject>
#include <QPointer>
#include <QRect>
#include <QVector>
#include <QWidget>

#include "core/image_processing.h"
#include "model/font_dictionary_document.h"

class QCheckBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTableWidget;

namespace xiaoyv::tools {

class ImageDocument;
class FontDictionaryEditor;
class PixelGrid;
class RangeEditor;

class FontPanel final : public QWidget {
    Q_OBJECT

public:
    explicit FontPanel(QWidget* parent = nullptr);

    void setDocument(ImageDocument* document);
    void setSelectionRange(const QRect& selection);
    void setSelectionMode(bool enabled);
    /** 打开其他字库或退出程序前调用；返回 false 表示用户取消当前动作。 */
    bool confirmDiscardOrSaveChanges();

signals:
    void selectionRangeEdited(const QRect& selection);
    void selectionModeRequested(bool enabled);
    void statusMessage(const QString& message);

private:
    struct ExtractedRow {
        ExtractedGlyph glyph;
        QString label;
    };

    enum class PreviewSource {
        None,
        Extracted,
        Dictionary,
        Manual,
    };

    void syncColorRulesFromDocument();
    /** 清空旧提取结果和预览，保证切图或重新提取失败后不会继续显示过期点阵。 */
    void clearExtractionResults();
    void extractGlyphs();
    void rebuildExtractedTable();
    void showExtractedRow(int row);
    void showDictionaryRecord(const FontDictionaryRecord& record);
    void setPreview(
            const QString& label,
            int width,
            int height,
            std::vector<std::uint8_t> mask,
            PreviewSource source,
            int sourceRow);
    void updatePreviewText();
    void parseEditedPreviewText();
    FontDictionaryRecord previewRecord(QString* error = nullptr) const;
    QVector<FontDictionaryRecord> batchRecords(QString* error = nullptr) const;
    void testCurrentPixel();

    QPointer<ImageDocument> document_;
    QMetaObject::Connection selectionConnection_;
    QMetaObject::Connection colorPointsConnection_;
    QLineEdit* labelEdit_ = nullptr;
    QLineEdit* colorRulesEdit_ = nullptr;
    RangeEditor* rangeEditor_ = nullptr;
    QSpinBox* rowGapSpin_ = nullptr;
    QSpinBox* columnGapSpin_ = nullptr;
    QTableWidget* extractedTable_ = nullptr;
    QPlainTextEdit* pixelText_ = nullptr;
    QCheckBox* editableCheck_ = nullptr;
    PixelGrid* pixelGrid_ = nullptr;
    FontDictionaryEditor* dictionaryEditor_ = nullptr;
    QVector<ExtractedRow> extractedRows_;
    PreviewSource previewSource_ = PreviewSource::None;
    int previewSourceRow_ = -1;
    int previewWidth_ = 0;
    int previewHeight_ = 0;
    std::vector<std::uint8_t> previewMask_;
    bool updatingPreview_ = false;
    bool panelStateInitialized_ = false;
};

} // namespace xiaoyv::tools
