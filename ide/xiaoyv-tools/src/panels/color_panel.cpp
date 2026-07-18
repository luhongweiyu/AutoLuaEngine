/**
 * 文件用途：实现不限数量取色点、逐行删除、基准点、刷新和单语言格式生成工作流。
 */
#include "panels/color_panel.h"

#include "generator/generator_engine.h"
#include "model/color_point_model.h"
#include "model/image_document.h"
#include "ui/range_editor.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QFontMetrics>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>

namespace xiaoyv::tools {
namespace {

class ColorSwatchDelegate final : public QStyledItemDelegate {
public:
    explicit ColorSwatchDelegate(QObject* parent = nullptr)
            : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem prepared(option);
        initStyleOption(&prepared, index);
        prepared.text.clear();
        prepared.icon = {};
        prepared.features &= ~QStyleOptionViewItem::HasDecoration;
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &prepared, painter);

        const QColor color = index.data(Qt::DecorationRole).value<QColor>();
        if (!color.isValid()) return;
        const QRect swatch = option.rect.adjusted(4, 3, -4, -3);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->fillRect(swatch, color);
        painter->setPen(option.palette.color(QPalette::Mid));
        painter->drawRect(swatch.adjusted(0, 0, -1, -1));
        painter->restore();
    }
};

class HexColorDelegate final : public QStyledItemDelegate {
public:
    explicit HexColorDelegate(QObject* parent = nullptr)
            : QStyledItemDelegate(parent) {}

    QWidget* createEditor(
            QWidget* parent,
            const QStyleOptionViewItem&,
            const QModelIndex&) const override {
        // 单颜色单元格允许 RRGGBB、#RRGGBB 或 0xRRGGBB，最长均不超过 8 个字符。
        auto* editor = new QLineEdit(parent);
        editor->setMaxLength(8);
        return editor;
    }
};

class BaseDeleteDelegate final : public QStyledItemDelegate {
public:
    explicit BaseDeleteDelegate(QObject* parent = nullptr)
            : QStyledItemDelegate(parent) {}

    std::function<void(int)> setBase;
    std::function<void(int)> remove;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem prepared(option);
        initStyleOption(&prepared, index);
        prepared.text.clear();
        prepared.features &= ~QStyleOptionViewItem::HasCheckIndicator;
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &prepared, painter);

        const QRect checkRect(option.rect.left() + 7, option.rect.center().y() - 7, 14, 14);
        QStyleOptionButton check;
        check.rect = checkRect;
        check.state = QStyle::State_Enabled
                | (index.data(Qt::CheckStateRole).toInt() == Qt::Checked
                           ? QStyle::State_On : QStyle::State_Off);
        QApplication::style()->drawControl(QStyle::CE_CheckBox, &check, painter);

        const QRect closeRect(option.rect.right() - 20, option.rect.center().y() - 8, 16, 16);
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setPen(QPen(
                option.palette.color(QPalette::Text),
                1.0,
                Qt::SolidLine,
                Qt::SquareCap,
                Qt::MiterJoin));
        painter->drawLine(closeRect.topLeft() + QPoint(4, 4), closeRect.bottomRight() - QPoint(4, 4));
        painter->drawLine(closeRect.topRight() + QPoint(-4, 4), closeRect.bottomLeft() + QPoint(4, -4));
    }

    bool editorEvent(
            QEvent* event,
            QAbstractItemModel*,
            const QStyleOptionViewItem& option,
            const QModelIndex& index) override {
        if (event->type() != QEvent::MouseButtonRelease) return false;
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() != Qt::LeftButton) return false;
        const QRect closeRect(option.rect.right() - 22, option.rect.top(), 22, option.rect.height());
        if (closeRect.contains(mouse->position().toPoint())) {
            if (remove) remove(index.row());
        } else if (setBase) {
            setBase(index.row());
        }
        return true;
    }
};

} // namespace

ColorPanel::ColorPanel(GeneratorEngine* generator, QWidget* parent)
        : QWidget(parent), generator_(generator) {
    rangeEditor_ = new RangeEditor(this);
    formatCombo_ = new QComboBox(this);
    directionCombo_ = new QComboBox(this);
    for (int direction = 1; direction <= 8; ++direction) {
        directionCombo_->addItem(QString::number(direction), direction);
    }
    defaultDeltaEdit_ = new QLineEdit(QStringLiteral("000000"), this);
    defaultDeltaEdit_->setMaxLength(8);
    defaultDeltaEdit_->setToolTip(QString::fromUtf8("m.findColors 的默认偏色，格式为 RRGGBB"));

    table_ = new QTableView(this);
    table_->setObjectName(QStringLiteral("colorPointTable"));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(22);
    table_->horizontalHeader()->setFixedHeight(21);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->setMinimumHeight(160);

    table_->setItemDelegateForColumn(
            ColorPointModel::SwatchColumn,
            new ColorSwatchDelegate(table_));
    auto* hexDelegate = new HexColorDelegate(table_);
    table_->setItemDelegateForColumn(ColorPointModel::HexColumn, hexDelegate);
    table_->setItemDelegateForColumn(ColorPointModel::DeltaColumn, hexDelegate);

    auto* delegate = new BaseDeleteDelegate(table_);
    delegate->setBase = [this](int row) {
        if (document_ != nullptr) document_->colorPoints()->setBase(row);
    };
    delegate->remove = [this](int row) {
        if (document_ != nullptr) document_->colorPoints()->removePoint(row);
    };
    table_->setItemDelegateForColumn(ColorPointModel::BaseColumn, delegate);

    auto* refreshSelectedButton = new QPushButton(QString::fromUtf8("刷新选中"), this);
    auto* refreshAllButton = new QPushButton(QString::fromUtf8("刷新全部"), this);
    auto* clearButton = new QPushButton(QString::fromUtf8("清空"), this);
    auto* generateButton = new QPushButton(QString::fromUtf8("生成代码"), this);
    auto* runButton = new QPushButton(QString::fromUtf8("发送运行"), this);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(QString::fromUtf8("框选范围"), rangeEditor_);
    form->addRow(QString::fromUtf8("生成格式"), formatCombo_);
    auto* options = new QWidget(this);
    auto* optionsLayout = new QHBoxLayout(options);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(4);
    optionsLayout->addWidget(directionCombo_);
    optionsLayout->addWidget(defaultDeltaEdit_, 1);
    form->addRow(QString::fromUtf8("方向 / 偏色"), options);

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(4);
    actions->addWidget(refreshSelectedButton);
    actions->addWidget(refreshAllButton);
    actions->addWidget(clearButton);
    actions->addStretch();
    actions->addWidget(generateButton);
    actions->addWidget(runButton);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(5);
    layout->addLayout(form);
    layout->addWidget(table_, 1);
    layout->addLayout(actions);

    connect(rangeEditor_, &RangeEditor::rangeEdited, this, [this](const QRect& range) {
        // 框选状态只由当前 ImageDocument 或主窗口的待用范围持有；面板仅提交用户输入。
        emit selectionRangeEdited(range);
    });
    connect(rangeEditor_, &RangeEditor::selectionModeRequested,
            this, &ColorPanel::selectionModeRequested);
    connect(rangeEditor_, &RangeEditor::inputRejected, this, &ColorPanel::statusMessage);
    connect(refreshSelectedButton, &QPushButton::clicked, this, &ColorPanel::refreshSelected);
    connect(refreshAllButton, &QPushButton::clicked, this, &ColorPanel::refreshAll);
    connect(clearButton, &QPushButton::clicked, this, [this] {
        if (document_ != nullptr) document_->colorPoints()->clear();
    });
    connect(generateButton, &QPushButton::clicked, this, [this] { generateCode(false); });
    connect(runButton, &QPushButton::clicked, this, [this] { generateCode(true); });
    connect(table_, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if (document_ == nullptr || index.column() != ColorPointModel::SwatchColumn) return;
        const ColorPoint* point = document_->colorPoints()->point(index.row());
        if (point == nullptr) return;
        const QColor selected = QColorDialog::getColor(
                point->color, this, QString::fromUtf8("选择取色点颜色"));
        if (selected.isValid()) document_->colorPoints()->setColor(index.row(), selected);
    });
    connect(generator_, &GeneratorEngine::formatsChanged, this, &ColorPanel::rebuildFormats);
    rebuildFormats();
}

void ColorPanel::setDocument(ImageDocument* document) {
    if (document_ == document) return;
    disconnect(selectionConnection_);
    document_ = document;
    table_->setModel(document_ == nullptr ? nullptr : document_->colorPoints());
    if (document_ != nullptr) {
        rangeEditor_->setRange(document_->selection());
        selectionConnection_ = connect(document_, &ImageDocument::selectionChanged, this,
                [this](const QRect& range) { rangeEditor_->setRange(range); });
        const QFontMetrics metrics(table_->font());
        const int indicatorWidth = table_->style()->pixelMetric(QStyle::PM_IndicatorWidth);
        const int indicatorSpacing = table_->style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing);
        const int sequenceWidth = indicatorWidth + indicatorSpacing
                + metrics.horizontalAdvance(QStringLiteral("99")) + 6;
        const int coordinateWidth = metrics.horizontalAdvance(QStringLiteral("9999,9999 ")) + 6;
        const int colorWidth = metrics.horizontalAdvance(QStringLiteral("RRGGBB ")) + 6;
        const int baseWidth = indicatorWidth + indicatorSpacing
                + metrics.horizontalAdvance(QString::fromUtf8("基准")) + 6;

        auto* header = table_->horizontalHeader();
        for (int column = 0; column < ColorPointModel::ColumnCount; ++column) {
            header->setSectionResizeMode(column, QHeaderView::Fixed);
        }
        table_->setColumnWidth(ColorPointModel::SequenceColumn, sequenceWidth);
        table_->setColumnWidth(ColorPointModel::SwatchColumn, 24);
        table_->setColumnWidth(ColorPointModel::CoordinateColumn, coordinateWidth);
        table_->setColumnWidth(ColorPointModel::HexColumn, colorWidth);
        table_->setColumnWidth(ColorPointModel::DeltaColumn, colorWidth);
        table_->setColumnWidth(ColorPointModel::BaseColumn, baseWidth);
    }
}

void ColorPanel::setSelectionRange(const QRect& selection) {
    rangeEditor_->setRange(selection);
}

void ColorPanel::setSelectionMode(bool enabled) {
    rangeEditor_->setSelectionMode(enabled);
}

void ColorPanel::reloadFormats() {
    generator_->reload();
    if (!generator_->loadErrors().isEmpty()) {
        emit statusMessage(QString::fromUtf8("部分生成格式加载失败：")
                + generator_->loadErrors().join(QString::fromUtf8("；")));
    } else {
        emit statusMessage(QString::fromUtf8("生成格式已重新加载"));
    }
}

void ColorPanel::rebuildFormats() {
    const QString previous = formatCombo_->currentData().toString();
    formatCombo_->clear();
    for (const GeneratorFormat& format : generator_->formats()) {
        formatCombo_->addItem(format.name, format.id);
    }
    const int previousIndex = formatCombo_->findData(previous);
    if (previousIndex >= 0) {
        formatCombo_->setCurrentIndex(previousIndex);
    } else {
        const int preferred = formatCombo_->findData(QStringLiteral("m-find-colors-lua"));
        if (preferred >= 0) formatCombo_->setCurrentIndex(preferred);
    }
}

void ColorPanel::refreshSelected() {
    if (document_ == nullptr) {
        emit statusMessage(QString::fromUtf8("请先打开图片"));
        return;
    }
    const int row = table_->currentIndex().row();
    if (row < 0) {
        emit statusMessage(QString::fromUtf8("请先选择一个取色点"));
        return;
    }
    if (!document_->colorPoints()->refreshPoint(row, document_->displayedImage())) {
        emit statusMessage(QString::fromUtf8("该取色点已超出当前图片范围"));
        return;
    }
    emit statusMessage(QString::fromUtf8("已刷新选中取色点"));
}

void ColorPanel::refreshAll() {
    if (document_ == nullptr) {
        emit statusMessage(QString::fromUtf8("请先打开图片"));
        return;
    }
    const int count = document_->colorPoints()->refreshAll(document_->displayedImage());
    emit statusMessage(QString::fromUtf8("已刷新 %1 个有效取色点").arg(count));
}

void ColorPanel::generateCode(bool runAfterGenerate) {
    QString error;
    const QVariantMap context = buildContext(&error);
    if (!error.isEmpty()) {
        emit statusMessage(error);
        return;
    }
    const QString formatId = formatCombo_->currentData().toString();
    const QString code = generator_->generate(formatId, context, &error);
    if (!error.isEmpty()) {
        emit statusMessage(QString::fromUtf8("生成代码失败：") + error);
        return;
    }
    const QString language = currentLanguage();
    emit codeGenerated(language, code);
    if (runAfterGenerate) emit runRequested(language, code);
}

QVariantMap ColorPanel::buildContext(QString* error) const {
    if (document_ == nullptr || document_->colorPoints()->effectiveBaseRow() < 0) {
        if (error != nullptr) *error = QString::fromUtf8("请先在图片上采集至少一个启用的取色点");
        return {};
    }
    QColor defaultDelta;
    if (!parseRgbHex(defaultDeltaEdit_->text(), &defaultDelta)) {
        if (error != nullptr) *error = QString::fromUtf8("默认偏色格式应为 RRGGBB");
        return {};
    }
    const int baseRow = document_->colorPoints()->effectiveBaseRow();
    const QPoint basePosition = document_->colorPoints()->points()[baseRow].position;
    QVariantList points;
    auto appendPoint = [&](int row) {
        const ColorPoint& point = document_->colorPoints()->points()[row];
        points.push_back(QVariantMap{
                {QStringLiteral("enabled"), point.enabled},
                {QStringLiteral("base"), row == baseRow},
                {QStringLiteral("x"), point.position.x()},
                {QStringLiteral("y"), point.position.y()},
                {QStringLiteral("dx"), point.position.x() - basePosition.x()},
                {QStringLiteral("dy"), point.position.y() - basePosition.y()},
                {QStringLiteral("hex"), colorToHex(point.color)},
                {QStringLiteral("delta"), colorToHex(point.delta)},
        });
    };
    appendPoint(baseRow);
    for (int row = 0; row < document_->colorPoints()->points().size(); ++row) {
        if (row != baseRow) appendPoint(row);
    }

    const QRect range = document_->selection().isNull()
            ? document_->image().rect()
            : document_->selection();
    if (error != nullptr) error->clear();
    return {
            {QStringLiteral("points"), points},
            {QStringLiteral("region"), QVariantMap{
                    {QStringLiteral("left"), range.left()},
                    {QStringLiteral("top"), range.top()},
                    {QStringLiteral("right"), range.right()},
                    {QStringLiteral("bottom"), range.bottom()},
            }},
            {QStringLiteral("image"), QVariantMap{
                    {QStringLiteral("width"), document_->image().width()},
                    {QStringLiteral("height"), document_->image().height()},
                    {QStringLiteral("path"), document_->filePath()},
            }},
            {QStringLiteral("direction"), directionCombo_->currentData().toInt()},
            {QStringLiteral("defaultDelta"), colorToHex(defaultDelta)},
    };
}

QString ColorPanel::currentLanguage() const {
    const GeneratorFormat* format = generator_->find(formatCombo_->currentData().toString());
    return format == nullptr ? QStringLiteral("lua") : format->language;
}

} // namespace xiaoyv::tools
