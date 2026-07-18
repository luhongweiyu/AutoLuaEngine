/**
 * 文件用途：实现图片工作区，集中处理标签生命周期，避免主窗口和面板复制图片状态。
 */
#include "workspace/image_workspace.h"

#include "canvas/image_canvas.h"
#include "canvas/image_editor.h"
#include "core/image_processing.h"
#include "model/image_document.h"

#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace xiaoyv::tools {
namespace {

QString unnamedImageName() {
    return QDateTime::currentDateTime().toString(QStringLiteral("yyMMdd_HHmmss"));
}

} // namespace

ImageWorkspace::ImageWorkspace(QWidget* parent)
        : QWidget(parent) {
    setAcceptDrops(true);
    pages_ = new QStackedWidget(this);
    emptyCanvas_ = new ImageCanvas(pages_);
    documents_ = new QTabWidget(pages_);
    documents_->setObjectName(QStringLiteral("imageDocuments"));
    documents_->setTabsClosable(true);
    documents_->setMovable(true);
    documents_->setDocumentMode(true);
    // 关闭按钮的默认尺寸会把图片名称标签撑高，固定高度避免再次侵占画布空间。
    documents_->tabBar()->setFixedHeight(22);
    pages_->addWidget(emptyCanvas_);
    pages_->addWidget(documents_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(pages_);

    connect(documents_, &QTabWidget::currentChanged,
            this, &ImageWorkspace::updateCurrentDocument);
    connect(documents_, &QTabWidget::tabCloseRequested,
            this, &ImageWorkspace::closeDocument);
    updateCurrentDocument();
}

ImageEditor* ImageWorkspace::currentEditor() const {
    return qobject_cast<ImageEditor*>(documents_->currentWidget());
}

ImageDocument* ImageWorkspace::currentDocument() const {
    ImageEditor* editor = currentEditor();
    return editor == nullptr ? nullptr : editor->document();
}

ImageCanvas* ImageWorkspace::currentCanvas() const {
    ImageEditor* editor = currentEditor();
    return editor == nullptr ? nullptr : editor->canvas();
}

QImage ImageWorkspace::currentDisplayedImage() const {
    ImageDocument* document = currentDocument();
    return document == nullptr ? QImage{} : document->displayedImage();
}

QRect ImageWorkspace::selectionRange() const {
    ImageDocument* document = currentDocument();
    return document == nullptr ? pendingSelection_ : document->selection();
}

bool ImageWorkspace::addImage(QImage image, const QString& filePath, const QString& displayName) {
    if (image.isNull()) {
        emit statusMessage(QString::fromUtf8("图片为空或格式不受支持"));
        return false;
    }
    const QString effectiveName = filePath.isEmpty() && displayName.isEmpty()
            ? unnamedImageName()
            : displayName;
    auto* editor = new ImageEditor(
            toRgba8888(image), filePath, effectiveName, documents_);
    if (!pendingSelection_.isNull()) {
        editor->document()->setSelection(pendingSelection_);
        pendingSelection_ = {};
    }
    connectEditor(editor);
    const int index = documents_->addTab(editor, editor->document()->tabTitle());
    documents_->setCurrentIndex(index);
    QTimer::singleShot(0, editor->canvas(), &ImageCanvas::actualSize);
    return true;
}

bool ImageWorkspace::addTemporaryImage(QImage image) {
    return addImage(std::move(image));
}

void ImageWorkspace::openImagePath(const QString& path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        emit statusMessage(QString::fromUtf8("无法打开 %1：%2").arg(path, reader.errorString()));
        return;
    }
    addImage(image, path, QFileInfo(path).fileName());
}

bool ImageWorkspace::confirmApplicationExit() {
    int temporaryCount = 0;
    int modifiedFileCount = 0;
    for (int index = 0; index < documents_->count(); ++index) {
        const auto* editor = qobject_cast<ImageEditor*>(documents_->widget(index));
        if (editor == nullptr || !editor->document()->needsExitAttention()) continue;
        if (editor->document()->isTemporary()) ++temporaryCount;
        else ++modifiedFileCount;
    }
    if (temporaryCount == 0 && modifiedFileCount == 0) return true;

    QMessageBox box(QMessageBox::Question,
                    QString::fromUtf8("退出抓图取色器"),
                    QString::fromUtf8("仍有未保存内容"),
                    QMessageBox::NoButton,
                    this);
    box.setInformativeText(QString::fromUtf8(
            "临时图片：%1 张\n已修改文件图片：%2 张\n退出后未保存内容将丢失。")
            .arg(temporaryCount)
            .arg(modifiedFileCount));
    QPushButton* exit = box.addButton(QString::fromUtf8("直接退出"), QMessageBox::DestructiveRole);
    QPushButton* cancel = box.addButton(QString::fromUtf8("取消"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.exec();
    return box.clickedButton() == exit;
}

void ImageWorkspace::openImages() {
    const QStringList paths = QFileDialog::getOpenFileNames(
            this, QString::fromUtf8("打开图片"), {},
            QString::fromUtf8(
                    "图片 (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.tif *.tiff *.ico);;所有文件 (*)"));
    for (const QString& path : paths) openImagePath(path);
}

void ImageWorkspace::saveCurrent() {
    saveDocument(currentDocument(), false);
}

void ImageWorkspace::saveCurrentAs() {
    saveDocument(currentDocument(), true);
}

void ImageWorkspace::undo() {
    if (currentDocument() != nullptr) currentDocument()->undo();
}

void ImageWorkspace::redo() {
    if (currentDocument() != nullptr) currentDocument()->redo();
}

void ImageWorkspace::rotateLeft() {
    if (currentDocument() != nullptr) currentDocument()->rotateLeft();
}

void ImageWorkspace::rotateRight() {
    if (currentDocument() != nullptr) currentDocument()->rotateRight();
}

void ImageWorkspace::flipHorizontal() {
    if (currentDocument() != nullptr) currentDocument()->flipHorizontal();
}

void ImageWorkspace::flipVertical() {
    if (currentDocument() != nullptr) currentDocument()->flipVertical();
}

void ImageWorkspace::cropSelection() {
    ImageDocument* document = currentDocument();
    if (document == nullptr) return;
    const QRect range = document->selection().intersected(document->image().rect());
    if (range.isEmpty()) {
        emit statusMessage(QString::fromUtf8("请先框选裁剪范围"));
        return;
    }
    addTemporaryImage(document->image().copy(range));
}

void ImageWorkspace::zoomIn() {
    if (currentCanvas() != nullptr) currentCanvas()->zoomIn();
}

void ImageWorkspace::zoomOut() {
    if (currentCanvas() != nullptr) currentCanvas()->zoomOut();
}

void ImageWorkspace::fitToViewport() {
    if (currentCanvas() != nullptr) currentCanvas()->fitToViewport();
}

void ImageWorkspace::actualSize() {
    if (currentCanvas() != nullptr) currentCanvas()->actualSize();
}

void ImageWorkspace::applySelectionRange(const QRect& range) {
    ImageDocument* document = currentDocument();
    if (document != nullptr) {
        document->setSelection(range);
        return;
    }
    if (pendingSelection_ == range) return;
    pendingSelection_ = range;
    emit selectionRangeChanged(range);
    emit stateChanged();
}

void ImageWorkspace::setSelectionMode(bool enabled) {
    ImageCanvas* canvas = currentCanvas();
    if (canvas == nullptr) {
        emit selectionModeChanged(false);
        if (enabled) emit statusMessage(QString::fromUtf8("请先打开图片"));
        return;
    }
    canvas->setSelectionMode(enabled);
}

void ImageWorkspace::setConfirmedSelectionVisible(bool visible) {
    if (currentCanvas() != nullptr) currentCanvas()->setConfirmedSelectionVisible(visible);
}

void ImageWorkspace::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasImage()) {
        event->acceptProposedAction();
    }
}

void ImageWorkspace::dropEvent(QDropEvent* event) {
    if (event->mimeData()->hasImage()) {
        addTemporaryImage(qvariant_cast<QImage>(event->mimeData()->imageData()));
    }
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) openImagePath(url.toLocalFile());
    }
    event->acceptProposedAction();
}

bool ImageWorkspace::saveDocument(ImageDocument* document, bool choosePath) {
    if (document == nullptr) return false;
    QString path = choosePath ? QString{} : document->filePath();
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(
                this, QString::fromUtf8("保存图片"), document->displayName(),
                QString::fromUtf8(
                        "PNG 图片 (*.png);;JPEG 图片 (*.jpg *.jpeg);;WebP 图片 (*.webp);;BMP 图片 (*.bmp)"));
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".png");
    }
    QString error;
    if (!document->save(path, &error)) {
        QMessageBox::warning(this, QString::fromUtf8("保存图片失败"), error);
        return false;
    }
    emit statusMessage(QString::fromUtf8("图片已保存：") + path);
    return true;
}

void ImageWorkspace::closeDocument(int index) {
    auto* editor = qobject_cast<ImageEditor*>(documents_->widget(index));
    if (editor == nullptr) return;
    ImageDocument* document = editor->document();
    if (!document->isTemporary() && document->isModified()) {
        const auto answer = QMessageBox::question(
                this,
                QString::fromUtf8("保存图片"),
                QString::fromUtf8("是否保存 %1？").arg(document->displayName()),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel) return;
        if (answer == QMessageBox::Save && !saveDocument(document, false)) return;
    }
    documents_->removeTab(index);
    editor->deleteLater();
}

void ImageWorkspace::connectEditor(ImageEditor* editor) {
    ImageDocument* document = editor->document();
    ImageCanvas* canvas = editor->canvas();
    connect(document, &ImageDocument::metadataChanged, this, [this, editor] {
        updateDocumentTab(editor);
        emit stateChanged();
    });
    connect(document, &ImageDocument::historyChanged,
            this, &ImageWorkspace::stateChanged);
    connect(document, &ImageDocument::selectionChanged, this, [this, editor](const QRect& range) {
        if (editor == currentEditor()) emit selectionRangeChanged(range);
        emit stateChanged();
    });
    connect(canvas, &ImageCanvas::hoverChanged, this,
            [this, editor](const QPoint& point, const QColor& color) {
                if (editor == currentEditor()) emit cursorChanged(true, point, color);
            });
    connect(canvas, &ImageCanvas::hoverLeftImage, this, [this, editor] {
        if (editor == currentEditor()) emit cursorChanged(false, {}, {});
    });
    connect(canvas, &ImageCanvas::zoomChanged, this, [this, editor](double zoom) {
        if (editor == currentEditor()) emit zoomChanged(true, zoom);
    });
    connect(canvas, &ImageCanvas::pickRequested, document->colorPoints(),
            [document](const QPoint& point, const QColor& color) {
                document->colorPoints()->addPoint(point, color);
            });
    connect(canvas, &ImageCanvas::selectionModeChanged,
            this, &ImageWorkspace::selectionModeChanged);
}

void ImageWorkspace::updateDocumentTab(ImageEditor* editor) {
    const int index = documents_->indexOf(editor);
    if (index >= 0) documents_->setTabText(index, editor->document()->tabTitle());
}

void ImageWorkspace::updateCurrentDocument() {
    ImageDocument* document = currentDocument();
    if (activeDocument_ != document) {
        // 取色器是一个连续工作台：切换图片时把当前取色点和范围直接带到下一张，
        // 不为每个标签保存互相独立的工具面板状态。
        if (activeDocument_ != nullptr) {
            carriedColorPoints_ = activeDocument_->colorPoints()->points();
            carriedSelection_ = activeDocument_->selection();
            hasCarriedToolState_ = true;
        }
        if (document != nullptr && hasCarriedToolState_) {
            document->colorPoints()->replacePoints(carriedColorPoints_);
            document->setSelection(carriedSelection_);
        } else if (document == nullptr && hasCarriedToolState_) {
            pendingSelection_ = carriedSelection_;
        }
        activeDocument_ = document;
    }
    pages_->setCurrentWidget(document == nullptr
            ? static_cast<QWidget*>(emptyCanvas_)
            : static_cast<QWidget*>(documents_));
    emit currentDocumentChanged(document);
    emit selectionRangeChanged(document == nullptr ? pendingSelection_ : document->selection());
    emit cursorChanged(false, {}, {});
    if (document == nullptr) {
        emit zoomChanged(false, 0.0);
    } else {
        emit zoomChanged(true, currentCanvas()->zoom());
    }
    emit stateChanged();
}

} // namespace xiaoyv::tools
