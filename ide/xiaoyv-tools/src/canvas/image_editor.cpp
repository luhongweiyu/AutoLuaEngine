/**
 * 文件用途：实现图片文档与画布的一对一组合，不在此层复制任何图片业务规则。
 */
#include "canvas/image_editor.h"

#include "canvas/image_canvas.h"
#include "model/image_document.h"

#include <QVBoxLayout>

namespace xiaoyv::tools {

ImageEditor::ImageEditor(
        QImage image,
        QString filePath,
        QString displayName,
        QWidget* parent)
        : QWidget(parent) {
    document_ = new ImageDocument(
            std::move(image), std::move(filePath), std::move(displayName), this);
    canvas_ = new ImageCanvas(this);
    canvas_->setDocument(document_);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(canvas_);
}

ImageDocument* ImageEditor::document() const {
    return document_;
}

ImageCanvas* ImageEditor::canvas() const {
    return canvas_;
}

} // namespace xiaoyv::tools
