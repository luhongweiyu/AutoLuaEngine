/**
 * 文件用途：实现动态取色点表格模型和唯一基准点规则。
 */
#include "model/color_point_model.h"

#include <QRegularExpression>
#include <QStringList>

namespace xiaoyv::tools {

QString colorToHex(const QColor& color) {
    return QStringLiteral("%1%2%3")
            .arg(color.red(), 2, 16, QLatin1Char('0'))
            .arg(color.green(), 2, 16, QLatin1Char('0'))
            .arg(color.blue(), 2, 16, QLatin1Char('0'))
            .toUpper();
}

bool parseRgbHex(const QString& text, QColor* color) {
    if (color == nullptr) return false;
    QString value = text.trimmed();
    if (value.startsWith(QLatin1Char('#'))) value.remove(0, 1);
    if (value.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) value.remove(0, 2);
    if (!QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{6}$")).match(value).hasMatch()) {
        return false;
    }
    bool ok = false;
    const uint rgb = value.toUInt(&ok, 16);
    if (!ok) return false;
    *color = QColor::fromRgb(rgb);
    return true;
}

ColorPointModel::ColorPointModel(QObject* parent)
        : QAbstractTableModel(parent) {}

int ColorPointModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : points_.size();
}

int ColorPointModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ColorPointModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= points_.size()) return {};
    const ColorPoint& item = points_[index.row()];

    if (role == Qt::CheckStateRole && index.column() == SequenceColumn) {
        return item.enabled ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::CheckStateRole && index.column() == BaseColumn) {
        return item.base ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::DecorationRole && index.column() == SwatchColumn) {
        // 颜色块由专用委托绘制在选中背景之上，避免整行蓝色高亮把颜色完全遮住。
        return item.color;
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};

    switch (index.column()) {
        case SequenceColumn:
            return index.row() + 1;
        case CoordinateColumn:
            return QStringLiteral("%1,%2").arg(item.position.x()).arg(item.position.y());
        case HexColumn:
            return colorToHex(item.color);
        case DeltaColumn:
            return colorToHex(item.delta);
        default:
            return {};
    }
}

QVariant ColorPointModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case SequenceColumn: return QString::fromUtf8("序");
        case SwatchColumn: return QString{};
        case CoordinateColumn: return QString::fromUtf8("坐标");
        case HexColumn: return QStringLiteral("RGB");
        case DeltaColumn: return QString::fromUtf8("偏色");
        case BaseColumn: return QString::fromUtf8("基准");
        default: return {};
    }
}

Qt::ItemFlags ColorPointModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == SequenceColumn || index.column() == BaseColumn) {
        result |= Qt::ItemIsUserCheckable;
    }
    if (index.column() == CoordinateColumn
            || index.column() == HexColumn
            || index.column() == DeltaColumn) {
        result |= Qt::ItemIsEditable;
    }
    return result;
}

bool ColorPointModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() < 0 || index.row() >= points_.size()) return false;
    if (index.column() == SequenceColumn && role == Qt::CheckStateRole) {
        return setEnabled(index.row(), value.toInt() == Qt::Checked);
    }
    if (index.column() == BaseColumn && role == Qt::CheckStateRole
            && value.toInt() == Qt::Checked) {
        return setBase(index.row());
    }
    if (index.column() == CoordinateColumn && role == Qt::EditRole) {
        const QRegularExpressionMatch match = QRegularExpression(
                QStringLiteral("^\\s*(-?\\d+)\\s*,\\s*(-?\\d+)\\s*$"))
                .match(value.toString());
        if (!match.hasMatch()) return false;
        bool xOk = false;
        bool yOk = false;
        const int x = match.captured(1).toInt(&xOk);
        const int y = match.captured(2).toInt(&yOk);
        return xOk && yOk && setPosition(index.row(), QPoint(x, y));
    }
    if (index.column() == HexColumn && role == Qt::EditRole) {
        QColor parsed;
        return parseRgbHex(value.toString(), &parsed) && setColor(index.row(), parsed);
    }
    if (index.column() == DeltaColumn && role == Qt::EditRole) {
        QColor parsed;
        return parseRgbHex(value.toString(), &parsed) && setDelta(index.row(), parsed);
    }
    return false;
}

const QVector<ColorPoint>& ColorPointModel::points() const {
    return points_;
}

const ColorPoint* ColorPointModel::point(int row) const {
    return row >= 0 && row < points_.size() ? &points_[row] : nullptr;
}

QString ColorPointModel::enabledColorRulesText() const {
    QStringList rules;
    rules.reserve(points_.size());
    for (const ColorPoint& point : points_) {
        if (!point.enabled) continue;
        rules.push_back(colorToHex(point.color)
                + QLatin1Char('-')
                + colorToHex(point.delta));
    }
    return rules.join(QLatin1Char('|'));
}

void ColorPointModel::addPoint(const QPoint& position, const QColor& color) {
    const int row = points_.size();
    beginInsertRows({}, row, row);
    points_.push_back({true, position, color, Qt::black, points_.isEmpty()});
    endInsertRows();
    emit pointsChanged();
}

void ColorPointModel::replacePoints(const QVector<ColorPoint>& points) {
    if (points_ == points) return;
    beginResetModel();
    points_ = points;
    bool hasBase = false;
    for (const ColorPoint& point : points_) {
        if (point.base) {
            hasBase = true;
            break;
        }
    }
    if (!points_.isEmpty() && !hasBase) points_.front().base = true;
    endResetModel();
    emit pointsChanged();
}

bool ColorPointModel::removePoint(int row) {
    if (row < 0 || row >= points_.size()) return false;
    beginRemoveRows({}, row, row);
    points_.removeAt(row);
    endRemoveRows();
    ensureBasePoint();
    emit pointsChanged();
    return true;
}

void ColorPointModel::clear() {
    if (points_.isEmpty()) return;
    beginResetModel();
    points_.clear();
    endResetModel();
    emit pointsChanged();
}

bool ColorPointModel::setEnabled(int row, bool enabled) {
    if (row < 0 || row >= points_.size() || points_[row].enabled == enabled) return false;
    points_[row].enabled = enabled;
    emitRowChanged(row);
    emit pointsChanged();
    return true;
}

bool ColorPointModel::setPosition(int row, const QPoint& position) {
    if (row < 0 || row >= points_.size() || points_[row].position == position) return false;
    points_[row].position = position;
    emitRowChanged(row);
    emit pointsChanged();
    return true;
}

bool ColorPointModel::setColor(int row, const QColor& color) {
    if (row < 0 || row >= points_.size() || !color.isValid() || points_[row].color == color) {
        return false;
    }
    points_[row].color = color;
    emitRowChanged(row);
    emit pointsChanged();
    return true;
}

bool ColorPointModel::setDelta(int row, const QColor& delta) {
    if (row < 0 || row >= points_.size() || !delta.isValid() || points_[row].delta == delta) {
        return false;
    }
    points_[row].delta = delta;
    emitRowChanged(row);
    emit pointsChanged();
    return true;
}

bool ColorPointModel::setBase(int row) {
    if (row < 0 || row >= points_.size()) return false;
    bool changed = false;
    for (int index = 0; index < points_.size(); ++index) {
        const bool shouldBeBase = index == row;
        if (points_[index].base == shouldBeBase) continue;
        points_[index].base = shouldBeBase;
        emitRowChanged(index);
        changed = true;
    }
    if (changed) emit pointsChanged();
    return changed;
}

bool ColorPointModel::refreshPoint(int row, const QImage& image) {
    if (row < 0 || row >= points_.size() || image.isNull()
            || !image.rect().contains(points_[row].position)) {
        return false;
    }
    const QColor sampled = image.pixelColor(points_[row].position);
    if (sampled == points_[row].color) return true;
    points_[row].color = sampled;
    emitRowChanged(row);
    emit pointsChanged();
    return true;
}

int ColorPointModel::refreshAll(const QImage& image) {
    if (image.isNull()) return 0;
    int refreshed = 0;
    bool changed = false;
    for (int row = 0; row < points_.size(); ++row) {
        if (!image.rect().contains(points_[row].position)) continue;
        const QColor sampled = image.pixelColor(points_[row].position);
        if (sampled != points_[row].color) {
            points_[row].color = sampled;
            emitRowChanged(row);
            changed = true;
        }
        ++refreshed;
    }
    if (changed) emit pointsChanged();
    return refreshed;
}

int ColorPointModel::effectiveBaseRow() const {
    for (int row = 0; row < points_.size(); ++row) {
        if (points_[row].enabled && points_[row].base) return row;
    }
    for (int row = 0; row < points_.size(); ++row) {
        if (points_[row].enabled) return row;
    }
    return -1;
}

void ColorPointModel::emitRowChanged(int row) {
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
}

void ColorPointModel::ensureBasePoint() {
    if (points_.isEmpty()) return;
    for (const ColorPoint& point : points_) {
        if (point.base) return;
    }
    points_.front().base = true;
    emitRowChanged(0);
}

} // namespace xiaoyv::tools
