/**
 * 文件用途：声明一个图片标签视图，把纯图片文档与对应画布组合为可加入 QTabWidget 的控件。
 */
#pragma once

#include <QWidget>

namespace xiaoyv::tools {

class ImageCanvas;
class ImageDocument;

class ImageEditor final : public QWidget {
    Q_OBJECT

public:
    explicit ImageEditor(
            QImage image,
            QString filePath = {},
            QString displayName = {},
            QWidget* parent = nullptr);

    ImageDocument* document() const;
    ImageCanvas* canvas() const;

private:
    ImageDocument* document_ = nullptr;
    ImageCanvas* canvas_ = nullptr;
};

} // namespace xiaoyv::tools
