/**
 * 文件用途：声明固定格子尺寸的点阵预览控件；默认只读，明确启用后才允许逐点修改。
 */
#pragma once

#include <QWidget>

#include <cstdint>
#include <vector>

namespace xiaoyv::tools {

class PixelGrid final : public QWidget {
    Q_OBJECT

public:
    explicit PixelGrid(QWidget* parent = nullptr);

    void setEditable(bool editable);
    void setMask(int width, int height, std::vector<std::uint8_t> mask);

signals:
    void maskChanged(int width, int height, const std::vector<std::uint8_t>& mask);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    QSize sizeHint() const override;

private:
    static constexpr int kCellSize = 6;
    static constexpr int kPadding = 5;
    int width_ = 0;
    int height_ = 0;
    bool editable_ = false;
    std::vector<std::uint8_t> mask_;
};

} // namespace xiaoyv::tools
