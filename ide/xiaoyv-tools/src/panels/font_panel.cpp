/**
 * 文件用途：实现完整点阵优先的提取流程，以及不会静默覆盖磁盘内容的字库编辑工作流。
 */
#include "panels/font_panel.h"

#include "model/color_point_model.h"
#include "model/image_document.h"
#include "panels/font_dictionary_editor.h"
#include "ui/pixel_grid.h"
#include "ui/range_editor.h"

#include <QCheckBox>
#include <QAbstractItemView>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace xiaoyv::tools {

FontPanel::FontPanel(QWidget* parent)
        : QWidget(parent) {
    labelEdit_ = new QLineEdit(this);
    labelEdit_->setPlaceholderText(QString::fromUtf8("文字或标签"));
    colorRulesEdit_ = new QLineEdit(this);
    colorRulesEdit_->setPlaceholderText(QStringLiteral("D9F9FF-505050|B9B1DA-000000"));
    colorRulesEdit_->setToolTip(QString::fromUtf8(
            "颜色规则：RRGGBB-偏色，多条用 | 分隔；取色点变化时会同步，也可手动修改"));
    rangeEditor_ = new RangeEditor(this);

    rowGapSpin_ = new QSpinBox(this);
    rowGapSpin_->setRange(1, 128);
    rowGapSpin_->setValue(1);
    rowGapSpin_->setToolTip(QString::fromUtf8("连续空白行达到该数量时切分文本行"));
    columnGapSpin_ = new QSpinBox(this);
    columnGapSpin_->setRange(1, 128);
    columnGapSpin_->setValue(1);
    columnGapSpin_->setToolTip(QString::fromUtf8("连续空白列达到该数量时切分字形"));
    auto* extractButton = new QPushButton(QString::fromUtf8("提取点阵"), this);
    extractButton->setObjectName(QStringLiteral("fontExtractButton"));
    auto* testButton = new QPushButton(QString::fromUtf8("当前图片测试"), this);
    auto* rangeWidget = new QWidget(this);
    auto* rangeLayout = new QHBoxLayout(rangeWidget);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    rangeLayout->setSpacing(4);
    rangeLayout->addWidget(rangeEditor_, 1);
    rangeLayout->addWidget(testButton);
    auto* gapWidget = new QWidget(this);
    auto* gapLayout = new QHBoxLayout(gapWidget);
    gapLayout->setContentsMargins(0, 0, 0, 0);
    gapLayout->setSpacing(4);
    gapLayout->addWidget(new QLabel(QString::fromUtf8("行"), gapWidget));
    gapLayout->addWidget(rowGapSpin_);
    gapLayout->addWidget(new QLabel(QString::fromUtf8("列"), gapWidget));
    gapLayout->addWidget(columnGapSpin_);
    gapLayout->addStretch();
    gapLayout->addWidget(extractButton);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(QString::fromUtf8("文字"), labelEdit_);
    form->addRow(QString::fromUtf8("颜色"), colorRulesEdit_);
    form->addRow(QString::fromUtf8("区域范围"), rangeWidget);
    form->addRow(QString::fromUtf8("分割间隔"), gapWidget);

    extractedTable_ = new QTableWidget(0, 4, this);
    extractedTable_->setObjectName(QStringLiteral("fontExtractedTable"));
    extractedTable_->setHorizontalHeaderLabels({
            QString::fromUtf8("序"), QString::fromUtf8("文字"),
            QString::fromUtf8("宽高"), QString::fromUtf8("点数")});
    extractedTable_->verticalHeader()->hide();
    extractedTable_->verticalHeader()->setDefaultSectionSize(24);
    extractedTable_->horizontalHeader()->setFixedHeight(23);
    extractedTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    extractedTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    extractedTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    extractedTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    extractedTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    extractedTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    extractedTable_->setMinimumHeight(116);

    pixelText_ = new QPlainTextEdit(this);
    pixelText_->setObjectName(QStringLiteral("fontPixelText"));
    pixelText_->setPlaceholderText(QString::fromUtf8("文字$宽$高$十六进制"));
    const QFontMetrics metrics(pixelText_->font());
    pixelText_->setMaximumHeight(metrics.lineSpacing() * 2 + 12);
    pixelGrid_ = new PixelGrid(this);
    auto* gridScroll = new QScrollArea(this);
    gridScroll->setObjectName(QStringLiteral("fontPixelGridScroll"));
    gridScroll->setWidget(pixelGrid_);
    gridScroll->setWidgetResizable(false);
    gridScroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    gridScroll->setMinimumHeight(78);
    editableCheck_ = new QCheckBox(QString::fromUtf8("允许编辑"), this);

    dictionaryEditor_ = new FontDictionaryEditor(this);
    dictionaryEditor_->setRecordProvider(
            [this](QString* error) { return previewRecord(error); });
    dictionaryEditor_->setBatchProvider(
            [this](QString* error) { return batchRecords(error); });

    auto* previewActions = new QHBoxLayout();
    previewActions->setContentsMargins(0, 0, 0, 0);
    previewActions->addWidget(editableCheck_);
    previewActions->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(5);
    layout->addLayout(form);
    layout->addWidget(extractedTable_);
    layout->addWidget(pixelText_);
    layout->addLayout(previewActions);
    layout->addWidget(gridScroll);
    layout->addWidget(dictionaryEditor_);

    connect(rangeEditor_, &RangeEditor::rangeEdited, this, [this](const QRect& range) {
        // 面板只提交范围；当前图片或下一张图片待用范围由上层唯一持有。
        emit selectionRangeEdited(range);
    });
    connect(rangeEditor_, &RangeEditor::selectionModeRequested,
            this, &FontPanel::selectionModeRequested);
    connect(rangeEditor_, &RangeEditor::inputRejected, this, &FontPanel::statusMessage);
    connect(extractButton, &QPushButton::clicked, this, &FontPanel::extractGlyphs);
    connect(testButton, &QPushButton::clicked, this, &FontPanel::testCurrentPixel);
    connect(extractedTable_, &QTableWidget::currentCellChanged, this,
            [this](int row) { if (row >= 0) showExtractedRow(row); });
    connect(dictionaryEditor_, &FontDictionaryEditor::recordSelected,
            this, &FontPanel::showDictionaryRecord);
    connect(dictionaryEditor_, &FontDictionaryEditor::statusMessage,
            this, &FontPanel::statusMessage);
    connect(labelEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (updatingPreview_) return;
        if (previewSource_ == PreviewSource::Extracted
                && previewSourceRow_ >= 0 && previewSourceRow_ < extractedRows_.size()) {
            extractedRows_[previewSourceRow_].label = text.trimmed();
            if (auto* edit = qobject_cast<QLineEdit*>(extractedTable_->cellWidget(previewSourceRow_, 1))) {
                const QSignalBlocker blocker(edit);
                edit->setText(text);
            }
        }
        updatePreviewText();
    });
    connect(pixelText_, &QPlainTextEdit::textChanged, this, &FontPanel::parseEditedPreviewText);
    connect(editableCheck_, &QCheckBox::toggled, pixelGrid_, &PixelGrid::setEditable);
    connect(pixelGrid_, &PixelGrid::maskChanged, this,
            [this](int width, int height, const std::vector<std::uint8_t>& mask) {
        previewWidth_ = width;
        previewHeight_ = height;
        previewMask_ = mask;
        if (previewSource_ == PreviewSource::Extracted
                && previewSourceRow_ >= 0 && previewSourceRow_ < extractedRows_.size()) {
            // 网格修正属于当前提取结果；批量入库必须使用修正后的点阵，而不是提取时旧副本。
            extractedRows_[previewSourceRow_].glyph.width = width;
            extractedRows_[previewSourceRow_].glyph.height = height;
            extractedRows_[previewSourceRow_].glyph.mask = mask;
        }
        updatePreviewText();
    });
}

void FontPanel::setDocument(ImageDocument* document) {
    if (document_ == document) return;
    disconnect(selectionConnection_);
    disconnect(colorPointsConnection_);
    document_ = document;
    if (document_ != nullptr) {
        if (!panelStateInitialized_) {
            colorRulesEdit_->setText(document_->colorPoints()->enabledColorRulesText());
            panelStateInitialized_ = true;
        }
        rangeEditor_->setRange(document_->selection());
        selectionConnection_ = connect(document_, &ImageDocument::selectionChanged, this,
                [this](const QRect& range) { rangeEditor_->setRange(range); });
        colorPointsConnection_ = connect(document_->colorPoints(), &ColorPointModel::pointsChanged,
                this, &FontPanel::syncColorRulesFromDocument);
    }
}

void FontPanel::setSelectionRange(const QRect& selection) {
    rangeEditor_->setRange(selection);
}

void FontPanel::setSelectionMode(bool enabled) {
    rangeEditor_->setSelectionMode(enabled);
}

bool FontPanel::confirmDiscardOrSaveChanges() {
    return dictionaryEditor_->confirmDiscardOrSaveChanges();
}

void FontPanel::syncColorRulesFromDocument() {
    colorRulesEdit_->setText(document_ == nullptr
            ? QString{}
            : document_->colorPoints()->enabledColorRulesText());
}

void FontPanel::clearExtractionResults() {
    const QSignalBlocker tableBlocker(extractedTable_);
    updatingPreview_ = true;
    extractedRows_.clear();
    extractedTable_->clearContents();
    extractedTable_->setRowCount(0);
    previewSource_ = PreviewSource::None;
    previewSourceRow_ = -1;
    previewWidth_ = 0;
    previewHeight_ = 0;
    previewMask_.clear();
    labelEdit_->clear();
    pixelText_->clear();
    pixelGrid_->setMask(0, 0, {});
    updatingPreview_ = false;
}

void FontPanel::extractGlyphs() {
    if (document_ == nullptr) {
        emit statusMessage(QString::fromUtf8("请先打开图片"));
        return;
    }
    // 一次提取就是一次完整刷新；先移除旧结果，失败时也不能继续展示上次的点阵。
    clearExtractionResults();
    std::vector<xiaoyv::image::ColorRule> rules;
    QString error;
    if (!parseColorRules(colorRulesEdit_->text(), &rules, &error)) {
        emit statusMessage(error);
        return;
    }
    std::vector<ExtractedGlyph> glyphs;
    if (!extractFontGlyphs(
            document_->image(), document_->selection(), rules,
            rowGapSpin_->value(), columnGapSpin_->value(), &glyphs, &error)) {
        emit statusMessage(error);
        return;
    }
    extractedRows_.reserve(static_cast<int>(glyphs.size()));
    for (ExtractedGlyph& glyph : glyphs) extractedRows_.push_back({std::move(glyph), {}});
    rebuildExtractedTable();
    if (extractedRows_.size() > 1) {
        // 第一行是未分割总览；默认打开首个分割字形，避免直接展示可能很大的总览网格。
        extractedTable_->setCurrentCell(1, 0);
    }
    emit statusMessage(QString::fromUtf8("已提取未分割点阵和 %1 个分割结果")
            .arg(extractedRows_.isEmpty() ? 0 : extractedRows_.size() - 1));
}

void FontPanel::rebuildExtractedTable() {
    const QSignalBlocker blocker(extractedTable_);
    extractedTable_->setRowCount(extractedRows_.size());
    auto makeReadOnlyItem = [](const QString& text) {
        auto* tableItem = new QTableWidgetItem(text);
        tableItem->setFlags(tableItem->flags() & ~Qt::ItemIsEditable);
        return tableItem;
    };
    for (int row = 0; row < extractedRows_.size(); ++row) {
        const ExtractedRow& item = extractedRows_[row];
        extractedTable_->setItem(row, 0, makeReadOnlyItem(
                item.glyph.overview ? QString::fromUtf8("总") : QString::number(row)));
        auto* edit = new QLineEdit(item.label, extractedTable_);
        edit->setPlaceholderText(QString::fromUtf8("文字"));
        connect(edit, &QLineEdit::textChanged, this, [this, row](const QString& text) {
            if (row >= extractedRows_.size()) return;
            extractedRows_[row].label = text.trimmed();
            if (previewSource_ == PreviewSource::Extracted && previewSourceRow_ == row) {
                const QSignalBlocker blocker(labelEdit_);
                labelEdit_->setText(text);
                updatePreviewText();
            }
        });
        extractedTable_->setCellWidget(row, 1, edit);
        extractedTable_->setItem(row, 2, makeReadOnlyItem(
                QStringLiteral("%1 x %2").arg(item.glyph.width).arg(item.glyph.height)));
        const auto pointCount = std::count(
                item.glyph.mask.cbegin(), item.glyph.mask.cend(), std::uint8_t{1});
        extractedTable_->setItem(row, 3, makeReadOnlyItem(QString::number(pointCount)));
    }
}

void FontPanel::showExtractedRow(int row) {
    if (row < 0 || row >= extractedRows_.size()) return;
    const ExtractedRow& item = extractedRows_[row];
    setPreview(item.label, item.glyph.width, item.glyph.height, item.glyph.mask,
               PreviewSource::Extracted, row);
}

void FontPanel::showDictionaryRecord(const FontDictionaryRecord& record) {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> mask;
    QString error;
    if (!decodeFontPixel(record.pixelBody(), &width, &height, &mask, nullptr, &error)) {
        emit statusMessage(error);
        return;
    }
    setPreview(record.label, width, height, std::move(mask), PreviewSource::Dictionary, -1);
}

void FontPanel::setPreview(
        const QString& label,
        int width,
        int height,
        std::vector<std::uint8_t> mask,
        PreviewSource source,
        int sourceRow) {
    updatingPreview_ = true;
    previewSource_ = source;
    previewSourceRow_ = sourceRow;
    previewWidth_ = width;
    previewHeight_ = height;
    previewMask_ = std::move(mask);
    labelEdit_->setText(label);
    pixelGrid_->setMask(previewWidth_, previewHeight_, previewMask_);
    const QString body = encodeFontPixel(previewWidth_, previewHeight_, previewMask_);
    pixelText_->setPlainText(label.trimmed().isEmpty() ? body : label.trimmed() + QLatin1Char('$') + body);
    updatingPreview_ = false;
}

void FontPanel::updatePreviewText() {
    if (updatingPreview_ || previewWidth_ <= 0 || previewHeight_ <= 0) return;
    const QString body = encodeFontPixel(previewWidth_, previewHeight_, previewMask_);
    updatingPreview_ = true;
    pixelText_->setPlainText(labelEdit_->text().trimmed().isEmpty()
            ? body
            : labelEdit_->text().trimmed() + QLatin1Char('$') + body);
    updatingPreview_ = false;
}

void FontPanel::parseEditedPreviewText() {
    if (updatingPreview_) return;
    int width = 0;
    int height = 0;
    QString label;
    std::vector<std::uint8_t> mask;
    if (!decodeFontPixel(pixelText_->toPlainText(), &width, &height, &mask, &label)) return;
    updatingPreview_ = true;
    previewSource_ = PreviewSource::Manual;
    previewSourceRow_ = -1;
    previewWidth_ = width;
    previewHeight_ = height;
    previewMask_ = std::move(mask);
    if (!label.isEmpty()) labelEdit_->setText(label);
    pixelGrid_->setMask(previewWidth_, previewHeight_, previewMask_);
    updatingPreview_ = false;
}

FontDictionaryRecord FontPanel::previewRecord(QString* error) const {
    const QString label = labelEdit_->text().trimmed();
    if (label.isEmpty()) {
        if (error != nullptr) *error = QString::fromUtf8("请填写文字");
        return {};
    }
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> mask;
    QString parseError;
    if (!decodeFontPixel(pixelText_->toPlainText(), &width, &height, &mask, nullptr, &parseError)) {
        if (error != nullptr) *error = parseError;
        return {};
    }
    FontDictionaryRecord result;
    if (!FontDictionaryDocument::parseRecord(
            label + QLatin1Char('$') + encodeFontPixel(width, height, mask), &result, error)) {
        return {};
    }
    if (error != nullptr) error->clear();
    return result;
}

QVector<FontDictionaryRecord> FontPanel::batchRecords(QString* error) const {
    QVector<FontDictionaryRecord> records;
    for (const ExtractedRow& row : extractedRows_) {
        if (row.label.trimmed().isEmpty()) continue;
        FontDictionaryRecord incoming;
        if (!FontDictionaryDocument::parseRecord(
                row.label.trimmed() + QLatin1Char('$') + row.glyph.pixelBody(),
                &incoming,
                error)) {
            return {};
        }
        records.push_back(std::move(incoming));
    }
    if (records.isEmpty()) {
        if (error != nullptr) {
            *error = QString::fromUtf8("请先为要加入字库的点阵填写文字");
        }
        return {};
    }
    if (error != nullptr) error->clear();
    return records;
}

void FontPanel::testCurrentPixel() {
    if (document_ == nullptr) {
        emit statusMessage(QString::fromUtf8("请先打开图片"));
        return;
    }
    int patternWidth = 0;
    int patternHeight = 0;
    std::vector<std::uint8_t> pattern;
    QString error;
    if (!decodeFontPixel(pixelText_->toPlainText(), &patternWidth, &patternHeight, &pattern, nullptr, &error)) {
        emit statusMessage(error);
        return;
    }
    std::vector<xiaoyv::image::ColorRule> rules;
    if (!parseColorRules(colorRulesEdit_->text(), &rules, &error)) {
        emit statusMessage(error);
        return;
    }
    std::vector<std::uint8_t> imageMask;
    int imageWidth = 0;
    int imageHeight = 0;
    QRect actual;
    if (!makeColorMask(document_->image(), {}, rules,
                       &imageMask, &imageWidth, &imageHeight, &actual, &error)) {
        emit statusMessage(error);
        return;
    }
    const auto matches = xiaoyv::image::findBinaryPattern(
            imageMask.data(), imageWidth, imageHeight,
            pattern.data(), patternWidth, patternHeight, 1.0);
    if (matches.empty()) {
        emit statusMessage(QString::fromUtf8("当前图片未找到该点阵"));
    } else {
        emit statusMessage(QString::fromUtf8("命中 %1 处，首个坐标：%2,%3")
                .arg(matches.size()).arg(matches.front().x).arg(matches.front().y));
    }
}

} // namespace xiaoyv::tools
