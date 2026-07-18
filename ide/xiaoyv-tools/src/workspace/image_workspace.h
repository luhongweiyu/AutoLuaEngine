/**
 * 文件用途：声明图片工作区，唯一负责图片标签、待用框选、保存关闭和画布交互状态。
 */
#pragma once

#include <QImage>
#include <QPointer>
#include <QRect>
#include <QVector>
#include <QWidget>

#include "model/color_point_model.h"

class QDragEnterEvent;
class QDropEvent;
class QStackedWidget;
class QTabWidget;

namespace xiaoyv::tools {

class ImageCanvas;
class ImageDocument;
class ImageEditor;

class ImageWorkspace final : public QWidget {
    Q_OBJECT

public:
    explicit ImageWorkspace(QWidget* parent = nullptr);

    ImageEditor* currentEditor() const;
    ImageDocument* currentDocument() const;
    ImageCanvas* currentCanvas() const;
    QImage currentDisplayedImage() const;
    QRect selectionRange() const;

    /** 加入文件图片或临时图片；空路径且无名称时自动使用短时间戳命名。 */
    bool addImage(QImage image, const QString& filePath = {}, const QString& displayName = {});
    /** 以统一短时间戳命名加入截图、裁剪或拖入图片。 */
    bool addTemporaryImage(QImage image);
    void openImagePath(const QString& path);
    /** 退出应用前汇总图片状态；单个临时标签关闭时不调用此确认。 */
    bool confirmApplicationExit();

public slots:
    void openImages();
    void saveCurrent();
    void saveCurrentAs();
    void undo();
    void redo();
    void rotateLeft();
    void rotateRight();
    void flipHorizontal();
    void flipVertical();
    void cropSelection();
    void zoomIn();
    void zoomOut();
    void fitToViewport();
    void actualSize();
    void applySelectionRange(const QRect& range);
    void setSelectionMode(bool enabled);
    void setConfirmedSelectionVisible(bool visible);

signals:
    void currentDocumentChanged(ImageDocument* document);
    /** 图片、历史、范围、标签或当前页变化后发出，外壳据此刷新动作和状态栏。 */
    void stateChanged();
    void selectionRangeChanged(const QRect& range);
    void selectionModeChanged(bool enabled);
    void cursorChanged(bool valid, const QPoint& point, const QColor& color);
    void zoomChanged(bool valid, double zoom);
    void statusMessage(const QString& message);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    bool saveDocument(ImageDocument* document, bool choosePath);
    void closeDocument(int index);
    void connectEditor(ImageEditor* editor);
    void updateDocumentTab(ImageEditor* editor);
    void updateCurrentDocument();

    QStackedWidget* pages_ = nullptr;
    ImageCanvas* emptyCanvas_ = nullptr;
    QTabWidget* documents_ = nullptr;
    QPointer<ImageDocument> activeDocument_;
    QVector<ColorPoint> carriedColorPoints_;
    QRect carriedSelection_;
    bool hasCarriedToolState_ = false;
    /** 没有图片时唯一保存的待用范围，下一张图片采用后立即清空。 */
    QRect pendingSelection_;
};

} // namespace xiaoyv::tools
