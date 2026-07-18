/**
 * 文件用途：实现不会自由拖图的像素画布，并保持键盘方向、鼠标坐标和图片坐标严格一致。
 */
#include "canvas/image_canvas.h"

#include "model/image_document.h"

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace xiaoyv::tools {
namespace {

constexpr double kMinimumZoom = 0.125;
constexpr double kMaximumZoom = 32.0;
constexpr int kMagnifierCells = 23;
constexpr int kMagnifierCellSize = 6;

double nextZoom(double current, bool increase) {
    static constexpr double values[] = {
            0.125, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0,
            4.0, 6.0, 8.0, 12.0, 16.0, 24.0, 32.0,
    };
    if (increase) {
        for (double value : values) if (value > current + 0.0001) return value;
        return kMaximumZoom;
    }
    for (auto iterator = std::rbegin(values); iterator != std::rend(values); ++iterator) {
        if (*iterator < current - 0.0001) return *iterator;
    }
    return kMinimumZoom;
}

} // namespace

ImageCanvas::ImageCanvas(QWidget* parent)
        : QAbstractScrollArea(parent) {
    setObjectName(QStringLiteral("imageCanvas"));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    horizontalScrollBar()->setSingleStep(24);
    verticalScrollBar()->setSingleStep(24);
}

void ImageCanvas::setDocument(ImageDocument* document) {
    if (document_ == document) return;
    disconnect(imageConnection_);
    disconnect(selectionConnection_);
    document_ = document;
    hoverImagePosition_ = {-1, -1};
    transientSelection_ = {};
    selecting_ = false;
    reconnectDocument();
    updateScrollBars();
    viewport()->update();
}

ImageDocument* ImageCanvas::document() const {
    return document_;
}

double ImageCanvas::zoom() const {
    return zoom_;
}

QPoint ImageCanvas::currentImagePosition() const {
    return hoverImagePosition_;
}

void ImageCanvas::zoomIn() {
    setZoom(nextZoom(zoom_, true));
}

void ImageCanvas::zoomOut() {
    setZoom(nextZoom(zoom_, false));
}

void ImageCanvas::actualSize() {
    setZoom(1.0);
    horizontalScrollBar()->setValue(0);
    verticalScrollBar()->setValue(0);
}

void ImageCanvas::fitToViewport() {
    if (document_ == nullptr || document_->displayedImage().isNull()) return;
    const QSize imageSize = document_->displayedImage().size();
    const double horizontal = static_cast<double>(viewport()->width()) / imageSize.width();
    const double vertical = static_cast<double>(viewport()->height()) / imageSize.height();
    setZoom(std::clamp(std::min(horizontal, vertical), kMinimumZoom, kMaximumZoom));
    horizontalScrollBar()->setValue(0);
    verticalScrollBar()->setValue(0);
}

void ImageCanvas::setSelectionMode(bool enabled) {
    if (selectionMode_ == enabled) return;
    selectionMode_ = enabled;
    selecting_ = false;
    transientSelection_ = {};
    viewport()->setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    viewport()->update();
    emit selectionModeChanged(enabled);
}

bool ImageCanvas::selectionMode() const {
    return selectionMode_;
}

void ImageCanvas::setConfirmedSelectionVisible(bool visible) {
    if (confirmedSelectionVisible_ == visible) return;
    confirmedSelectionVisible_ = visible;
    viewport()->update();
}

void ImageCanvas::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    drawCanvasBackground(&painter);
    drawImage(&painter);
    drawPixelGrid(&painter);
    drawSelection(&painter);
    drawMagnifier(&painter);
}

void ImageCanvas::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void ImageCanvas::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    if (selectionMode_ && document_ != nullptr && event->button() == Qt::LeftButton) {
        selecting_ = true;
        selectionAnchor_ = viewportToImageClamped(event->position().toPoint());
        transientSelection_ = QRect(selectionAnchor_, selectionAnchor_);
        viewport()->update();
        event->accept();
        return;
    }
    // 普通左键只更新精确光标位置，不采集取色点；取色仍只由回车或空格触发。
    updateHover(event->position().toPoint());
    if (event->button() == Qt::LeftButton) {
        event->accept();
        return;
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void ImageCanvas::mouseMoveEvent(QMouseEvent* event) {
    const QPoint position = event->position().toPoint();
    if (ignoringProgrammaticMouseMove_ && !selecting_) {
        event->accept();
        return;
    }
    if (selecting_ && document_ != nullptr) {
        transientSelection_ = QRect(selectionAnchor_, viewportToImageClamped(position)).normalized();
        updateHover(position);
        viewport()->update();
        event->accept();
        return;
    }
    updateHover(position);
    QAbstractScrollArea::mouseMoveEvent(event);
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (selecting_ && event->button() == Qt::LeftButton) {
        selecting_ = false;
        const QRect completed = transientSelection_;
        transientSelection_ = {};
        setSelectionMode(false);
        if (document_ != nullptr && !completed.isNull()) {
            document_->setSelection(completed);
            emit selectionCompleted(completed);
        }
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void ImageCanvas::leaveEvent(QEvent* event) {
    if (!selecting_) {
        hoverImagePosition_ = {-1, -1};
        emit hoverLeftImage();
        viewport()->update();
    }
    QAbstractScrollArea::leaveEvent(event);
}

void ImageCanvas::keyPressEvent(QKeyEvent* event) {
    QPoint delta;
    switch (event->key()) {
        case Qt::Key_Left: delta = {-1, 0}; break;
        case Qt::Key_Right: delta = {1, 0}; break;
        case Qt::Key_Up: delta = {0, -1}; break;
        case Qt::Key_Down: delta = {0, 1}; break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            if (document_ != nullptr && document_->displayedImage().rect().contains(hoverImagePosition_)) {
                emit pickRequested(
                        hoverImagePosition_,
                        document_->displayedImage().pixelColor(hoverImagePosition_));
            }
            event->accept();
            return;
        default:
            QAbstractScrollArea::keyPressEvent(event);
            return;
    }
    moveLogicalCursor(delta);
    event->accept();
}

void ImageCanvas::reconnectDocument() {
    if (document_ == nullptr) return;
    imageConnection_ = connect(document_, &ImageDocument::imageChanged, this, [this] {
        updateScrollBars();
        viewport()->update();
    });
    selectionConnection_ = connect(document_, &ImageDocument::selectionChanged, this, [this] {
        viewport()->update();
    });
}

void ImageCanvas::updateScrollBars() {
    QSize content;
    if (document_ != nullptr && !document_->displayedImage().isNull()) {
        content = QSize(
                qRound(document_->displayedImage().width() * zoom_),
                qRound(document_->displayedImage().height() * zoom_));
    }
    horizontalScrollBar()->setPageStep(viewport()->width());
    verticalScrollBar()->setPageStep(viewport()->height());
    horizontalScrollBar()->setRange(0, std::max(0, content.width() - viewport()->width()));
    verticalScrollBar()->setRange(0, std::max(0, content.height() - viewport()->height()));
}

void ImageCanvas::setZoom(double value) {
    const double bounded = std::clamp(value, kMinimumZoom, kMaximumZoom);
    if (qFuzzyCompare(zoom_, bounded)) return;

    const QPointF centerImage(
            (horizontalScrollBar()->value() + viewport()->width() / 2.0) / zoom_,
            (verticalScrollBar()->value() + viewport()->height() / 2.0) / zoom_);
    zoom_ = bounded;
    updateScrollBars();
    horizontalScrollBar()->setValue(qRound(centerImage.x() * zoom_ - viewport()->width() / 2.0));
    verticalScrollBar()->setValue(qRound(centerImage.y() * zoom_ - viewport()->height() / 2.0));
    viewport()->update();
    emit zoomChanged(zoom_);
}

QPoint ImageCanvas::viewportToImage(const QPoint& position) const {
    return {
            static_cast<int>(std::floor((position.x() + horizontalScrollBar()->value()) / zoom_)),
            static_cast<int>(std::floor((position.y() + verticalScrollBar()->value()) / zoom_)),
    };
}

QPoint ImageCanvas::viewportToImageClamped(const QPoint& position) const {
    if (document_ == nullptr || document_->displayedImage().isNull()) return {};
    const QPoint point = viewportToImage(position);
    return {
            std::clamp(point.x(), 0, document_->displayedImage().width() - 1),
            std::clamp(point.y(), 0, document_->displayedImage().height() - 1),
    };
}

QPoint ImageCanvas::imageToViewport(const QPoint& position) const {
    return {
            static_cast<int>(std::floor((position.x() + 0.5) * zoom_))
                    - horizontalScrollBar()->value(),
            static_cast<int>(std::floor((position.y() + 0.5) * zoom_))
                    - verticalScrollBar()->value(),
    };
}

QRect ImageCanvas::imageRectInViewport() const {
    if (document_ == nullptr || document_->displayedImage().isNull()) return {};
    return {
            -horizontalScrollBar()->value(),
            -verticalScrollBar()->value(),
            qRound(document_->displayedImage().width() * zoom_),
            qRound(document_->displayedImage().height() * zoom_),
    };
}

bool ImageCanvas::updateHover(const QPoint& viewportPosition) {
    hoverViewportPosition_ = viewportPosition;
    if (document_ == nullptr || document_->displayedImage().isNull()) return false;
    const QPoint imagePosition = viewportToImage(viewportPosition);
    if (!document_->displayedImage().rect().contains(imagePosition)) {
        if (hoverImagePosition_.x() >= 0) emit hoverLeftImage();
        hoverImagePosition_ = {-1, -1};
        viewport()->update();
        return false;
    }
    hoverImagePosition_ = imagePosition;
    emit hoverChanged(imagePosition, document_->displayedImage().pixelColor(imagePosition));
    viewport()->update();
    return true;
}

void ImageCanvas::moveLogicalCursor(const QPoint& delta) {
    if (document_ == nullptr || document_->displayedImage().isNull()) return;
    QPoint current = hoverImagePosition_;
    if (!document_->displayedImage().rect().contains(current)) current = {0, 0};
    current += delta;
    current.setX(std::clamp(current.x(), 0, document_->displayedImage().width() - 1));
    current.setY(std::clamp(current.y(), 0, document_->displayedImage().height() - 1));

    const QPoint viewportPoint = imageToViewport(current);
    hoverImagePosition_ = current;
    hoverViewportPosition_ = viewportPoint;
    emit hoverChanged(current, document_->displayedImage().pixelColor(current));
    viewport()->update();

    // 系统移动鼠标后会再产生一次 MouseMove；该事件可能因缩放或 DPI 舍入回到相邻像素。
    // 在当前事件循环内忽略它，逻辑坐标始终以键盘移动结果为准。
    ignoringProgrammaticMouseMove_ = true;
    QCursor::setPos(viewport()->mapToGlobal(viewportPoint));
    QTimer::singleShot(0, this, [this] { ignoringProgrammaticMouseMove_ = false; });
}

void ImageCanvas::drawCanvasBackground(QPainter* painter) const {
    painter->fillRect(viewport()->rect(), QColor(QStringLiteral("#626262")));
    painter->setPen(QPen(QColor(QStringLiteral("#696969")), 1));
    for (int x = 0; x < viewport()->width(); x += 8) painter->drawLine(x, 0, x, viewport()->height());
    for (int y = 0; y < viewport()->height(); y += 8) painter->drawLine(0, y, viewport()->width(), y);
    painter->setPen(QPen(QColor(QStringLiteral("#747474")), 1));
    for (int x = 0; x < viewport()->width(); x += 64) painter->drawLine(x, 0, x, viewport()->height());
    for (int y = 0; y < viewport()->height(); y += 64) painter->drawLine(0, y, viewport()->width(), y);
}

void ImageCanvas::drawImage(QPainter* painter) const {
    if (document_ == nullptr || document_->displayedImage().isNull()) return;
    painter->drawImage(imageRectInViewport(), document_->displayedImage());
}

void ImageCanvas::drawPixelGrid(QPainter* painter) const {
    if (zoom_ < 8.0 || document_ == nullptr || document_->displayedImage().isNull()) return;
    const QRect visible = viewport()->rect().intersected(imageRectInViewport());
    if (visible.isEmpty()) return;
    const int firstX = std::max(0, viewportToImage(visible.topLeft()).x());
    const int firstY = std::max(0, viewportToImage(visible.topLeft()).y());
    const int lastX = std::min(document_->displayedImage().width(), viewportToImage(visible.bottomRight()).x() + 2);
    const int lastY = std::min(document_->displayedImage().height(), viewportToImage(visible.bottomRight()).y() + 2);
    painter->setPen(QPen(QColor(32, 32, 32, 130), 0));
    for (int x = firstX; x <= lastX; ++x) {
        const int viewX = qRound(x * zoom_) - horizontalScrollBar()->value();
        painter->drawLine(viewX, visible.top(), viewX, visible.bottom());
    }
    for (int y = firstY; y <= lastY; ++y) {
        const int viewY = qRound(y * zoom_) - verticalScrollBar()->value();
        painter->drawLine(visible.left(), viewY, visible.right(), viewY);
    }
}

void ImageCanvas::drawSelection(QPainter* painter) const {
    QRect selection = selecting_ ? transientSelection_
            : (confirmedSelectionVisible_ && document_ != nullptr ? document_->selection() : QRect{});
    if (selection.isNull()) return;
    const QRect viewRect(
            QPoint(qRound(selection.left() * zoom_) - horizontalScrollBar()->value(),
                   qRound(selection.top() * zoom_) - verticalScrollBar()->value()),
            QPoint(qRound((selection.right() + 1) * zoom_) - horizontalScrollBar()->value() - 1,
                   qRound((selection.bottom() + 1) * zoom_) - verticalScrollBar()->value() - 1));
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(Qt::black, 1, Qt::DashLine));
    painter->drawRect(viewRect);
    painter->setPen(QPen(Qt::white, 1, Qt::DashLine, Qt::SquareCap, Qt::RoundJoin));
    painter->drawRect(viewRect.adjusted(1, 1, -1, -1));
}

void ImageCanvas::drawMagnifier(QPainter* painter) const {
    if (document_ == nullptr || !document_->displayedImage().rect().contains(hoverImagePosition_)
            || selecting_) {
        return;
    }
    const int extent = kMagnifierCells * kMagnifierCellSize;
    QPoint origin = hoverViewportPosition_ + QPoint(18, 18);
    if (origin.x() + extent + 2 > viewport()->width()) origin.setX(hoverViewportPosition_.x() - extent - 18);
    if (origin.y() + extent + 2 > viewport()->height()) origin.setY(hoverViewportPosition_.y() - extent - 18);
    origin.setX(std::max(2, origin.x()));
    origin.setY(std::max(2, origin.y()));

    const QRect frame(origin, QSize(extent + 1, extent + 1));
    painter->fillRect(frame.adjusted(-2, -2, 2, 2), QColor(20, 20, 20, 225));
    const int radius = kMagnifierCells / 2;
    for (int row = 0; row < kMagnifierCells; ++row) {
        for (int column = 0; column < kMagnifierCells; ++column) {
            const QPoint sample = hoverImagePosition_ + QPoint(column - radius, row - radius);
            const QColor color = document_->displayedImage().rect().contains(sample)
                    ? document_->displayedImage().pixelColor(sample)
                    : QColor(32, 32, 32);
            painter->fillRect(
                    QRect(origin.x() + column * kMagnifierCellSize,
                          origin.y() + row * kMagnifierCellSize,
                          kMagnifierCellSize, kMagnifierCellSize),
                    color);
        }
    }
    painter->setPen(QPen(QColor(0, 0, 0, 130), 1));
    for (int index = 0; index <= kMagnifierCells; ++index) {
        const int offset = index * kMagnifierCellSize;
        painter->drawLine(origin.x() + offset, origin.y(), origin.x() + offset, origin.y() + extent);
        painter->drawLine(origin.x(), origin.y() + offset, origin.x() + extent, origin.y() + offset);
    }
    const QRect center(
            origin.x() + radius * kMagnifierCellSize,
            origin.y() + radius * kMagnifierCellSize,
            kMagnifierCellSize,
            kMagnifierCellSize);
    painter->setPen(QPen(Qt::red, 2));
    painter->drawRect(center.adjusted(0, 0, -1, -1));
}

} // namespace xiaoyv::tools
