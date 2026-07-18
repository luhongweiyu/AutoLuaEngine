/**
 * 文件用途：声明取色点、范围、格式生成和设备测试面板。
 */
#pragma once

#include <QMetaObject>
#include <QPointer>
#include <QRect>
#include <QWidget>

class QComboBox;
class QLineEdit;
class QTableView;

namespace xiaoyv::tools {

class GeneratorEngine;
class ImageDocument;
class RangeEditor;

class ColorPanel final : public QWidget {
    Q_OBJECT

public:
    explicit ColorPanel(GeneratorEngine* generator, QWidget* parent = nullptr);

    void setDocument(ImageDocument* document);
    void setSelectionRange(const QRect& selection);
    void setSelectionMode(bool enabled);
    void reloadFormats();

signals:
    void selectionRangeEdited(const QRect& selection);
    void selectionModeRequested(bool enabled);
    void codeGenerated(const QString& language, const QString& code);
    void runRequested(const QString& language, const QString& code);
    void statusMessage(const QString& message);

private:
    void rebuildFormats();
    void refreshSelected();
    void refreshAll();
    void generateCode(bool runAfterGenerate);
    QVariantMap buildContext(QString* error) const;
    QString currentLanguage() const;

    GeneratorEngine* generator_ = nullptr;
    QPointer<ImageDocument> document_;
    QMetaObject::Connection selectionConnection_;
    RangeEditor* rangeEditor_ = nullptr;
    QTableView* table_ = nullptr;
    QComboBox* formatCombo_ = nullptr;
    QComboBox* directionCombo_ = nullptr;
    QLineEdit* defaultDeltaEdit_ = nullptr;
};

} // namespace xiaoyv::tools
