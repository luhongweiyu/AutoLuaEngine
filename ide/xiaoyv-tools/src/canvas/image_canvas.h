/**
 * 文件用途：声明图片画布，负责固定左上角显示、缩放、像素网格、跟随放大镜和精确框选。
 */
#pragma once

#include <QAbstractScrollArea>
#include <QMetaObject>
#include <QPointer>

namespace xiaoyv::tools {

class ImageDocument;

class ImageCanvas final : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit ImageCanvas(QWidget* parent = nullptr);

    void setDocument(ImageDocument* document);
    ImageDocument* document() const;
    double zoom() const;
    /** 当前精确指向的图片坐标；不在图片内时返回 (-1,-1)。 */
    QPoint currentImagePosition() const;
    void zoomIn();
    void zoomOut();
    void actualSize();
    void fitToViewport();
    void setSelectionMode(bool enabled);
    bool selectionMode() const;
    /** 点阵页激活时显示已确认范围，其他页面不额外覆盖图片。 */
    void setConfirmedSelectionVisible(bool visible);

signals:
    void zoomChanged(double zoom);
    void hoverChanged(const QPoint& imagePosition, const QColor& color);
    void hoverLeftImage();
    void pickRequested(const QPoint& imagePosition, const QColor& color);
    void selectionModeChanged(bool enabled);
    void selectionCompleted(const QRect& selection);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void reconnectDocument();
    void updateScrollBars();
    void setZoom(double value);
    QPoint viewportToImage(const QPoint& position) const;
    QPoint viewportToImageClamped(const QPoint& position) const;
    QPoint imageToViewport(const QPoint& position) const;
    QRect imageRectInViewport() const;
    bool updateHover(const QPoint& viewportPosition);
    void moveLogicalCursor(const QPoint& delta);
    void drawCanvasBackground(QPainter* painter) const;
    void drawImage(QPainter* painter) const;
    void drawPixelGrid(QPainter* painter) const;
    void drawSelection(QPainter* painter) const;
    void drawMagnifier(QPainter* painter) const;

    QPointer<ImageDocument> document_;
    QMetaObject::Connection imageConnection_;
    QMetaObject::Connection selectionConnection_;
    double zoom_ = 1.0;
    bool selectionMode_ = false;
    bool selecting_ = false;
    bool confirmedSelectionVisible_ = false;
    QPoint selectionAnchor_;
    QRect transientSelection_;
    QPoint hoverImagePosition_ = {-1, -1};
    QPoint hoverViewportPosition_;
    bool ignoringProgrammaticMouseMove_ = false;
};

} // namespace xiaoyv::tools
