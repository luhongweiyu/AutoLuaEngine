/**
 * 文件用途：验证重构后的图片状态、二值化、点阵字库事务和代码生成核心行为。
 */
#include <QtTest/QTest>

#include "core/image_processing.h"
#include "core/selection_range.h"
#include "generator/generator_engine.h"
#include "model/font_dictionary_document.h"
#include "model/image_document.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

using namespace xiaoyv::tools;

class ModelTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesSelectionWithoutImageBounds();
    void parsesColorRulesAndFindsPattern();
    void tracksTemporaryAndFileImageState();
    void preservesColorPointsAcrossImageChanges();
    void rejectsStalePreview();
    void createsBinaryPreview();
    void extractsOverviewAndSplitGlyphs();
    void enforcesDictionaryPixelUniqueness();
    void allowsSameLabelWithDifferentPixels();
    void mergesExternalDictionaryChangesAtomically();
    void resolvesExternalDictionaryConflict();
    void generatesAllBuiltInFormats();
    void usesExecutableFormatsDirectory();
};

void ModelTests::parsesSelectionWithoutImageBounds() {
    QRect range;
    QString error;
    QVERIFY(parseSelectionRange(QStringLiteral("-20,3,9000,10000"), &range, &error));
    QCOMPARE(range, QRect(QPoint(-20, 3), QPoint(9000, 10000)));
    QVERIFY(parseSelectionRange(QString::fromUtf8("全图"), &range, &error));
    QVERIFY(range.isNull());
    QVERIFY(!parseSelectionRange(QStringLiteral("10,10,1,1"), &range, &error));
}

void ModelTests::parsesColorRulesAndFindsPattern() {
    QImage image(4, 3, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    image.setPixelColor(1, 1, QColor(QStringLiteral("#1A73E8")));
    image.setPixelColor(2, 1, QColor(QStringLiteral("#1B72E7")));
    std::vector<xiaoyv::image::ColorRule> rules;
    QString error;
    QVERIFY(parseColorRules(QStringLiteral("1A73E8-010101"), &rules, &error));
    std::vector<std::uint8_t> mask;
    int width = 0;
    int height = 0;
    QVERIFY(makeColorMask(image, {}, rules, &mask, &width, &height, nullptr, &error));
    const std::uint8_t pattern[] = {1, 1};
    const auto matches = xiaoyv::image::findBinaryPattern(
            mask.data(), width, height, pattern, 2, 1, 1.0);
    QCOMPARE(matches.size(), std::size_t(1));
    QCOMPARE(matches.front().x, 1);
    QCOMPARE(matches.front().y, 1);
}

void ModelTests::tracksTemporaryAndFileImageState() {
    QImage image(4, 3, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    ImageDocument temporary(image, {}, QString::fromUtf8("截图"));
    QVERIFY(temporary.isTemporary());
    QVERIFY(temporary.needsExitAttention());
    QVERIFY(temporary.tabTitle().endsWith(QLatin1Char('*')));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString error;
    const QString path = directory.filePath(QStringLiteral("saved.png"));
    QVERIFY(temporary.save(path, &error));
    QVERIFY(!temporary.isTemporary());
    QVERIFY(!temporary.isModified());
    QVERIFY(!temporary.needsExitAttention());

    temporary.flipHorizontal();
    QVERIFY(temporary.isModified());
    QVERIFY(temporary.canUndo());
    temporary.undo();
    QVERIFY(!temporary.isModified());
}

void ModelTests::preservesColorPointsAcrossImageChanges() {
    QImage image(2, 3, QImage::Format_RGBA8888);
    image.fill(Qt::black);
    image.setPixelColor(0, 0, Qt::red);
    image.setPixelColor(0, 2, Qt::blue);
    ImageDocument document(image);
    document.colorPoints()->addPoint(QPoint(0, 0), Qt::red);
    document.setSelection(QRect(QPoint(0, 1), QPoint(1, 2)));

    document.rotateRight();
    QCOMPARE(document.image().size(), QSize(3, 2));
    QCOMPARE(document.selection(), QRect(QPoint(0, 0), QPoint(1, 1)));
    QCOMPARE(document.colorPoints()->points().front().position, QPoint(0, 0));
    QCOMPARE(document.colorPoints()->points().front().color, QColor(Qt::red));
    document.undo();
    QCOMPARE(document.image().size(), QSize(2, 3));
    QCOMPARE(document.selection(), QRect(QPoint(0, 1), QPoint(1, 2)));
    document.redo();
    QVERIFY(document.colorPoints()->refreshPoint(0, document.image()));
    QCOMPARE(document.colorPoints()->points().front().color, QColor(Qt::blue));
}

void ModelTests::rejectsStalePreview() {
    QImage source(3, 2, QImage::Format_RGBA8888);
    source.fill(Qt::black);
    ImageDocument document(source);
    const quint64 revision = document.imageRevision();
    QImage preview(source.size(), QImage::Format_RGBA8888);
    preview.fill(Qt::white);
    QVERIFY(document.setPreview(preview, revision));
    QVERIFY(document.hasPreview());
    document.rotateRight();
    QVERIFY(!document.hasPreview());
    QVERIFY(!document.setPreview(preview, revision));
}

void ModelTests::createsBinaryPreview() {
    QImage image(2, 1, QImage::Format_RGBA8888);
    image.setPixelColor(0, 0, QColor(250, 250, 250));
    image.setPixelColor(1, 0, QColor(10, 10, 10));
    BinarySettings settings;
    settings.mode = BinaryMode::Grayscale;
    settings.grayscaleThreshold = 128;
    QImage result;
    QString error;
    QVERIFY(makeBinaryPreview(image, settings, &result, &error));
    QCOMPARE(result.pixelColor(0, 0), QColor(255, 255, 255, 255));
    QCOMPARE(result.pixelColor(1, 0), QColor(0, 0, 0, 255));

    QImage colorSource(3, 1, QImage::Format_RGBA8888);
    colorSource.setPixelColor(0, 0, QColor(QStringLiteral("#112233")));
    colorSource.setPixelColor(1, 0, QColor(QStringLiteral("#AABBCC")));
    colorSource.setPixelColor(2, 0, QColor(QStringLiteral("#445566")));
    settings.mode = BinaryMode::Color;
    settings.colorRules = QStringLiteral("112233-000000|AABBCC-000000");
    QVERIFY(makeBinaryPreview(colorSource, settings, &result, &error));
    QCOMPARE(result.pixelColor(0, 0), QColor(255, 255, 255, 255));
    QCOMPARE(result.pixelColor(1, 0), QColor(255, 255, 255, 255));
    QCOMPARE(result.pixelColor(2, 0), QColor(0, 0, 0, 255));
}

void ModelTests::extractsOverviewAndSplitGlyphs() {
    QImage image(12, 6, QImage::Format_RGBA8888);
    image.fill(Qt::white);
    for (int y = 1; y <= 4; ++y) {
        for (int x = 1; x <= 2; ++x) image.setPixelColor(x, y, Qt::black);
        for (int x = 7; x <= 9; ++x) image.setPixelColor(x, y, Qt::black);
    }
    std::vector<xiaoyv::image::ColorRule> rules;
    QString error;
    QVERIFY(parseColorRules(QStringLiteral("000000-000000"), &rules, &error));
    std::vector<ExtractedGlyph> glyphs;
    QVERIFY(extractFontGlyphs(image, {}, rules, 1, 2, &glyphs, &error));
    QCOMPARE(glyphs.size(), std::size_t(3));
    QVERIFY(glyphs[0].overview);
    QCOMPARE(glyphs[1].bounds, QRect(1, 1, 2, 4));
    QCOMPARE(glyphs[2].bounds, QRect(7, 1, 3, 4));

    QImage single(4, 4, QImage::Format_RGBA8888);
    single.fill(Qt::white);
    single.setPixelColor(1, 1, Qt::black);
    QVERIFY(extractFontGlyphs(single, {}, rules, 1, 1, &glyphs, &error));
    QCOMPARE(glyphs.size(), std::size_t(2));
    QVERIFY(glyphs[0].overview);
    QVERIFY(!glyphs[1].overview);
    QCOMPARE(glyphs[0].mask, glyphs[1].mask);
}

void ModelTests::enforcesDictionaryPixelUniqueness() {
    FontDictionaryRecord first;
    FontDictionaryRecord second;
    QString error;
    QVERIFY(FontDictionaryDocument::parseRecord(QStringLiteral("A$2$2$F"), &first, &error));
    QVERIFY(FontDictionaryDocument::parseRecord(QStringLiteral("B$2$2$F"), &second, &error));
    QVector<FontDictionaryRecord> records;
    QCOMPARE(FontDictionaryDocument::addTo(
            &records, first, FontDictionaryDocument::DuplicateDecision::Skip),
            FontDictionaryDocument::WriteResult::Added);
    QCOMPARE(FontDictionaryDocument::addTo(
            &records, second, FontDictionaryDocument::DuplicateDecision::Skip),
            FontDictionaryDocument::WriteResult::Skipped);
    QCOMPARE(records.front().label, QStringLiteral("A"));
    QCOMPARE(FontDictionaryDocument::addTo(
            &records, second, FontDictionaryDocument::DuplicateDecision::Replace),
            FontDictionaryDocument::WriteResult::Replaced);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.front().label, QStringLiteral("B"));
}

void ModelTests::allowsSameLabelWithDifferentPixels() {
    FontDictionaryRecord first;
    FontDictionaryRecord second;
    QString error;
    QVERIFY(FontDictionaryDocument::parseRecord(QStringLiteral("同$1$1$8"), &first, &error));
    QVERIFY(FontDictionaryDocument::parseRecord(QStringLiteral("同$1$2$C"), &second, &error));
    QVector<FontDictionaryRecord> records;
    QCOMPARE(FontDictionaryDocument::addTo(
            &records, first, FontDictionaryDocument::DuplicateDecision::Skip),
            FontDictionaryDocument::WriteResult::Added);
    QCOMPARE(FontDictionaryDocument::addTo(
            &records, second, FontDictionaryDocument::DuplicateDecision::Skip),
            FontDictionaryDocument::WriteResult::Added);
    QCOMPARE(records.size(), 2);
}

void ModelTests::mergesExternalDictionaryChangesAtomically() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("font.txt"));
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << "# 说明\nA$2$2$F\n";
    }

    FontDictionaryDocument document;
    QString error;
    QVERIFY(document.load(path, &error));
    QCOMPARE(document.rowCount(), 1);
    QVERIFY(document.data(document.index(0, 0), Qt::DisplayRole).toString().contains(QStringLiteral("A")));
    FontDictionaryRecord second;
    QVERIFY(FontDictionaryDocument::parseRecord(QStringLiteral("B$1$1$8"), &second, &error));
    QCOMPARE(document.add(second, FontDictionaryDocument::DuplicateDecision::Skip),
             FontDictionaryDocument::WriteResult::Added);
    QCOMPARE(document.rowCount(), 2);
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::Append | QIODevice::Text));
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << "C$1$2$C\n";
    }
    QVERIFY(document.save(path, {}, &error));
    QFile saved(path);
    QVERIFY(saved.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(saved.readAll());
    QVERIFY(text.contains(QStringLiteral("# 说明")));
    QVERIFY(text.contains(QStringLiteral("A$2$2$F")));
    QVERIFY(text.contains(QStringLiteral("B$1$1$8")));
    QVERIFY(text.contains(QStringLiteral("C$1$2$C")));
}

void ModelTests::resolvesExternalDictionaryConflict() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("conflict.txt"));
    auto write = [&path](const QString& text) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        file.write(text.toUtf8());
        return true;
    };
    QVERIFY(write(QStringLiteral("A$2$2$F\n")));
    FontDictionaryDocument document;
    QString error;
    QVERIFY(document.load(path, &error));
    FontDictionaryRecord local;
    QVERIFY(FontDictionaryDocument::parseRecord(QStringLiteral("B$2$2$F"), &local, &error));
    QCOMPARE(document.add(local, FontDictionaryDocument::DuplicateDecision::Replace),
             FontDictionaryDocument::WriteResult::Replaced);
    QVERIFY(write(QStringLiteral("C$2$2$F\n")));
    int resolverCalls = 0;
    QString existingLabel;
    QString incomingLabel;
    QVERIFY(document.save(path,
            [&resolverCalls, &existingLabel, &incomingLabel](
                    const FontDictionaryRecord& existing,
                    const FontDictionaryRecord& incoming,
                    int) {
                ++resolverCalls;
                existingLabel = existing.label;
                incomingLabel = incoming.label;
                return FontDictionaryDocument::DuplicateDecision::Skip;
            },
            &error));
    QCOMPARE(resolverCalls, 1);
    QCOMPARE(existingLabel, QStringLiteral("C"));
    QCOMPARE(incomingLabel, QStringLiteral("B"));
    QCOMPARE(document.records().front().label, QStringLiteral("C"));
}

void ModelTests::generatesAllBuiltInFormats() {
    GeneratorEngine generator;
    QVERIFY2(!generator.formats().isEmpty(), qPrintable(generator.loadErrors().join('\n')));
    QVariantMap context{
            {QStringLiteral("points"), QVariantList{
                    QVariantMap{{"enabled", true}, {"base", true}, {"x", 10}, {"y", 20},
                                {"dx", 0}, {"dy", 0}, {"hex", "FFFFFF"}, {"delta", "000000"}}
            }},
            {QStringLiteral("region"), QVariantMap{{"left", 0}, {"top", 0}, {"right", 100}, {"bottom", 200}}},
            {QStringLiteral("direction"), 1},
            {QStringLiteral("defaultDelta"), QStringLiteral("000000")},
    };
    QString error;
    const QString code = generator.generate(QStringLiteral("m-find-colors-lua"), context, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(code.contains(QStringLiteral("m.findColors")));
    const QString javaScript = generator.generate(
            QStringLiteral("m-find-colors-js"), context, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(javaScript.contains(QStringLiteral("m.findColors")));
    const QString coordinateLua = generator.generate(
            QStringLiteral("coordinate-list-lua"), context, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(coordinateLua.contains(QStringLiteral("local points")));
    const QString coordinateJavaScript = generator.generate(
            QStringLiteral("coordinate-list-js"), context, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(coordinateJavaScript.contains(QStringLiteral("const points")));
}

void ModelTests::usesExecutableFormatsDirectory() {
    GeneratorEngine generator;
    const QString expected = QDir::cleanPath(
            QCoreApplication::applicationDirPath() + QStringLiteral("/formats"));
    // 格式目录必须与当前测试程序同级，确保正式程序也不会回退到系统配置目录。
    QCOMPARE(QDir::cleanPath(generator.formatsDirectory()), expected);
    QVERIFY(QDir(expected).exists());
}

QTEST_MAIN(ModelTests)
#include "model_tests.moc"
