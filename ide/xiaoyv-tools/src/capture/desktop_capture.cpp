/**
 * 文件用途：实现可重复浮动抓图，以及 Windows 原生窗口句柄选择和 PrintWindow 抓取。
 */
#include "capture/desktop_capture.h"

#include <QApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <dwmapi.h>
#include <windows.h>
#endif

#include <algorithm>

namespace xiaoyv::tools {
namespace {

QImage grabDesktopRect(const QRect& globalRect) {
    if (globalRect.isEmpty()) return {};
    QImage result(globalRect.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    bool captured = false;
    for (QScreen* screen : QGuiApplication::screens()) {
        const QRect intersection = globalRect.intersected(screen->geometry());
        if (intersection.isEmpty()) continue;
        const QPoint local = intersection.topLeft() - screen->geometry().topLeft();
        const QPixmap pixmap = screen->grabWindow(
                0, local.x(), local.y(), intersection.width(), intersection.height());
        if (pixmap.isNull()) continue;
        painter.drawPixmap(intersection.topLeft() - globalRect.topLeft(), pixmap);
        captured = true;
    }
    return captured ? result : QImage{};
}

QIcon targetIcon(bool dark, bool active) {
    constexpr int size = 24;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor color = active ? QColor(QStringLiteral("#E5484D"))
                                : (dark ? QColor(QStringLiteral("#E5E7EB"))
                                        : QColor(QStringLiteral("#202124")));
    painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(12, 12), 6.5, 6.5);
    painter.drawEllipse(QPointF(12, 12), 2.0, 2.0);
    painter.drawLine(12, 2, 12, 7);
    painter.drawLine(12, 17, 12, 22);
    painter.drawLine(2, 12, 7, 12);
    painter.drawLine(17, 12, 22, 12);
    return QIcon(pixmap);
}

#ifdef Q_OS_WIN

QString windowDescription(HWND window) {
    wchar_t title[512]{};
    wchar_t className[256]{};
    GetWindowTextW(window, title, 511);
    GetClassNameW(window, className, 255);
    RECT rect{};
    GetWindowRect(window, &rect);
    const QString visibleTitle = QString::fromWCharArray(title).trimmed();
    return QString::fromUtf8("目标：%1  [%2]  %3 x %4")
            .arg(visibleTitle.isEmpty() ? QString::fromUtf8("无标题窗口") : visibleTitle)
            .arg(QString::fromWCharArray(className))
            .arg(rect.right - rect.left)
            .arg(rect.bottom - rect.top);
}

QImage captureWindow(HWND window, QString* error) {
    if (window == nullptr || !IsWindow(window)) {
        if (error != nullptr) *error = QString::fromUtf8("尚未指定有效窗口");
        return {};
    }
    RECT rect{};
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
        GetWindowRect(window, &rect);
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        if (error != nullptr) *error = QString::fromUtf8("目标窗口尺寸无效");
        return {};
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDc = GetDC(nullptr);
    HDC memoryDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ previous = bitmap == nullptr ? nullptr : SelectObject(memoryDc, bitmap);
    const BOOL printed = bitmap != nullptr
            && PrintWindow(window, memoryDc, 0x00000002 /* PW_RENDERFULLCONTENT */);

    QImage result;
    if (printed) {
        QImage view(static_cast<uchar*>(bits), width, height, width * 4, QImage::Format_ARGB32);
        result = view.convertToFormat(QImage::Format_RGB32);
    } else if (error != nullptr) {
        *error = QString::fromUtf8("系统无法抓取该窗口，目标可能禁止 PrintWindow");
    }

    if (previous != nullptr) SelectObject(memoryDc, previous);
    if (bitmap != nullptr) DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);
    return result;
}

#endif

} // namespace

FloatingCaptureWindow::FloatingCaptureWindow(QWidget* parent)
        : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setMinimumSize(120, 90);
    resize(420, 280);

    captureButton_ = new QToolButton(this);
    captureButton_->setText(QString::fromUtf8("抓图"));
    captureButton_->setToolTip(QString::fromUtf8("抓取框内画面"));
    captureButton_->setAutoRaise(false);
    closeButton_ = new QToolButton(this);
    closeButton_->setText(QStringLiteral("×"));
    closeButton_->setToolTip(QString::fromUtf8("关闭浮动抓图"));
    connect(captureButton_, &QToolButton::clicked, this, &FloatingCaptureWindow::captureRegion);
    connect(closeButton_, &QToolButton::clicked, this, &QWidget::hide);
}

void FloatingCaptureWindow::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.fillRect(rect().adjusted(2, 2, -2, -2), QColor(0, 0, 0, 22));
    painter.setPen(QPen(QColor(QStringLiteral("#36CFC9")), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(1, 1, -2, -2));
}

void FloatingCaptureWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    captureButton_->adjustSize();
    closeButton_->setFixedSize(24, 24);
    captureButton_->move(width() / 2 - captureButton_->width() / 2, 6);
    closeButton_->move(width() - 30, 6);
}

void FloatingCaptureWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    interacting_ = true;
    resizeEdges_ = hitTest(event->position().toPoint());
    pressGlobalPosition_ = event->globalPosition().toPoint();
    pressGeometry_ = geometry();
    event->accept();
}

void FloatingCaptureWindow::mouseMoveEvent(QMouseEvent* event) {
    if (!interacting_) {
        updatePointer(event->position().toPoint());
        return;
    }
    const QPoint delta = event->globalPosition().toPoint() - pressGlobalPosition_;
    if (resizeEdges_ == NoEdge) {
        move(pressGeometry_.topLeft() + delta);
        return;
    }
    QRect next = pressGeometry_;
    if (resizeEdges_ & LeftEdge) next.setLeft(next.left() + delta.x());
    if (resizeEdges_ & RightEdge) next.setRight(next.right() + delta.x());
    if (resizeEdges_ & TopEdge) next.setTop(next.top() + delta.y());
    if (resizeEdges_ & BottomEdge) next.setBottom(next.bottom() + delta.y());
    if (next.width() >= minimumWidth() && next.height() >= minimumHeight()) setGeometry(next.normalized());
}

void FloatingCaptureWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        interacting_ = false;
        resizeEdges_ = NoEdge;
        updatePointer(event->position().toPoint());
    }
}

int FloatingCaptureWindow::hitTest(const QPoint& position) const {
    constexpr int margin = 8;
    int result = NoEdge;
    if (position.x() <= margin) result |= LeftEdge;
    if (position.x() >= width() - margin) result |= RightEdge;
    if (position.y() <= margin) result |= TopEdge;
    if (position.y() >= height() - margin) result |= BottomEdge;
    return result;
}

void FloatingCaptureWindow::updatePointer(const QPoint& position) {
    const int edges = hitTest(position);
    if (edges == (LeftEdge | TopEdge) || edges == (RightEdge | BottomEdge)) {
        setCursor(Qt::SizeFDiagCursor);
    } else if (edges == (RightEdge | TopEdge) || edges == (LeftEdge | BottomEdge)) {
        setCursor(Qt::SizeBDiagCursor);
    } else if (edges & (LeftEdge | RightEdge)) {
        setCursor(Qt::SizeHorCursor);
    } else if (edges & (TopEdge | BottomEdge)) {
        setCursor(Qt::SizeVerCursor);
    } else {
        setCursor(Qt::SizeAllCursor);
    }
}

void FloatingCaptureWindow::captureRegion() {
    const QRect captureRect = frameGeometry().adjusted(2, 2, -2, -2);
    hide();
    QTimer::singleShot(90, this, [this, captureRect] {
        const QImage image = grabDesktopRect(captureRect);
        show();
        raise();
        if (image.isNull()) emit captureFailed(QString::fromUtf8("无法抓取当前屏幕区域"));
        else emit imageCaptured(image);
    });
}

WindowCaptureButton::WindowCaptureButton(QWidget* parent)
        : QToolButton(parent) {
    setToolTip(QString::fromUtf8("单击抓取已指定窗口；按住拖动靶心可重新指定"));
    setAutoRaise(true);
    updateTargetIcon(false);
#ifndef Q_OS_WIN
    setEnabled(false);
    setToolTip(QString::fromUtf8("指定窗口抓图当前只支持 Windows"));
#endif
}

void WindowCaptureButton::triggerCapture() {
#ifdef Q_OS_WIN
    if (targetWindow_ == 0) {
        emit captureFailed(QString::fromUtf8("请按住窗口抓图图标，拖到目标窗口后释放"));
        return;
    }
    QString error;
    const QImage image = captureWindow(reinterpret_cast<HWND>(targetWindow_), &error);
    if (image.isNull()) emit captureFailed(error);
    else emit imageCaptured(image);
#else
    emit captureFailed(QString::fromUtf8("指定窗口抓图当前只支持 Windows"));
#endif
}

void WindowCaptureButton::setDarkTheme(bool dark) {
    darkTheme_ = dark;
    updateTargetIcon(dragging_);
}

void WindowCaptureButton::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QToolButton::mousePressEvent(event);
        return;
    }
    pressed_ = true;
    dragging_ = false;
    pressGlobalPosition_ = event->globalPosition().toPoint();
    grabMouse();
    event->accept();
}

void WindowCaptureButton::mouseMoveEvent(QMouseEvent* event) {
    if (!pressed_) return;
    if (!dragging_ && (event->globalPosition().toPoint() - pressGlobalPosition_).manhattanLength()
            >= QApplication::startDragDistance()) {
        dragging_ = true;
        updateTargetIcon(true);
    }
    if (dragging_) updateDragTarget(event->globalPosition().toPoint());
    event->accept();
}

void WindowCaptureButton::mouseReleaseEvent(QMouseEvent* event) {
    if (!pressed_ || event->button() != Qt::LeftButton) return;
    releaseMouse();
    pressed_ = false;
    if (dragging_) {
        if (dragTargetWindow_ != 0) {
            targetWindow_ = dragTargetWindow_;
            emit statusMessage(QString::fromUtf8("已指定窗口，单击图标即可抓图"));
        }
        dragTargetWindow_ = 0;
        dragging_ = false;
        updateTargetIcon(false);
    } else {
        triggerCapture();
    }
    event->accept();
}

quintptr WindowCaptureButton::windowAt(const QPoint& globalPosition) const {
#ifdef Q_OS_WIN
    HWND window = WindowFromPoint({globalPosition.x(), globalPosition.y()});
    if (window == nullptr) return 0;
    window = GetAncestor(window, GA_ROOT);
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    if (processId == GetCurrentProcessId()) return 0;
    return reinterpret_cast<quintptr>(window);
#else
    Q_UNUSED(globalPosition)
    return 0;
#endif
}

void WindowCaptureButton::updateDragTarget(const QPoint& globalPosition) {
    const quintptr next = windowAt(globalPosition);
    if (next == dragTargetWindow_) return;
    dragTargetWindow_ = next;
#ifdef Q_OS_WIN
    if (next == 0) emit statusMessage(QString::fromUtf8("当前没有可指定的外部窗口"));
    else emit statusMessage(windowDescription(reinterpret_cast<HWND>(next)));
#endif
}

void WindowCaptureButton::updateTargetIcon(bool active) {
    setIcon(targetIcon(darkTheme_, active));
    setIconSize(QSize(22, 22));
}

} // namespace xiaoyv::tools
