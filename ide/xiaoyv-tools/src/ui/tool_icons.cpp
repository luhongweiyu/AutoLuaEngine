/**
 * 文件用途：使用 QPainter 在目标 DPI 直接绘制工具图标，避免缩放位图造成轮廓发虚。
 */
#include "ui/tool_icons.h"

#include <QFont>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QTransform>

namespace xiaoyv::tools {
namespace {

void arrowHead(QPainter& painter, const QPointF& tip, const QPointF& left, const QPointF& right) {
    const QColor color = painter.pen().color();
    QPainterPath path;
    path.moveTo(tip);
    path.lineTo(left);
    path.lineTo(right);
    path.closeSubpath();
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPath(path);
    painter.restore();
}

// 使用重构前的实心箭头轮廓，保持撤销和重做在小尺寸下仍然容易辨认。
void drawUndoRedo(QPainter& painter, bool redo) {
    painter.save();
    if (redo) {
        painter.translate(24, 0);
        painter.scale(-1, 1);
    }
    const QColor color = painter.pen().color();
    QPolygonF arrow{
            QPointF(3, 9), QPointF(9, 4), QPointF(9, 7.5),
            QPointF(14, 7.5), QPointF(18.5, 11.5), QPointF(18.5, 19),
            QPointF(15.5, 19), QPointF(15.5, 12.8), QPointF(13, 10.5),
            QPointF(9, 10.5), QPointF(9, 14),
    };
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(arrow);
    painter.restore();
}

// 使用重构前的直角旋转箭头和方块组合，避免圆弧图案在小尺寸下不易识别。
void drawRotate(QPainter& painter, bool clockwise) {
    painter.save();
    if (clockwise) {
        painter.translate(24, 0);
        painter.scale(-1, 1);
    }
    const QColor color = painter.pen().color();
    QPainterPath arrowPath;
    arrowPath.moveTo(19, 20);
    arrowPath.lineTo(19, 10);
    arrowPath.quadTo(19, 7, 16, 7);
    arrowPath.lineTo(8, 7);
    painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(arrowPath);
    arrowHead(painter, QPointF(4, 7), QPointF(9, 3), QPointF(9, 11));
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.drawRect(QRectF(7, 12, 8, 8));
    painter.restore();
}

// 使用重构前的齿轮路径，而不是两个同心圆，保证设置图标确实呈现齿轮轮廓。
void drawGear(QPainter& painter, const QColor& color) {
    QPainterPath gear;
    gear.addEllipse(QRectF(5, 5, 14, 14));
    QPainterPath tooth;
    tooth.addRect(QRectF(10.5, 1.5, 3, 5));
    for (int index = 0; index < 8; ++index) {
        QTransform transform;
        transform.translate(12, 12);
        transform.rotate(index * 45.0);
        transform.translate(-12, -12);
        gear = gear.united(transform.map(tooth));
    }
    QPainterPath center;
    center.addEllipse(QRectF(9, 9, 6, 6));
    painter.fillPath(gear.subtracted(center), color);
}

void drawToolIcon(QPainter& painter, ToolIcon icon, bool darkTheme, bool disabled) {
    // 工具图标按像素网格绘制，避免抗锯齿产生灰色过渡边缘。
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::TextAntialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const QColor color = disabled
            ? (darkTheme ? QColor(QStringLiteral("#777777"))
                         : QColor(QStringLiteral("#888888")))
            : (darkTheme ? QColor(QStringLiteral("#E6E6E6"))
                         : QColor(QStringLiteral("#202124")));
    QPen pen(color, 1.6, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
        case ToolIcon::Open: {
            QPainterPath path;
            path.moveTo(3, 7); path.lineTo(9, 7); path.lineTo(11, 9); path.lineTo(21, 9);
            path.lineTo(18, 19); path.lineTo(4, 19); path.closeSubpath();
            painter.drawPath(path);
            painter.drawLine(4, 7, 4, 5); painter.drawLine(4, 5, 10, 5); painter.drawLine(10, 5, 12, 7);
            break;
        }
        case ToolIcon::Save:
            painter.drawRoundedRect(QRectF(4, 3, 16, 18), 1.5, 1.5);
            painter.drawRect(QRectF(7, 3, 9, 6)); painter.drawRect(QRectF(7, 13, 10, 8));
            break;
        case ToolIcon::Undo:
        case ToolIcon::Redo: {
            drawUndoRedo(painter, icon == ToolIcon::Redo);
            break;
        }
        case ToolIcon::DeviceScreenshot:
            painter.drawRoundedRect(QRectF(6, 2.5, 12, 19), 2, 2);
            painter.drawEllipse(QPointF(12, 12), 3.2, 3.2); painter.drawEllipse(QPointF(12, 12), 1, 1);
            painter.drawLine(10, 5, 14, 5);
            break;
        case ToolIcon::FloatingScreenshot:
            painter.drawRect(QRectF(4, 5, 16, 14));
            painter.drawLine(2, 9, 2, 3); painter.drawLine(2, 3, 8, 3);
            painter.drawLine(22, 15, 22, 21); painter.drawLine(22, 21, 16, 21);
            break;
        case ToolIcon::Projection:
            painter.drawRoundedRect(QRectF(3, 4, 18, 13), 1.5, 1.5);
            painter.drawLine(8, 21, 16, 21); painter.drawLine(12, 17, 12, 21);
            painter.drawLine(12, 14, 12, 7); painter.setBrush(color);
            arrowHead(painter, {12, 6}, {9, 9}, {15, 9});
            break;
        case ToolIcon::RotateLeft:
        case ToolIcon::RotateRight: {
            drawRotate(painter, icon == ToolIcon::RotateRight);
            break;
        }
        case ToolIcon::FlipHorizontal:
            painter.drawLine(12, 3, 12, 21); painter.drawLine(3, 12, 9, 7); painter.drawLine(3, 12, 9, 17);
            painter.drawLine(21, 12, 15, 7); painter.drawLine(21, 12, 15, 17); break;
        case ToolIcon::FlipVertical:
            painter.drawLine(3, 12, 21, 12); painter.drawLine(12, 3, 7, 9); painter.drawLine(12, 3, 17, 9);
            painter.drawLine(12, 21, 7, 15); painter.drawLine(12, 21, 17, 15); break;
        case ToolIcon::Crop:
            painter.drawLine(7, 3, 7, 17); painter.drawLine(7, 17, 21, 17);
            painter.drawLine(3, 7, 17, 7); painter.drawLine(17, 7, 17, 21); break;
        case ToolIcon::ZoomIn:
        case ToolIcon::ZoomOut:
            painter.drawEllipse(QPointF(10, 10), 6, 6); painter.drawLine(14.5, 14.5, 21, 21);
            painter.drawLine(7, 10, 13, 10);
            if (icon == ToolIcon::ZoomIn) painter.drawLine(10, 7, 10, 13);
            break;
        case ToolIcon::Fit:
            painter.drawLine(3, 9, 3, 3); painter.drawLine(3, 3, 9, 3);
            painter.drawLine(15, 3, 21, 3); painter.drawLine(21, 3, 21, 9);
            painter.drawLine(3, 15, 3, 21); painter.drawLine(3, 21, 9, 21);
            painter.drawLine(15, 21, 21, 21); painter.drawLine(21, 21, 21, 15); break;
        case ToolIcon::ActualSize: {
            QFont font = painter.font();
            font.setFamily(QStringLiteral("Arial"));
            // 工具栏会把 24 单位图标缩放到约 18 像素，使用 16 单位字号后，
            // 最终的 1:1 仍有约 12 像素高，不会缩成难以辨认的小字。
            font.setPixelSize(16);
            font.setBold(true);
            font.setHintingPreference(QFont::PreferFullHinting);
            font.setStyleStrategy(QFont::NoAntialias);
            painter.setFont(font);
            painter.drawText(QRectF(0, 2, 24, 20), Qt::AlignCenter, QStringLiteral("1:1")); break;
        }
        case ToolIcon::Settings:
            drawGear(painter, color);
            break;
        case ToolIcon::Stop:
            painter.setBrush(color); painter.drawRoundedRect(QRectF(6, 6, 12, 12), 1.5, 1.5); break;
    }
}

/**
 * 在 QIcon 请求的最终尺寸上直接绘制，避免先生成 24px 位图再缩放到工具栏尺寸。
 * 这样无抗锯齿策略不会被 QIcon 的二次缩放重新引入灰色边缘。
 */
class ToolIconEngine final : public QIconEngine {
public:
    ToolIconEngine(ToolIcon icon, bool darkTheme)
            : icon_(icon), darkTheme_(darkTheme) {}

    QIconEngine* clone() const override {
        return new ToolIconEngine(icon_, darkTheme_);
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override {
        Q_UNUSED(state)
        if (painter == nullptr || rect.isEmpty()) return;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, false);
        painter->setRenderHint(QPainter::TextAntialiasing, false);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter->translate(rect.left(), rect.top());
        painter->scale(rect.width() / 24.0, rect.height() / 24.0);
        drawToolIcon(*painter, icon_, darkTheme_, mode == QIcon::Disabled);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        painter.end();
        return result;
    }

    QPixmap scaledPixmap(
            const QSize& size,
            QIcon::Mode mode,
            QIcon::State state,
            qreal scale) override {
        const QSize physicalSize(
                qMax(1, qRound(size.width() * scale)),
                qMax(1, qRound(size.height() * scale)));
        QPixmap result(physicalSize);
        result.setDevicePixelRatio(scale);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        painter.end();
        return result;
    }

private:
    ToolIcon icon_;
    bool darkTheme_ = true;
};

} // namespace

QIcon makeToolIcon(ToolIcon icon, bool darkTheme) {
    return QIcon(new ToolIconEngine(icon, darkTheme));
}

} // namespace xiaoyv::tools
