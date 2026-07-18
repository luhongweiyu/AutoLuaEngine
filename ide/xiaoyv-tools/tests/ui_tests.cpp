/**
 * 文件用途：验证画布精确定位、框选结束和主要面板在无图片时保持可操作。
 */
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include "canvas/image_canvas.h"
#include "generator/generator_engine.h"
#include "model/image_document.h"
#include "panels/analysis_panel.h"
#include "panels/color_panel.h"
#include "panels/font_dictionary_editor.h"
#include "panels/font_panel.h"
#include "panels/script_editor_panel.h"
#include "ui/pixel_grid.h"
#include "ui/range_editor.h"
#include "workspace/image_workspace.h"

#include <QAbstractItemDelegate>
#include <QApplication>
#include <QCheckBox>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QStyleOptionViewItem>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>

using namespace xiaoyv::tools;

class UiTests final : public QObject {
    Q_OBJECT

private slots:
    void canvasMovesInImageDirections();
    void canvasPicksOnlyByKeyboard();
    void magnifierPreservesSourceColors();
    void selectionClampsAndEndsMode();
    void panelsRemainUsableWithoutImage();
    void rangePanelsOnlySubmitUserInput();
    void workspaceCarriesToolStateAcrossImages();
    void scriptEditorOwnsLanguageAndExecutionState();
    void dictionaryEditorOwnsRecordTransactions();
    void colorPanelUsesSelectedBaseAndDeletesOneRow();
    void fontExtractionReplacesStalePreview();
    void fontGridRequiresExplicitEditing();
    void analysisModeEnablesOnlyRelevantInputs();
    void analysisUsesEnabledColorPointRules();
    void analysisPreviewFollowsActiveState();
    void analysisAppliesLatestPreview();
    void analysisDiscardsResultAfterImageChanges();
};

void UiTests::canvasMovesInImageDirections() {
    QImage image(20, 20, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    ImageDocument document(image);
    ImageCanvas canvas;
    canvas.resize(300, 220);
    canvas.setDocument(&document);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    QSignalSpy hoverSpy(&canvas, &ImageCanvas::hoverChanged);
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QTRY_VERIFY(!hoverSpy.isEmpty());
    const QPoint before = canvas.currentImagePosition();
    QTest::keyClick(&canvas, Qt::Key_Right);
    QCOMPARE(canvas.currentImagePosition(), before + QPoint(1, 0));
    QTest::keyClick(&canvas, Qt::Key_Down);
    QCOMPARE(canvas.currentImagePosition(), before + QPoint(1, 1));
}

void UiTests::canvasPicksOnlyByKeyboard() {
    QImage image(32, 24, QImage::Format_RGBA8888);
    image.fill(QColor(QStringLiteral("#123456")));
    ImageDocument document(image);
    ImageCanvas canvas;
    canvas.resize(300, 220);
    canvas.setDocument(&document);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    QSignalSpy picked(&canvas, &ImageCanvas::pickRequested);
    QTest::mouseClick(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(7, 9));
    QCOMPARE(picked.size(), 0);
    QTest::keyClick(&canvas, Qt::Key_Right);
    QTest::keyClick(&canvas, Qt::Key_Space);
    QCOMPARE(picked.size(), 1);
    QCOMPARE(picked.front().at(0).toPoint(), QPoint(8, 9));
    QCOMPARE(picked.front().at(1).value<QColor>(), QColor(QStringLiteral("#123456")));
}

void UiTests::magnifierPreservesSourceColors() {
    const QColor source(QStringLiteral("#12A4E0"));
    QImage image(32, 24, QImage::Format_RGBA8888);
    image.fill(source);
    ImageDocument document(image);
    ImageCanvas canvas;
    canvas.resize(300, 220);
    canvas.setDocument(&document);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    const QPoint localPosition(10, 10);
    QMouseEvent moveEvent(
            QEvent::MouseMove,
            QPointF(localPosition),
            QPointF(localPosition),
            QPointF(canvas.viewport()->mapToGlobal(localPosition)),
            Qt::NoButton,
            Qt::NoButton,
            Qt::NoModifier);
    QApplication::sendEvent(canvas.viewport(), &moveEvent);
    QCOMPARE(canvas.currentImagePosition(), localPosition);

    QImage rendered(canvas.viewport()->size(), QImage::Format_RGBA8888);
    rendered.fill(Qt::transparent);
    canvas.viewport()->render(&rendered);
    int magnifiedSourcePixels = 0;
    for (int y = 0; y < rendered.height(); ++y) {
        for (int x = 0; x < rendered.width(); ++x) {
            const bool outsideOriginalImage = x >= image.width() || y >= image.height();
            if (outsideOriginalImage && rendered.pixelColor(x, y) == source) {
                ++magnifiedSourcePixels;
            }
        }
    }
    // 原图区域外仍应出现大量原始颜色像素，证明放大镜没有灰度化或交换颜色通道。
    QVERIFY(magnifiedSourcePixels > 100);
}

void UiTests::selectionClampsAndEndsMode() {
    QImage image(10, 8, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    ImageDocument document(image);
    ImageCanvas canvas;
    canvas.resize(220, 180);
    canvas.setDocument(&document);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));
    canvas.setSelectionMode(true);
    QSignalSpy completed(&canvas, &ImageCanvas::selectionCompleted);
    QTest::mousePress(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
    QTest::mouseMove(canvas.viewport(), QPoint(180, 160));
    QTest::mouseRelease(canvas.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(180, 160));
    QCOMPARE(completed.size(), 1);
    QCOMPARE(document.selection(), QRect(QPoint(2, 2), QPoint(9, 7)));
    QVERIFY(!canvas.selectionMode());
}

void UiTests::panelsRemainUsableWithoutImage() {
    AnalysisPanel analysis;
    FontPanel font;
    analysis.setDocument(nullptr);
    font.setDocument(nullptr);
    const QList<QRadioButton*> modes = analysis.findChildren<QRadioButton*>();
    QCOMPARE(modes.size(), 2);
    QVERIFY(modes[0]->isEnabled());
    QVERIFY(font.findChild<QLineEdit*>() != nullptr);
}

void UiTests::rangePanelsOnlySubmitUserInput() {
    QImage image(20, 20, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    ImageDocument document(image);
    GeneratorEngine generator;
    ColorPanel color(&generator);
    FontPanel font;
    color.setDocument(&document);
    font.setDocument(&document);

    auto submitRange = [&document](QWidget* panel, const char* signal) {
        auto* editor = panel->findChild<RangeEditor*>();
        QVERIFY(editor != nullptr);
        auto* input = editor->findChild<QLineEdit*>();
        QVERIFY(input != nullptr);
        QSignalSpy submitted(panel, signal);
        input->setText(QStringLiteral("1,2,8,9"));
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(submitted.size(), 1);
        QVERIFY(document.selection().isNull());
    };
    submitRange(&color, SIGNAL(selectionRangeEdited(QRect)));
    submitRange(&font, SIGNAL(selectionRangeEdited(QRect)));

    const QRect modelRange(QPoint(3, 4), QPoint(10, 11));
    document.setSelection(modelRange);
    for (RangeEditor* editor : color.findChildren<RangeEditor*>()) {
        QCOMPARE(editor->findChild<QLineEdit*>()->text(), QStringLiteral("3,4,10,11"));
    }
    for (RangeEditor* editor : font.findChildren<RangeEditor*>()) {
        QCOMPARE(editor->findChild<QLineEdit*>()->text(), QStringLiteral("3,4,10,11"));
    }
}

void UiTests::workspaceCarriesToolStateAcrossImages() {
    ImageWorkspace workspace;
    const QRect pending(QPoint(2, 3), QPoint(8, 9));
    workspace.applySelectionRange(pending);
    QCOMPARE(workspace.selectionRange(), pending);

    QImage first(20, 20, QImage::Format_RGBA8888);
    first.fill(Qt::white);
    QVERIFY(workspace.addImage(first, {}, QString::fromUtf8("第一张")));
    QVERIFY(workspace.currentDocument() != nullptr);
    QCOMPARE(workspace.currentDocument()->selection(), pending);
    workspace.currentDocument()->colorPoints()->addPoint(
            QPoint(4, 5), QColor(QStringLiteral("#112233")));

    QImage second(20, 20, QImage::Format_RGBA8888);
    second.fill(Qt::black);
    QVERIFY(workspace.addImage(second, {}, QString::fromUtf8("第二张")));
    QCOMPARE(workspace.currentDocument()->selection(), pending);
    QCOMPARE(workspace.currentDocument()->colorPoints()->rowCount(), 1);
    QCOMPARE(workspace.currentDocument()->colorPoints()->points().front().color,
             QColor(QStringLiteral("#112233")));

    QVERIFY(workspace.currentDocument()->colorPoints()->setDelta(
            0, QColor(QStringLiteral("#010203"))));
    auto* tabs = workspace.findChild<QTabWidget*>(QStringLiteral("imageDocuments"));
    QVERIFY(tabs != nullptr);
    tabs->setCurrentIndex(0);
    QCOMPARE(workspace.currentDocument()->selection(), pending);
    QCOMPARE(workspace.currentDocument()->colorPoints()->points().front().delta,
             QColor(QStringLiteral("#010203")));
}

void UiTests::scriptEditorOwnsLanguageAndExecutionState() {
    ScriptEditorPanel editor;
    editor.resize(560, 260);
    editor.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editor));
    editor.setCode(QStringLiteral("js"), QStringLiteral("console.log('ok')"));
    QSignalSpy runRequested(&editor, &ScriptEditorPanel::runRequested);
    QPushButton* run = nullptr;
    QPushButton* stop = nullptr;
    QPushButton* copy = nullptr;
    for (QPushButton* button : editor.findChildren<QPushButton*>()) {
        if (button->text() == QString::fromUtf8("复制")) copy = button;
        if (button->text() == QString::fromUtf8("运行当前代码")) run = button;
        if (button->text() == QString::fromUtf8("停止")) stop = button;
    }
    QVERIFY(run != nullptr);
    QVERIFY(stop != nullptr);
    QVERIFY(copy != nullptr);
    QVERIFY(copy->x() < run->x());
    QVERIFY(run->x() < stop->x());
    QVERIFY(copy->x() <= 2);
    QVERIFY(run->x() - copy->geometry().right() >= 3);
    QVERIFY(stop->x() - run->geometry().right() >= 3);
    run->click();
    QCOMPARE(runRequested.size(), 1);
    QCOMPARE(runRequested.front().at(0).toString(), QStringLiteral("js"));
    QCOMPARE(runRequested.front().at(1).toString(), QStringLiteral("console.log('ok')"));

    editor.setExecutionState(true, false, false);
    QVERIFY(!run->isEnabled());
    QVERIFY(stop->isEnabled());
    editor.setExecutionState(false, false, false);
    QVERIFY(run->isEnabled());
    QVERIFY(!stop->isEnabled());
}

void UiTests::dictionaryEditorOwnsRecordTransactions() {
    FontDictionaryRecord current;
    QString parseError;
    QVERIFY(FontDictionaryDocument::parseRecord(
            QStringLiteral("甲$2$2$8"), &current, &parseError));
    FontDictionaryRecord second;
    QVERIFY(FontDictionaryDocument::parseRecord(
            QStringLiteral("乙$2$2$4"), &second, &parseError));

    FontDictionaryEditor editor;
    editor.setRecordProvider([current](QString* error) {
        if (error != nullptr) error->clear();
        return current;
    });
    editor.setBatchProvider([second](QString* error) {
        if (error != nullptr) error->clear();
        return QVector<FontDictionaryRecord>{second};
    });
    QPushButton* add = nullptr;
    QPushButton* addBatch = nullptr;
    for (QPushButton* button : editor.findChildren<QPushButton*>()) {
        if (button->text() == QString::fromUtf8("添加当前")) add = button;
        if (button->text() == QString::fromUtf8("批量加入")) addBatch = button;
    }
    auto* list = editor.findChild<QListView*>(QStringLiteral("fontDictionaryList"));
    QVERIFY(add != nullptr);
    QVERIFY(addBatch != nullptr);
    QVERIFY(list != nullptr);
    add->click();
    QCOMPARE(list->model()->rowCount(), 1);
    addBatch->click();
    QCOMPARE(list->model()->rowCount(), 2);
}

void UiTests::colorPanelUsesSelectedBaseAndDeletesOneRow() {
    QImage image(20, 20, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    ImageDocument document(image);
    document.colorPoints()->addPoint(QPoint(2, 3), QColor(QStringLiteral("#FF0000")));
    document.colorPoints()->addPoint(QPoint(8, 9), QColor(QStringLiteral("#00FF00")));
    QVERIFY(document.colorPoints()->setBase(1));

    GeneratorEngine generator;
    ColorPanel panel(&generator);
    panel.setDocument(&document);
    QPushButton* generate = nullptr;
    for (QPushButton* button : panel.findChildren<QPushButton*>()) {
        if (button->text() == QString::fromUtf8("生成代码")) generate = button;
    }
    QVERIFY(generate != nullptr);
    QSignalSpy generated(&panel, &ColorPanel::codeGenerated);
    generate->click();
    QCOMPARE(generated.size(), 1);
    QVERIFY(generated.front().at(1).toString().contains(
            QStringLiteral("0|0|00FF00,-6|-6|FF0000")));

    auto* table = panel.findChild<QTableView*>(QStringLiteral("colorPointTable"));
    QVERIFY(table != nullptr);
    QAbstractItemDelegate* delegate = table->itemDelegateForColumn(ColorPointModel::BaseColumn);
    QVERIFY(delegate != nullptr);
    QStyleOptionViewItem option;
    option.rect = QRect(0, 0, 62, 24);
    QMouseEvent removeEvent(
            QEvent::MouseButtonRelease,
            QPointF(55, 12),
            QPointF(55, 12),
            QPointF(55, 12),
            Qt::LeftButton,
            Qt::LeftButton,
            Qt::NoModifier);
    QVERIFY(delegate->editorEvent(
            &removeEvent,
            document.colorPoints(),
            option,
            document.colorPoints()->index(0, ColorPointModel::BaseColumn)));
    QCOMPARE(document.colorPoints()->rowCount(), 1);
    QCOMPARE(document.colorPoints()->points().front().position, QPoint(8, 9));

    const QModelIndex coordinate = document.colorPoints()->index(0, ColorPointModel::CoordinateColumn);
    QVERIFY(document.colorPoints()->flags(coordinate) & Qt::ItemIsEditable);
    QVERIFY(document.colorPoints()->setData(coordinate, QStringLiteral("12, 14"), Qt::EditRole));
    QCOMPARE(document.colorPoints()->points().front().position, QPoint(12, 14));

    const QModelIndex rgb = document.colorPoints()->index(0, ColorPointModel::HexColumn);
    QVERIFY(document.colorPoints()->flags(rgb) & Qt::ItemIsEditable);
    QVERIFY(document.colorPoints()->setData(rgb, QStringLiteral("123456"), Qt::EditRole));
    QCOMPARE(document.colorPoints()->points().front().color, QColor(QStringLiteral("#123456")));

    QAbstractItemDelegate* hexDelegate = table->itemDelegateForColumn(ColorPointModel::HexColumn);
    QVERIFY(hexDelegate != nullptr);
    QStyleOptionViewItem editorOption;
    QWidget* editor = hexDelegate->createEditor(table, editorOption, rgb);
    auto* hexEdit = qobject_cast<QLineEdit*>(editor);
    QVERIFY(hexEdit != nullptr);
    QCOMPARE(hexEdit->maxLength(), 8);
    delete editor;

    const QModelIndex swatch = document.colorPoints()->index(0, ColorPointModel::SwatchColumn);
    QAbstractItemDelegate* swatchDelegate = table->itemDelegateForColumn(ColorPointModel::SwatchColumn);
    QVERIFY(swatchDelegate != nullptr);
    QImage rendered(24, 22, QImage::Format_RGBA8888);
    rendered.fill(Qt::transparent);
    QPainter painter(&rendered);
    QStyleOptionViewItem swatchOption;
    swatchOption.rect = rendered.rect();
    swatchOption.palette = table->palette();
    swatchOption.state = QStyle::State_Enabled | QStyle::State_Selected;
    swatchDelegate->paint(&painter, swatchOption, swatch);
    painter.end();
    QCOMPARE(rendered.pixelColor(rendered.rect().center()), QColor(QStringLiteral("#123456")));
}

void UiTests::fontExtractionReplacesStalePreview() {
    QImage image(12, 5, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    image.setPixelColor(1, 1, Qt::black);
    image.setPixelColor(2, 1, Qt::black);
    image.setPixelColor(1, 2, Qt::black);
    image.setPixelColor(2, 2, Qt::black);
    for (int x = 7; x <= 9; ++x) {
        image.setPixelColor(x, 1, Qt::black);
        image.setPixelColor(x, 2, Qt::black);
    }
    ImageDocument document(image);
    document.colorPoints()->addPoint(QPoint(1, 1), Qt::black);

    FontPanel panel;
    panel.setDocument(&document);
    auto* extract = panel.findChild<QPushButton*>(QStringLiteral("fontExtractButton"));
    auto* text = panel.findChild<QPlainTextEdit*>(QStringLiteral("fontPixelText"));
    auto* table = panel.findChild<QTableWidget*>(QStringLiteral("fontExtractedTable"));
    QVERIFY(extract != nullptr);
    QVERIFY(text != nullptr);
    QVERIFY(table != nullptr);

    extract->click();
    QCOMPARE(table->columnCount(), 4);
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QString::fromUtf8("序"));
    QCOMPARE(table->horizontalHeaderItem(1)->text(), QString::fromUtf8("文字"));
    QCOMPARE(table->horizontalHeaderItem(2)->text(), QString::fromUtf8("宽高"));
    QCOMPARE(table->horizontalHeaderItem(3)->text(), QString::fromUtf8("点数"));
    QCOMPARE(table->item(0, 0)->text(), QString::fromUtf8("总"));
    QCOMPARE(table->currentRow(), 1);
    QVERIFY(!text->toPlainText().isEmpty());

    ImageDocument otherDocument(image);
    panel.setDocument(&otherDocument);
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->currentRow(), 1);
    panel.setDocument(&document);
    QCOMPARE(table->rowCount(), 3);
    QCOMPARE(table->currentRow(), 1);
    QVERIFY(!text->toPlainText().isEmpty());

    QVERIFY(document.colorPoints()->setColor(0, Qt::red));
    extract->click();
    QCOMPARE(table->rowCount(), 0);
    QVERIFY(text->toPlainText().isEmpty());
}

void UiTests::fontGridRequiresExplicitEditing() {
    FontPanel panel;
    panel.resize(420, 760);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    auto* text = panel.findChild<QPlainTextEdit*>(QStringLiteral("fontPixelText"));
    auto* grid = panel.findChild<PixelGrid*>();
    QCheckBox* editable = nullptr;
    for (QCheckBox* check : panel.findChildren<QCheckBox*>()) {
        if (check->text() == QString::fromUtf8("允许编辑")) editable = check;
    }
    QVERIFY(text != nullptr);
    QVERIFY(grid != nullptr);
    QVERIFY(editable != nullptr);

    text->setPlainText(QStringLiteral("2$2$8"));
    QApplication::processEvents();
    QTest::mouseClick(grid, Qt::LeftButton, Qt::NoModifier, QPoint(8, 8));
    QCOMPARE(text->toPlainText(), QStringLiteral("2$2$8"));
    editable->setChecked(true);
    QTest::mouseClick(grid, Qt::LeftButton, Qt::NoModifier, QPoint(8, 8));
    QCOMPARE(text->toPlainText(), QStringLiteral("2$2$0"));
}

void UiTests::analysisModeEnablesOnlyRelevantInputs() {
    AnalysisPanel panel;
    auto* colorRules = panel.findChild<QLineEdit*>(QStringLiteral("binaryColorRulesInput"));
    auto* threshold = panel.findChild<QSlider*>();
    QVERIFY(colorRules != nullptr);
    QVERIFY(threshold != nullptr);
    QVERIFY(colorRules->isEnabled());
    QVERIFY(colorRules->maxLength() > 8);
    QVERIFY(!threshold->isEnabled());
    QVERIFY(panel.findChild<QPushButton*>(QStringLiteral("analysisPreviewButton")) == nullptr);
    QVERIFY(panel.findChild<QPushButton*>(QStringLiteral("analysisRestoreButton")) == nullptr);
    for (QCheckBox* check : panel.findChildren<QCheckBox*>()) {
        QVERIFY(check->text() != QString::fromUtf8("实时预览"));
    }

    for (QRadioButton* mode : panel.findChildren<QRadioButton*>()) {
        if (mode->text() == QString::fromUtf8("灰度阈值")) mode->setChecked(true);
    }
    QVERIFY(!colorRules->isEnabled());
    QVERIFY(threshold->isEnabled());
}

void UiTests::analysisUsesEnabledColorPointRules() {
    QImage image(3, 1, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    ImageDocument document(image);
    document.colorPoints()->addPoint(QPoint(0, 0), QColor(QStringLiteral("#112233")));
    document.colorPoints()->addPoint(QPoint(1, 0), QColor(QStringLiteral("#AABBCC")));

    AnalysisPanel panel;
    panel.setPreviewActive(true);
    panel.setDocument(&document);
    auto* colorRules = panel.findChild<QLineEdit*>(QStringLiteral("binaryColorRulesInput"));
    QVERIFY(colorRules != nullptr);
    QCOMPARE(colorRules->text(), QStringLiteral("112233-000000|AABBCC-000000"));

    QVERIFY(document.colorPoints()->setDelta(1, QColor(QStringLiteral("#010203"))));
    QCOMPARE(colorRules->text(), QStringLiteral("112233-000000|AABBCC-010203"));
    QVERIFY(document.colorPoints()->setEnabled(0, false));
    QCOMPARE(colorRules->text(), QStringLiteral("AABBCC-010203"));

    colorRules->setText(QStringLiteral("ABCDEF-000000"));
    QImage secondImage(3, 1, QImage::Format_RGBA8888);
    secondImage.fill(Qt::red);
    ImageDocument secondDocument(secondImage);
    secondDocument.colorPoints()->addPoint(QPoint(0, 0), Qt::red);
    panel.setDocument(&secondDocument);
    QCOMPARE(colorRules->text(), QStringLiteral("ABCDEF-000000"));
    panel.setDocument(&document);
    QCOMPARE(colorRules->text(), QStringLiteral("ABCDEF-000000"));
}

void UiTests::analysisPreviewFollowsActiveState() {
    QImage image(8, 8, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    ImageDocument document(image);
    document.colorPoints()->addPoint(QPoint(0, 0), Qt::white);
    AnalysisPanel panel;
    panel.setDocument(&document);

    QTest::qWait(50);
    QVERIFY(!document.hasPreview());
    panel.setPreviewActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(document.hasPreview(), 3000);
    panel.setPreviewActive(false);
    QVERIFY(!document.hasPreview());

    QVERIFY(document.colorPoints()->setDelta(0, QColor(QStringLiteral("#010101"))));
    QTest::qWait(50);
    QVERIFY(!document.hasPreview());
}

void UiTests::analysisAppliesLatestPreview() {
    QImage image(3, 1, QImage::Format_RGBA8888);
    image.setPixelColor(0, 0, QColor(250, 250, 250));
    image.setPixelColor(1, 0, QColor(100, 100, 100));
    image.setPixelColor(2, 0, QColor(10, 10, 10));
    ImageDocument document(image);
    AnalysisPanel panel;
    panel.setPreviewActive(true);
    panel.setDocument(&document);
    for (QRadioButton* mode : panel.findChildren<QRadioButton*>()) {
        if (mode->text() == QString::fromUtf8("灰度阈值")) mode->setChecked(true);
    }
    QPushButton* apply = panel.findChild<QPushButton*>(QStringLiteral("analysisApplyButton"));
    QVERIFY(apply != nullptr);
    QTest::mouseClick(apply, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(document.imageRevision() > 1, 3000);
    QCOMPARE(document.image().pixelColor(0, 0), QColor(255, 255, 255, 255));
    QCOMPARE(document.image().pixelColor(2, 0), QColor(0, 0, 0, 255));
}

void UiTests::analysisDiscardsResultAfterImageChanges() {
    QImage image(1800, 1200, QImage::Format_RGBA8888);
    image.fill(QColor(120, 120, 120));
    ImageDocument document(image);
    AnalysisPanel panel;
    panel.setPreviewActive(true);
    panel.setDocument(&document);
    QPushButton* apply = panel.findChild<QPushButton*>(QStringLiteral("analysisApplyButton"));
    QVERIFY(apply != nullptr);
    QTest::mouseClick(apply, Qt::LeftButton);
    document.rotateRight();
    QTest::qWait(500);
    QVERIFY(!document.hasPreview());
}

QTEST_MAIN(UiTests)
#include "ui_tests.moc"
