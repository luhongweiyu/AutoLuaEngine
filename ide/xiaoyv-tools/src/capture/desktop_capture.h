/**
 * 文件用途：声明浮动区域抓图和“单击抓取、拖动选窗”的 Windows 指定窗口抓图控件。
 */
#pragma once

#include <QImage>
#include <QToolButton>
#include <QWidget>

namespace xiaoyv::tools {

/** 可移动、可缩放、可重复抓取的透明置顶选区。 */
class FloatingCaptureWindow final : public QWidget {
    Q_OBJECT

public:
    explicit FloatingCaptureWindow(QWidget* parent = nullptr);

signals:
    void imageCaptured(const QImage& image);
    void captureFailed(const QString& message);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    enum ResizeEdge {
        NoEdge = 0,
        LeftEdge = 1,
        TopEdge = 2,
        RightEdge = 4,
        BottomEdge = 8,
    };

    int hitTest(const QPoint& position) const;
    void updatePointer(const QPoint& position);
    void captureRegion();

    QToolButton* captureButton_ = nullptr;
    QToolButton* closeButton_ = nullptr;
    bool interacting_ = false;
    int resizeEdges_ = NoEdge;
    QPoint pressGlobalPosition_;
    QRect pressGeometry_;
};

/** 单击抓取当前目标，按住拖动靶心到其他窗口后释放则更新目标。 */
class WindowCaptureButton final : public QToolButton {
    Q_OBJECT

public:
    explicit WindowCaptureButton(QWidget* parent = nullptr);

    void triggerCapture();
    void setDarkTheme(bool dark);

signals:
    void imageCaptured(const QImage& image);
    void captureFailed(const QString& message);
    void statusMessage(const QString& message);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    quintptr windowAt(const QPoint& globalPosition) const;
    void updateDragTarget(const QPoint& globalPosition);
    void updateTargetIcon(bool active);

    quintptr targetWindow_ = 0;
    quintptr dragTargetWindow_ = 0;
    bool pressed_ = false;
    bool dragging_ = false;
    bool darkTheme_ = true;
    QPoint pressGlobalPosition_;
};

} // namespace xiaoyv::tools
