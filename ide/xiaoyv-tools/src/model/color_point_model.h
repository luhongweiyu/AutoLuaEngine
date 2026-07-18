/**
 * 文件用途：声明每张图片独立持有的取色点模型，负责启用、偏色、基准点和刷新规则。
 */
#pragma once

#include <QAbstractTableModel>
#include <QColor>
#include <QImage>
#include <QPoint>
#include <QVector>

namespace xiaoyv::tools {

struct ColorPoint {
    bool enabled = true;
    QPoint position;
    QColor color = Qt::black;
    QColor delta = Qt::black;
    bool base = false;

    bool operator==(const ColorPoint& other) const = default;
};

class ColorPointModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        SequenceColumn,
        SwatchColumn,
        CoordinateColumn,
        HexColumn,
        DeltaColumn,
        BaseColumn,
        ColumnCount,
    };

    explicit ColorPointModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;

    const QVector<ColorPoint>& points() const;
    const ColorPoint* point(int row) const;
    /** 按表格顺序生成所有启用点的“RRGGBB-偏色”规则，供二值化和点阵面板共用。 */
    QString enabledColorRulesText() const;

    /** 新增点；第一个点自动成为唯一基准点。 */
    void addPoint(const QPoint& position, const QColor& color);
    /** 整体接管上一张图片正在使用的取色点，使工具状态在标签切换时连续。 */
    void replacePoints(const QVector<ColorPoint>& points);
    bool removePoint(int row);
    void clear();
    bool setEnabled(int row, bool enabled);
    /** 修改指定取色点坐标；坐标不在模型层裁剪，刷新颜色时再按当前图片判断有效范围。 */
    bool setPosition(int row, const QPoint& position);
    bool setColor(int row, const QColor& color);
    bool setDelta(int row, const QColor& delta);
    bool setBase(int row);
    bool refreshPoint(int row, const QImage& image);
    int refreshAll(const QImage& image);
    int effectiveBaseRow() const;

signals:
    /** 任一影响代码生成或点阵颜色同步的数据变化后发出。 */
    void pointsChanged();

private:
    void emitRowChanged(int row);
    void ensureBasePoint();

    QVector<ColorPoint> points_;
};

QString colorToHex(const QColor& color);
bool parseRgbHex(const QString& text, QColor* color);

} // namespace xiaoyv::tools
