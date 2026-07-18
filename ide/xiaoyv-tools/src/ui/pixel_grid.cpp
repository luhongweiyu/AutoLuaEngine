/**
 * 文件用途：实现不会随点阵宽高缩放的 6 x 6 固定格子预览和显式编辑。
 */
#include "ui/pixel_grid.h"

#include <QMouseEvent>
#include <QPainter>

#include <cmath>

namespace xiaoyv::tools {

PixelGrid::PixelGrid(QWidget* parent)
        : QWidget(parent) {
    setMinimumSize(80, 72);
    setEditable(false);
}

void PixelGrid::setEditable(bool editable) {
    editable_ = editable;
    setCursor(editable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setToolTip(editable
            ? QString::fromUtf8("单击格子切换前景点")
            : QString::fromUtf8("点阵已锁定，勾选“允许编辑”后才能修改"));
}

void PixelGrid::setMask(int width, int height, std::vector<std::uint8_t> mask) {
    width_ = width;
    height_ = height;
    mask_ = std::move(mask);
    const QSize contentSize = sizeHint();
    setMinimumSize(contentSize);
    // 点阵网格位于不可自动拉伸的滚动区内；显式调整实际尺寸，重新提取后才能立即刷新滚动范围。
    resize(contentSize);
    updateGeometry();
    update();
}

void PixelGrid::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Base));
    if (width_ <= 0 || height_ <= 0 || mask_.empty()) return;
    painter.setPen(QPen(palette().color(QPalette::Mid), 0));
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            const QRect cell(
                    kPadding + x * kCellSize,
                    kPadding + y * kCellSize,
                    kCellSize,
                    kCellSize);
            painter.fillRect(cell, mask_[static_cast<std::size_t>(y) * width_ + x] != 0
                    ? palette().color(QPalette::Text)
                    : palette().color(QPalette::AlternateBase));
            painter.drawRect(cell);
        }
    }
}

void PixelGrid::mousePressEvent(QMouseEvent* event) {
    if (!editable_ || event->button() != Qt::LeftButton || width_ <= 0 || height_ <= 0) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPointF local = event->position() - QPointF(kPadding, kPadding);
    const int x = static_cast<int>(std::floor(local.x() / kCellSize));
    const int y = static_cast<int>(std::floor(local.y() / kCellSize));
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    const std::size_t offset = static_cast<std::size_t>(y) * width_ + x;
    mask_[offset] = mask_[offset] == 0 ? 1 : 0;
    update();
    emit maskChanged(width_, height_, mask_);
}

QSize PixelGrid::sizeHint() const {
    return {
            std::max(80, width_ * kCellSize + kPadding * 2),
            std::max(72, height_ * kCellSize + kPadding * 2),
    };
}

} // namespace xiaoyv::tools
